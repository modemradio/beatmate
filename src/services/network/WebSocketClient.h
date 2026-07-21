#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Network {

// Minimal WebSocket client; background thread handles reconnection
class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    void connect(const std::string& url);
    void disconnect();
    void send(const std::string& message);
    bool isConnected() const;

    void setAutoReconnect(bool enable, int intervalMs = 5000) {
        autoReconnect_ = enable;
        reconnectInterval_ = intervalMs;
    }

    void onMessage(std::function<void(const std::string&)> callback) { messageCallback_ = std::move(callback); }
    void onConnected(std::function<void()> callback) { connectedCallback_ = std::move(callback); }
    void onDisconnected(std::function<void()> callback) { disconnectedCallback_ = std::move(callback); }
    void onError(std::function<void(const std::string&)> callback) { errorCallback_ = std::move(callback); }

private:
    void reconnectLoop();
    void receiveLoop();

    std::unique_ptr<juce::StreamingSocket> socket_;
    std::thread receiveThread_;
    std::string url_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> autoReconnect_{true};
    int reconnectInterval_ = 5000;
    std::atomic<bool> shouldStop_{false};

    std::thread reconnectThread_;
    std::mutex mutex_;

    std::function<void(const std::string&)> messageCallback_;
    std::function<void()> connectedCallback_;
    std::function<void()> disconnectedCallback_;
    std::function<void(const std::string&)> errorCallback_;

    // Handle for a future full WebSocket implementation
    void* wsHandle_ = nullptr;
};

} // namespace BeatMate::Services::Network
