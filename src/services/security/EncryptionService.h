#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace BeatMate::Services::Security {
class EncryptionService {
public:
    EncryptionService() = default;
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data, const std::string& key);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted, const std::string& key);
    std::string encryptString(const std::string& data, const std::string& key);
    std::string decryptString(const std::string& encrypted, const std::string& key);
private:
    std::vector<uint8_t> deriveKey(const std::string& password, int keyLen = 32);
    std::vector<uint8_t> generateIV();
};
}
