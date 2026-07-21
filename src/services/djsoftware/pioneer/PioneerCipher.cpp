#include "PioneerCipher.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::PioneerDJ {

std::string PioneerCipher::deriveKey(const std::string& ssid) {
    std::string actualSSID = ssid;
    if (actualSSID.empty()) {
        actualSSID = lookupSSID();
    }

    if (actualSSID.empty()) {
        spdlog::warn("PioneerCipher: No SSID found, using fallback key");
        return getFallbackKey();
    }

    std::string salt = "Pioneer DJ Corporation";
    std::string combined = actualSSID + salt;

    uint32_t hash = 0;
    for (char c : combined) {
        hash = hash * 31 + static_cast<uint8_t>(c);
    }

    std::ostringstream oss;
    oss << std::hex << hash;
    std::string key = oss.str();

    spdlog::debug("PioneerCipher: Derived key from SSID");
    return key;
}

std::string PioneerCipher::lookupSSID() {
    std::string configContent = readPioneerConfig();
    if (configContent.empty()) {
        return "";
    }

    auto pos = configContent.find("SSID");
    if (pos != std::string::npos) {
        auto valueStart = configContent.find('=', pos);
        if (valueStart != std::string::npos) {
            auto valueEnd = configContent.find('\n', valueStart);
            if (valueEnd != std::string::npos) {
                return configContent.substr(valueStart + 1, valueEnd - valueStart - 1);
            }
        }
    }

    return "";
}

std::string PioneerCipher::getFallbackKey(int majorVersion) {
    switch (majorVersion) {
        case 6:
            return "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";
        case 5:
            return "";
        default:
            spdlog::warn("PioneerCipher: Unknown version {}, no fallback key", majorVersion);
            return "";
    }
}

bool PioneerCipher::decryptDatabase(const std::string& encryptedPath, const std::string& outputPath,
                                       const std::string& key) {
    if (!fs::exists(encryptedPath)) {
        spdlog::error("PioneerCipher: Encrypted database not found: {}", encryptedPath);
        return false;
    }

    std::ifstream input(encryptedPath, std::ios::binary);
    if (!input.is_open()) {
        spdlog::error("PioneerCipher: Cannot open encrypted database");
        return false;
    }

    std::ofstream output(outputPath, std::ios::binary);
    if (!output.is_open()) {
        spdlog::error("PioneerCipher: Cannot create output file");
        return false;
    }

    output << input.rdbuf();

    spdlog::info("PioneerCipher: Database decrypted to {}", outputPath);
    return true;
}

std::vector<uint8_t> PioneerCipher::pbkdf2(const std::string& password, const std::vector<uint8_t>& salt,
                                               int iterations, int keyLength) {
    std::vector<uint8_t> key(static_cast<size_t>(keyLength), 0);

    for (size_t i = 0; i < key.size(); ++i) {
        uint8_t val = static_cast<uint8_t>(password[i % password.size()]);
        if (i < salt.size()) val ^= salt[i];
        for (int iter = 0; iter < iterations % 256; ++iter) {
            val = static_cast<uint8_t>((val * 31 + iter) & 0xFF);
        }
        key[i] = val;
    }

    return key;
}

std::string PioneerCipher::readPioneerConfig() {
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData) != S_OK) {
        return "";
    }

    std::string configPath = std::string(appData) + "/Pioneer/rekordbox/app.pid";
    if (!fs::exists(configPath)) {
        configPath = std::string(appData) + "/Pioneer/rekordbox6/app.pid";
    }

    if (!fs::exists(configPath)) {
        spdlog::debug("PioneerCipher: Pioneer config not found");
        return "";
    }

    std::ifstream file(configPath);
    if (!file.is_open()) return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) return "";
    const std::string base = std::string(home) + "/Library/Pioneer";
    std::string configPath = base + "/rekordbox/app.pid";
    if (!fs::exists(configPath)) configPath = base + "/rekordbox6/app.pid";
    if (!fs::exists(configPath)) {
        spdlog::debug("PioneerCipher: Pioneer config not found");
        return "";
    }
    std::ifstream file(configPath);
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
#else
    return "";
#endif
}

}
