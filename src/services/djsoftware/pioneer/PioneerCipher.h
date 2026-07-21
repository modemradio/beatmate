#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace BeatMate::Services::PioneerDJ {

class PioneerCipher {
public:
    PioneerCipher() = default;
    ~PioneerCipher() = default;

    // Derive the database key from SSID/app credentials
    std::string deriveKey(const std::string& ssid = "");

    // Lookup SSID from Pioneer configuration files
    std::string lookupSSID();

    // Fallback key for known Rekordbox versions
    std::string getFallbackKey(int majorVersion = 6);

    // Decrypt the database file to a temporary location
    bool decryptDatabase(const std::string& encryptedPath, const std::string& outputPath, const std::string& key);

private:
    std::vector<uint8_t> pbkdf2(const std::string& password, const std::vector<uint8_t>& salt, int iterations, int keyLength);
    std::string readPioneerConfig();
};

} // namespace BeatMate::Services::PioneerDJ
