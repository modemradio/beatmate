#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <sqlite3.h>

namespace BeatMate::Services::Rekordbox {

class RekordboxCipher {
public:
    RekordboxCipher() = default;
    ~RekordboxCipher() = default;

    std::string deriveKey(const std::string& ssid = "");

    std::string lookupSSID();

    std::string getFallbackKey(int majorVersion = 6);

    bool decryptDatabase(const std::string& encryptedPath, const std::string& outputPath, const std::string& key);

    static int openEncrypted(const std::string& dbPath,
                             const std::string& hexKey,
                             sqlite3** outDb);

    static std::string readPasswordFromOptionsJson();

private:
    std::vector<uint8_t> pbkdf2(const std::string& password, const std::vector<uint8_t>& salt, int iterations, int keyLength);
    std::string readPioneerConfig();
};

}
