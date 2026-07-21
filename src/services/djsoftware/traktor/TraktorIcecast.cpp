#include "TraktorIcecast.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <regex>

namespace BeatMate::Services::Traktor {

namespace {
static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Parse a Shoutcast ICY metadata chunk body. Format is:
static std::pair<std::string, std::string> parseIcyMetadata(const std::string& body) {
    static const std::regex re(R"(StreamTitle='([^']*)')");
    std::smatch m;
    if (!std::regex_search(body, m, re)) return {{}, {}};
    std::string s = m[1].str();
    auto sep = s.find(" - ");
    if (sep == std::string::npos) return {{}, s};
    return { s.substr(0, sep), s.substr(sep + 3) };
}

// Parse the Shoutcast/Icecast SOURCE request headers and any initial
static void consumeSourceHeaders(const std::string& headers,
                                 TraktorBroadcastTrack& out) {
    static const std::regex re(R"((?:ice-name|icy-name):\s*([^\r\n]+))",
                               std::regex::icase);
    std::smatch m;
    if (std::regex_search(headers, m, re)) {
        auto [a, t] = parseIcyMetadata("StreamTitle='" + m[1].str() + "';");
        if (!t.empty()) {
            out.artist = a;
            out.title  = t;
            out.lastUpdateMs = nowMs();
        }
    }
}
} // namespace

TraktorIcecast::TraktorIcecast()
    : serverSocket_(std::make_unique<juce::StreamingSocket>()) {
}

TraktorIcecast::~TraktorIcecast() {
    stopServer();
}

bool TraktorIcecast::startServer(int port) {
    if (running_) {
        spdlog::warn("TraktorIcecast: Server already running on port {}", port_);
        return false;
    }

    port_ = port;

    if (!serverSocket_->createListener(port)) {
        spdlog::error("TraktorIcecast: Failed to start on port {}", port);
        return false;
    }

    running_ = true;
    acceptThread_ = std::make_unique<std::thread>(&TraktorIcecast::acceptLoop, this);
    spdlog::info("TraktorIcecast: Server started on port {}, mount point: {}", port, mountPoint_);
    return true;
}

void TraktorIcecast::stopServer() {
    if (!running_) return;

    running_ = false;
    serverSocket_->close();

    if (acceptThread_ && acceptThread_->joinable()) {
        acceptThread_->join();
    }
    acceptThread_.reset();

    std::vector<ClientSession> sessions;
    {
        std::lock_guard<std::mutex> lk(sessionsMutex_);
        sessions.swap(sessions_);
    }
    for (auto& s : sessions) {
        if (s.socket) s.socket->close();
    }
    for (auto& s : sessions) {
        if (s.thread.joinable()) s.thread.join();
    }
    spdlog::info("TraktorIcecast: Server stopped");
}

bool TraktorIcecast::isRunning() const {
    return running_;
}

int TraktorIcecast::getPort() const {
    return port_;
}

std::optional<TraktorBroadcastTrack> TraktorIcecast::getLatestTrack() const {
    std::lock_guard<std::mutex> lk(trackMutex_);
    if (latestTrack_.title.empty()) return std::nullopt;
    if (nowMs() - latestTrack_.lastUpdateMs > 60000) return std::nullopt;
    return latestTrack_;
}

void TraktorIcecast::acceptLoop() {
    while (running_) {
        if (!running_) break;

        auto* newClient = serverSocket_->waitForNextConnection();
        if (newClient != nullptr) {
            std::lock_guard<std::mutex> lk(sessionsMutex_);
            sessions_.push_back(ClientSession{});
            auto& session = sessions_.back();
            session.socket.reset(newClient);
            session.thread = std::thread(&TraktorIcecast::handleClient, this, newClient);
        }
    }
}

void TraktorIcecast::handleClient(juce::StreamingSocket* client) {
    if (!client) return;

    std::string clientInfo = client->getHostName().toStdString() + ":" + std::to_string(client->getPort());
    if (clientConnectedCallback_) clientConnectedCallback_(clientInfo);
    spdlog::info("TraktorIcecast: Client connected: {}", clientInfo);

    const char* response =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: audio/mpeg\r\n"
        "icy-name: BeatMate Traktor Stream\r\n"
        "icy-genre: Electronic\r\n"
        "\r\n";
    client->write(response, static_cast<int>(strlen(response)));

    char buffer[4096];
    while (running_ && client->isConnected()) {
        if (client->waitUntilReady(true, 500) == 1) {
            int bytesRead = client->read(buffer, sizeof(buffer), false);
            if (bytesRead <= 0) break;

            std::string request(buffer, bytesRead);

            if (request.find("SOURCE") != std::string::npos || request.find("PUT") != std::string::npos) {
                if (!password_.empty()) {
                    const std::string creds = "source:" + password_;
                    const std::string expected = "Basic " + juce::Base64::toBase64(creds.data(), creds.size()).toStdString();
                    static const std::regex authRe(R"(Authorization:\s*([^\r\n]+))", std::regex::icase);
                    std::smatch am;
                    const bool authorized = std::regex_search(request, am, authRe) && am[1].str() == expected;
                    if (!authorized) {
                        const char* denied = "HTTP/1.0 401 Unauthorized\r\n\r\n";
                        client->write(denied, static_cast<int>(strlen(denied)));
                        spdlog::warn("TraktorIcecast: Source rejected (bad credentials)");
                        break;
                    }
                }
                spdlog::info("TraktorIcecast: Source client connected");
                const char* okResponse = "HTTP/1.0 200 OK\r\n\r\n";
                client->write(okResponse, static_cast<int>(strlen(okResponse)));
                {
                    std::lock_guard<std::mutex> lk(trackMutex_);
                    consumeSourceHeaders(request, latestTrack_);
                }
            }

            // Shoutcast inline metadata: look for "StreamTitle='...'" anywhere
            auto [artist, title] = parseIcyMetadata(request);
            if (!title.empty()) {
                std::lock_guard<std::mutex> lk(trackMutex_);
                latestTrack_.artist = artist;
                latestTrack_.title  = title;
                latestTrack_.lastUpdateMs = nowMs();
                spdlog::info("[TraktorIcecast] StreamTitle: '{}' - '{}'", artist, title);
            }

            if (dataReceivedCallback_) dataReceivedCallback_(bytesRead);
        }
    }

    if (clientDisconnectedCallback_) clientDisconnectedCallback_(clientInfo);
}

} // namespace BeatMate::Services::Traktor
