#pragma once
#include <string>
#include <functional>
namespace BeatMate::Services::Network {
class SyncEngine {
public:
    SyncEngine() = default;
    bool syncWithRemote(const std::string& endpoint);
    void setAuthToken(const std::string& token) { token_ = token; }
private:
    std::string token_;
};
}
