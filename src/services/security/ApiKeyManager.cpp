#include "ApiKeyManager.h"
#include "EncryptionService.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
namespace BeatMate::Services::Security {
void ApiKeyManager::store(const std::string& service, const std::string& key) { keys_[service] = key; spdlog::debug("ApiKeyManager: Stored key for {}", service); }
std::string ApiKeyManager::retrieve(const std::string& service) const { auto it = keys_.find(service); return (it != keys_.end()) ? it->second : ""; }
bool ApiKeyManager::hasKey(const std::string& service) const { return keys_.find(service) != keys_.end(); }
void ApiKeyManager::remove(const std::string& service) { keys_.erase(service); }
bool ApiKeyManager::saveToFile(const std::string& path, const std::string& encryptionKey) {
    nlohmann::json j = keys_;
    EncryptionService enc;
    std::string encrypted = enc.encryptString(j.dump(), encryptionKey);
    std::ofstream f(path); if (!f.is_open()) return false;
    f << encrypted; return true;
}
bool ApiKeyManager::loadFromFile(const std::string& path, const std::string& encryptionKey) {
    std::ifstream f(path); if (!f.is_open()) return false;
    std::string encrypted((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EncryptionService enc;
    try { auto j = nlohmann::json::parse(enc.decryptString(encrypted, encryptionKey)); keys_ = j.get<std::map<std::string, std::string>>(); return true; }
    catch (...) { spdlog::error("ApiKeyManager: Failed to load keys"); return false; }
}
} // namespace BeatMate::Services::Security
