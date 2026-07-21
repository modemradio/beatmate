#pragma once

#include <string>
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Traktor {

using ClientConnectedCallback = std::function<void(const std::string& clientInfo)>;
using ClientDisconnectedCallback = std::function<void(const std::string& clientInfo)>;
using DataReceivedCallback = std::function<void(int bytes)>;

struct TraktorBroadcastTrack {
    std::string artist;
    std::string title;
    int64_t     lastUpdateMs = 0;
};

class TraktorIcecast {
public:
    TraktorIcecast();
    ~TraktorIcecast();

    bool startServer(int port = 8000);
    void stopServer();
    bool isRunning() const;
    int getPort() const;

    std::optional<TraktorBroadcastTrack> getLatestTrack() const;

    void setMountPoint(const std::string& mount) { mountPoint_ = mount; }
    void setPassword(const std::string& password) { password_ = password; }

    void setClientConnectedCallback(ClientConnectedCallback cb) { clientConnectedCallback_ = std::move(cb); }
    void setClientDisconnectedCallback(ClientDisconnectedCallback cb) { clientDisconnectedCallback_ = std::move(cb); }
    void setDataReceivedCallback(DataReceivedCallback cb) { dataReceivedCallback_ = std::move(cb); }

private:
    void acceptLoop();
    void handleClient(juce::StreamingSocket* client);

    struct ClientSession {
        std::unique_ptr<juce::StreamingSocket> socket;
        std::thread thread;
    };

    std::unique_ptr<juce::StreamingSocket> serverSocket_;
    std::vector<ClientSession> sessions_;
    std::mutex sessionsMutex_;
    std::unique_ptr<std::thread> acceptThread_;
    std::string mountPoint_ = "/stream";
    std::string password_;
    int port_ = 8000;
    std::atomic<bool> running_{false};

    ClientConnectedCallback clientConnectedCallback_;
    ClientDisconnectedCallback clientDisconnectedCallback_;
    DataReceivedCallback dataReceivedCallback_;

    mutable std::mutex trackMutex_;
    TraktorBroadcastTrack latestTrack_;
};

}
