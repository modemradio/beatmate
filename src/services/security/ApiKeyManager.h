#pragma once
#include <string>
#include <map>
namespace BeatMate::Services::Security {
class ApiKeyManager {
public:
    ApiKeyManager() = default;
    void store(const std::string& service, const std::string& key);
    std::string retrieve(const std::string& service) const;
    bool hasKey(const std::string& service) const;
    void remove(const std::string& service);
    bool saveToFile(const std::string& path, const std::string& encryptionKey);
    bool loadFromFile(const std::string& path, const std::string& encryptionKey);
private:
    std::map<std::string, std::string> keys_;
};
} // namespace BeatMate::Services::Security
