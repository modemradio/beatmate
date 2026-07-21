#include "WebSocketClient.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <chrono>
#include <regex>

namespace BeatMate::Services::Network {

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
    disconnect();
}

void WebSocketClient::connect(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    url_ = url;
    shouldStop_ = false;

    // Parse URL: ws://host:port/path or wss://host:port/path
    std::regex urlRegex(R"(wss?://([^:/]+):?(\d+)?(/.*)?)" );
    std::smatch match;
    if (!std::regex_match(url, match, urlRegex)) {
        spdlog::error("WebSocketClient: Invalid URL: {}", url);
        return;
    }

    std::string host = match[1].str();
    int port = match[2].matched ? std::stoi(match[2].str()) : 80;
    std::string path = match[3].matched ? match[3].str() : "/";

    socket_ = std::make_unique<juce::StreamingSocket>();
    if (socket_->connect(host, port, 5000)) {
        std::string key = juce::Uuid().toString().toStdString();
        std::string handshake =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + ":" + std::to_string(port) + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + key + "\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n";

        socket_->write(handshake.c_str(), (int)handshake.size());

        char buffer[2048] = {};
        int bytesRead = socket_->read(buffer, sizeof(buffer) - 1, false);

        if (bytesRead > 0 && std::string(buffer).find("101") != std::string::npos) {
            connected_ = true;
            spdlog::info("WebSocketClient: Connected to {}", url);
            if (connectedCallback_) connectedCallback_();

            if (receiveThread_.joinable()) receiveThread_.join();
            receiveThread_ = std::thread([this]() { receiveLoop(); });
        } else {
            spdlog::error("WebSocketClient: Handshake failed with {}", url);
            socket_.reset();
        }
    } else {
        spdlog::error("WebSocketClient: TCP connect failed to {}:{}", host, port);
        socket_.reset();
    }

    if (!connected_ && autoReconnect_) {
        if (reconnectThread_.joinable()) reconnectThread_.join();
        reconnectThread_ = std::thread([this]() { reconnectLoop(); });
    }
}

void WebSocketClient::disconnect() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shouldStop_ = true;
        autoReconnect_ = false;
    }

    if (socket_) {
        socket_->close();
    }

    if (receiveThread_.joinable()) receiveThread_.join();
    if (reconnectThread_.joinable()) reconnectThread_.join();

    if (connected_) {
        connected_ = false;
        spdlog::info("WebSocketClient: Disconnected");
        if (disconnectedCallback_) disconnectedCallback_();
    }

    socket_.reset();
}

void WebSocketClient::send(const std::string& message) {
    if (!connected_ || !socket_) {
        spdlog::warn("WebSocketClient: Cannot send, not connected");
        return;
    }

    // WebSocket frame: text frame (opcode 0x81), masked
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + text opcode

    size_t len = message.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(0x80 | len)); // masked + length
    } else if (len < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    }

    // Masking key (4 random bytes)
    uint8_t mask[4];
    juce::Random rng;
    for (int i = 0; i < 4; ++i) mask[i] = static_cast<uint8_t>(rng.nextInt(256));
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i)
        frame.push_back(static_cast<uint8_t>(message[i]) ^ mask[i % 4]);

    std::lock_guard<std::mutex> lock(mutex_);
    socket_->write(frame.data(), (int)frame.size());
    spdlog::debug("WebSocketClient: Sent {} bytes", message.size());
}

bool WebSocketClient::isConnected() const {
    return connected_.load();
}

void WebSocketClient::receiveLoop() {
    char buffer[8192];
    while (!shouldStop_ && connected_ && socket_) {
        if (socket_->waitUntilReady(true, 100) == 1) {
            int bytesRead = socket_->read(buffer, sizeof(buffer) - 1, false);
            if (bytesRead <= 0) {
                connected_ = false;
                spdlog::warn("WebSocketClient: Connection lost");
                if (disconnectedCallback_) disconnectedCallback_();
                break;
            }

            // Parse WebSocket frame (simplified - text frames only)
            if (bytesRead >= 2) {
                uint8_t opcode = buffer[0] & 0x0F;
                bool masked = (buffer[1] & 0x80) != 0;
                size_t payloadLen = buffer[1] & 0x7F;
                size_t headerLen = 2;

                if (payloadLen == 126 && bytesRead >= 4) {
                    payloadLen = (static_cast<uint8_t>(buffer[2]) << 8) | static_cast<uint8_t>(buffer[3]);
                    headerLen = 4;
                }

                if (masked) headerLen += 4;

                if (opcode == 0x01 && headerLen + payloadLen <= (size_t)bytesRead) {
                    if (masked && headerLen >= 4) {
                        const unsigned char* mask = reinterpret_cast<const unsigned char*>(buffer + headerLen - 4);
                        for (size_t i = 0; i < payloadLen; ++i)
                            buffer[headerLen + i] = static_cast<char>(
                                static_cast<unsigned char>(buffer[headerLen + i]) ^ mask[i % 4]);
                    }
                    std::string msg(buffer + headerLen, payloadLen);
                    if (messageCallback_) messageCallback_(msg);
                } else if (opcode == 0x08) {
                    // Close frame
                    connected_ = false;
                    break;
                } else if (opcode == 0x09) {
                    // Ping - send pong
                    char pong[2] = {(char)0x8A, 0x00};
                    socket_->write(pong, 2);
                }
            }
        }
    }
}

void WebSocketClient::reconnectLoop() {
    while (!shouldStop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnectInterval_));
        if (shouldStop_) break;

        if (!connected_ && autoReconnect_) {
            spdlog::info("WebSocketClient: Attempting reconnect to {}", url_);
            connect(url_);
            if (connected_) break;
        }
    }
}

} // namespace BeatMate::Services::Network
