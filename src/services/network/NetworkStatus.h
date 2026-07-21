#pragma once
#include <functional>
#include <string>

namespace BeatMate::Services::Network {

class NetworkStatus {
public:
    NetworkStatus();
    ~NetworkStatus() = default;

    bool isOnline();
    void onStatusChanged(std::function<void(bool)> callback) { callback_ = std::move(callback); }

private:
    std::function<void(bool)> callback_;
    bool lastStatus_ = false;
};

} // namespace BeatMate::Services::Network
