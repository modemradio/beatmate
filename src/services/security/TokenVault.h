#pragma once
#include <string>
#include <map>
namespace BeatMate::Services::Security {
class TokenVault {
public:
    TokenVault() = default;
    void store(const std::string& service, const std::string& token);
    std::string retrieve(const std::string& service) const;
    bool has(const std::string& service) const;
    void remove(const std::string& service);
private:
    std::string protectData(const std::string& data) const;
    std::string unprotectData(const std::string& data) const;
    std::map<std::string, std::string> tokens_;
};
} // namespace BeatMate::Services::Security
