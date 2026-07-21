#include "RekordboxCipher.h"

#include <spdlog/spdlog.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_core/juce_core.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::Rekordbox {

static constexpr const char* kRekordboxBlowfishMagic = "ZOwUlUZYqe9Rdm6j";

static std::vector<uint8_t> base64Decode(const std::string& s) {
    auto charVal = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : s) {
        if (c == '=') break;
        int v = charVal(c);
        if (v < 0) continue;
        val = (val << 6) | v;
        valb += 6;
        if (valb >= 0) { out.push_back((uint8_t)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

std::string RekordboxCipher::readPasswordFromOptionsJson() {
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData) != S_OK) return {};
    std::string path = std::string(appData) +
        "\\Pioneer\\rekordboxAgent\\storage\\options.json";
#else
    const char* home = std::getenv("HOME");
    if (!home) return {};
    std::string path = std::string(home) +
        "/Library/Application Support/Pioneer/rekordboxAgent/storage/options.json";
#endif
    if (!fs::exists(path)) {
        spdlog::debug("[RBCipher] options.json not found at {}", path);
        return {};
    }
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss; ss << f.rdbuf();
    std::string content = ss.str();
    auto dpPos = content.find("\"dp\"");
    if (dpPos == std::string::npos) {
        spdlog::debug("[RBCipher] 'dp' key absent from options.json");
        return {};
    }
    auto comma = content.find(',', dpPos);
    if (comma == std::string::npos) return {};
    auto q1 = content.find('"', comma);
    if (q1 == std::string::npos) return {};
    auto q2 = content.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    std::string b64 = content.substr(q1 + 1, q2 - q1 - 1);
    auto ct = base64Decode(b64);
    if (ct.empty() || (ct.size() % 8) != 0) {
        spdlog::warn("[RBCipher] dp ciphertext has invalid length {}", ct.size());
        return {};
    }
    juce::BlowFish bf(kRekordboxBlowfishMagic, (int) std::strlen(kRekordboxBlowfishMagic));
    std::vector<uint8_t> pt; pt.reserve(ct.size());
    for (size_t i = 0; i < ct.size(); i += 8) {
        juce::uint32 l = ((juce::uint32)ct[i] << 24) | ((juce::uint32)ct[i+1] << 16)
                       | ((juce::uint32)ct[i+2] << 8) | (juce::uint32)ct[i+3];
        juce::uint32 r = ((juce::uint32)ct[i+4] << 24) | ((juce::uint32)ct[i+5] << 16)
                       | ((juce::uint32)ct[i+6] << 8) | (juce::uint32)ct[i+7];
        bf.decrypt(l, r);
        pt.push_back((uint8_t)((l >> 24) & 0xFF)); pt.push_back((uint8_t)((l >> 16) & 0xFF));
        pt.push_back((uint8_t)((l >> 8) & 0xFF));  pt.push_back((uint8_t)( l        & 0xFF));
        pt.push_back((uint8_t)((r >> 24) & 0xFF)); pt.push_back((uint8_t)((r >> 16) & 0xFF));
        pt.push_back((uint8_t)((r >> 8) & 0xFF));  pt.push_back((uint8_t)( r        & 0xFF));
    }
    if (!pt.empty()) {
        uint8_t pad = pt.back();
        if (pad >= 1 && pad <= 8 && pt.size() >= pad) {
            bool ok = true;
            for (size_t i = 0; i < pad; ++i) if (pt[pt.size()-1-i] != pad) { ok = false; break; }
            if (ok) pt.resize(pt.size() - pad);
        }
    }
    std::string result(pt.begin(), pt.end());
    while (!result.empty() && (result.back() == ' ' || result.back() == '\n' ||
                                result.back() == '\r' || result.back() == '\t'))
        result.pop_back();
    if (result.empty()) {
        spdlog::warn("[RBCipher] dp decrypted to empty string - magic key mismatch?");
    } else {
        spdlog::info("[RBCipher] recovered db password from options.json (len={})",
                     result.size());
    }
    return result;
}

std::string RekordboxCipher::deriveKey(const std::string& ssid) {
    std::string actualSSID = ssid;
    if (actualSSID.empty()) {
        actualSSID = lookupSSID();
    }

    if (actualSSID.empty()) {
        spdlog::warn("RekordboxCipher: No SSID found, using fallback key");
        return getFallbackKey();
    }

    std::string salt = "Pioneer DJ Corporation";
    std::string combined = actualSSID + salt;

    std::vector<uint8_t> saltBytes(salt.begin(), salt.end());
    auto derivedKey = pbkdf2(combined, saltBytes, 10000, 32);

    std::ostringstream oss;
    for (auto byte : derivedKey) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }

    spdlog::debug("RekordboxCipher: Derived key from SSID");
    return oss.str();
}

std::string RekordboxCipher::lookupSSID() {
    std::string configContent = readPioneerConfig();
    if (configContent.empty()) {
        return "";
    }

    auto pos = configContent.find("SSID");
    if (pos != std::string::npos) {
        auto valueStart = configContent.find('=', pos);
        if (valueStart == std::string::npos)
            valueStart = configContent.find(':', pos);
        if (valueStart != std::string::npos) {
            valueStart++;
            while (valueStart < configContent.size() &&
                   (configContent[valueStart] == ' ' || configContent[valueStart] == '"'))
                valueStart++;
            auto valueEnd = configContent.find_first_of("\n\r\"", valueStart);
            if (valueEnd != std::string::npos) {
                return configContent.substr(valueStart, valueEnd - valueStart);
            }
        }
    }

    return "";
}

std::string RekordboxCipher::getFallbackKey(int majorVersion) {
    switch (majorVersion) {
        case 6:
            return "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";
        case 5:
            return "";
        default:
            spdlog::warn("RekordboxCipher: Unknown version {}, no fallback key", majorVersion);
            return "";
    }
}

bool RekordboxCipher::decryptDatabase(const std::string& encryptedPath, const std::string& outputPath,
                                       const std::string& key) {
    if (!fs::exists(encryptedPath)) {
        spdlog::error("RekordboxCipher: Encrypted database not found: {}", encryptedPath);
        return false;
    }


    {
        sqlite3* encDb = nullptr;
        int rc = openEncrypted(encryptedPath, key, &encDb);
        if (rc == SQLITE_OK && encDb) {
            std::string attachSql = "ATTACH DATABASE '" + outputPath + "' AS plaintext KEY ''";
            rc = sqlite3_exec(encDb, attachSql.c_str(), nullptr, nullptr, nullptr);
            if (rc == SQLITE_OK) {
                rc = sqlite3_exec(encDb, "SELECT sqlcipher_export('plaintext')",
                                  nullptr, nullptr, nullptr);
                if (rc == SQLITE_OK) {
                    sqlite3_exec(encDb, "DETACH DATABASE plaintext", nullptr, nullptr, nullptr);
                    sqlite3_close(encDb);
                    spdlog::info("RekordboxCipher: decrypted master.db via openEncrypted -> {}",
                                 outputPath);
                    return true;
                }
                spdlog::debug("RekordboxCipher: sqlcipher_export failed rc={} (silent fallback)", rc);
            } else {
                spdlog::debug("RekordboxCipher: ATTACH plaintext failed rc={} (silent fallback, read-only handle)", rc);
            }
            sqlite3_close(encDb);
        } else {
            spdlog::debug("RekordboxCipher: openEncrypted failed rc={} (silent fallback)", rc);
        }
    }

    {
        sqlite3* testDb = nullptr;
        int rc = sqlite3_open_v2(encryptedPath.c_str(), &testDb, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(testDb, "SELECT COUNT(*) FROM sqlite_master", -1, &stmt, nullptr);
            if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                sqlite3_finalize(stmt);
                sqlite3_close(testDb);
                std::ifstream src(encryptedPath, std::ios::binary);
                std::ofstream dst(outputPath, std::ios::binary);
                dst << src.rdbuf();
                spdlog::info("RekordboxCipher: Database is unencrypted, copied to {}", outputPath);
                return true;
            }
            if (stmt) sqlite3_finalize(stmt);
            sqlite3_close(testDb);
        } else if (testDb) {
            sqlite3_close(testDb);
        }
    }

    if (!key.empty()) {
        sqlite3* rawDb = nullptr;
        int rc = sqlite3_open_v2(encryptedPath.c_str(), &rawDb, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK) {
            std::string pragmaKey = "PRAGMA key = '" + key + "'";
            sqlite3_exec(rawDb, pragmaKey.c_str(), nullptr, nullptr, nullptr);

            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(rawDb, "SELECT COUNT(*) FROM sqlite_master", -1, &stmt, nullptr);
            if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
                sqlite3_finalize(stmt);
                sqlite3_close(rawDb);
                std::ifstream src(encryptedPath, std::ios::binary);
                std::ofstream dst(outputPath, std::ios::binary);
                dst << src.rdbuf();
                spdlog::info("RekordboxCipher: Database readable with key, copied to {}", outputPath);
                return true;
            }
            if (stmt) sqlite3_finalize(stmt);
            sqlite3_close(rawDb);
        } else if (rawDb) {
            sqlite3_close(rawDb);
        }
    }

    spdlog::debug("RekordboxCipher: Could not decrypt database at {} (cold-start fallback path "
                  "silent; openDatabase will retry via direct openEncrypted handle).", encryptedPath);
    return false;
}

int RekordboxCipher::openEncrypted(const std::string& dbPath,
                                    const std::string& hexKey,
                                    sqlite3** outDb)
{
    if (outDb) *outDb = nullptr;
    if (!fs::exists(dbPath)) {
        spdlog::warn("[RB] openEncrypted: DB not found at {}", dbPath);
        return SQLITE_CANTOPEN;
    }

    std::vector<std::string> candidates;
    std::string fromOptions = RekordboxCipher::readPasswordFromOptionsJson();
    if (!fromOptions.empty()) {
        candidates.push_back(fromOptions);
        spdlog::info("[RBCipher] using password from options.json (primary)");
    }
    if (!hexKey.empty() && hexKey != fromOptions) {
        candidates.push_back(hexKey);
    }
    if (candidates.empty()) {
        spdlog::warn("[RB] openEncrypted: no password available");
        return SQLITE_MISUSE;
    }

    enum class Mode { V4Default, V3Legacy, V4Compat, V4Plain32 };
    enum class KeyForm { Passphrase, RawHex };
    struct Variant {
        const char* label;
        bool        useUri;
        Mode        mode;
        KeyForm     keyForm;
    };
    const Variant variants[] = {
        { "A1: URI + v4 defaults + PASSPHRASE (rekordbox-connect path)", true,  Mode::V4Default, KeyForm::Passphrase },
        { "A2: URI + cipher_compatibility=4 + PASSPHRASE",               true,  Mode::V4Compat,  KeyForm::Passphrase },
        { "A3: READONLY + v4 defaults + PASSPHRASE",                     false, Mode::V4Default, KeyForm::Passphrase },
        { "A4: URI + v4 + plaintext_header_size=32 + PASSPHRASE",        true,  Mode::V4Plain32, KeyForm::Passphrase },
        { "A5: URI + v3 legacy + PASSPHRASE",                            true,  Mode::V3Legacy,  KeyForm::Passphrase },
        { "B1: URI + v4 defaults + RAW HEX",                             true,  Mode::V4Default, KeyForm::RawHex },
        { "B2: URI + cipher_compatibility=4 + RAW HEX",                  true,  Mode::V4Compat,  KeyForm::RawHex },
        { "B3: URI + v4 + plaintext_header_size=32 + RAW HEX",           true,  Mode::V4Plain32, KeyForm::RawHex },
        { "B4: URI + v3 legacy + RAW HEX",                               true,  Mode::V3Legacy,  KeyForm::RawHex },
        { "B5: READONLY + v4 defaults + RAW HEX",                        false, Mode::V4Default, KeyForm::RawHex },
        { "B6: READONLY + cipher_compatibility=4 + RAW HEX",             false, Mode::V4Compat,  KeyForm::RawHex },
        { "B7: READONLY + v3 legacy + RAW HEX",                          false, Mode::V3Legacy,  KeyForm::RawHex },
    };

    auto tryOpen = [&](const Variant& v, const std::string& pwd) -> int {
        sqlite3* db = nullptr;
        int rc;
        if (v.useUri) {
            std::string uri = "file:" + dbPath + "?mode=ro";
            rc = sqlite3_open_v2(uri.c_str(), &db,
                                 SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
        } else {
            rc = sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        }
        if (rc != SQLITE_OK || !db) {
            spdlog::info("[RB] try variant {} -> OPEN FAIL rc={} ({})",
                         v.label, rc, db ? sqlite3_errmsg(db) : "null-db");
            if (db) sqlite3_close(db);
            return rc != SQLITE_OK ? rc : SQLITE_CANTOPEN;
        }

        auto execPragma = [&](const char* sql) {
            char* err = nullptr;
            int r = sqlite3_exec(db, sql, nullptr, nullptr, &err);
            if (r != SQLITE_OK) {
                spdlog::debug("[RB] pragma '{}' -> rc={} ({})", sql, r, err ? err : "");
            }
            if (err) sqlite3_free(err);
            return r;
        };

        execPragma("PRAGMA cipher = 'sqlcipher';");
        switch (v.mode) {
            case Mode::V4Default:
                // SQLCipher 4 defaults - pyrekordbox / Rekordbox 6/7 use these.
                execPragma("PRAGMA legacy = 4;");
                execPragma("PRAGMA kdf_iter = 256000;");
                execPragma("PRAGMA cipher_page_size = 4096;");
                execPragma("PRAGMA cipher_hmac_algorithm = HMAC_SHA512;");
                execPragma("PRAGMA cipher_kdf_algorithm  = PBKDF2_HMAC_SHA512;");
                break;
            case Mode::V3Legacy:
                execPragma("PRAGMA legacy = 3;");
                execPragma("PRAGMA kdf_iter = 64000;");
                execPragma("PRAGMA cipher_page_size = 1024;");
                execPragma("PRAGMA cipher_hmac_algorithm = HMAC_SHA1;");
                execPragma("PRAGMA cipher_kdf_algorithm  = PBKDF2_HMAC_SHA1;");
                break;
            case Mode::V4Compat:
                execPragma("PRAGMA cipher_compatibility = 4;");
                break;
            case Mode::V4Plain32:
                execPragma("PRAGMA legacy = 4;");
                execPragma("PRAGMA kdf_iter = 256000;");
                execPragma("PRAGMA cipher_page_size = 4096;");
                execPragma("PRAGMA cipher_hmac_algorithm = HMAC_SHA512;");
                execPragma("PRAGMA cipher_kdf_algorithm  = PBKDF2_HMAC_SHA512;");
                execPragma("PRAGMA cipher_plaintext_header_size = 32;");
                break;
        }

        std::string pragmaKey;
        if (v.keyForm == KeyForm::Passphrase) {
            std::string esc; esc.reserve(pwd.size());
            for (char c : pwd) { if (c == '\'') esc += "''"; else esc += c; }
            pragmaKey = "PRAGMA key = '" + esc + "';";
        } else {
            pragmaKey = "PRAGMA key = \"x'" + pwd + "'\";";
        }
        int kr = sqlite3_exec(db, pragmaKey.c_str(), nullptr, nullptr, nullptr);
        if (kr != SQLITE_OK) {
            spdlog::info("[RB] try variant {} -> PRAGMA key FAIL rc={} ({})",
                         v.label, kr, sqlite3_errmsg(db));
            sqlite3_close(db);
            return kr;
        }

        sqlite3_stmt* probe = nullptr;
        int pr = sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master;",
                                    -1, &probe, nullptr);
        if (pr == SQLITE_OK && sqlite3_step(probe) == SQLITE_ROW) {
            sqlite3_finalize(probe);
            spdlog::info("[RB] try variant {} -> OK (master.db unlocked)", v.label);
            if (outDb) { *outDb = db; } else { sqlite3_close(db); }
            return SQLITE_OK;
        }
        std::string err = sqlite3_errmsg(db);
        if (probe) sqlite3_finalize(probe);
        spdlog::info("[RB] try variant {} -> PROBE FAIL ({})", v.label, err);
        sqlite3_close(db);
        return SQLITE_NOTADB;
    };

    for (const auto& pwd : candidates) {
        spdlog::info("[RBCipher] trying password candidate (len={})", pwd.size());
        for (const auto& v : variants) {
            int rc = tryOpen(v, pwd);
            if (rc == SQLITE_OK) {
                spdlog::info("[RBCipher] decrypted master.db via method {} (pwd len={})",
                             v.label, pwd.size());
                return SQLITE_OK;
            }
        }
    }

    spdlog::warn("[RB] openEncrypted: every variant failed for {}", dbPath);
    return SQLITE_NOTADB;
}

std::vector<uint8_t> RekordboxCipher::pbkdf2(const std::string& password, const std::vector<uint8_t>& salt,
                                               int iterations, int keyLength) {
    std::vector<uint8_t> key(static_cast<size_t>(keyLength), 0);

#ifdef _WIN32
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                                   nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (BCRYPT_SUCCESS(status)) {
        status = BCryptDeriveKeyPBKDF2(
            hAlg,
            reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
            static_cast<ULONG>(password.size()),
            const_cast<PUCHAR>(salt.data()),
            static_cast<ULONG>(salt.size()),
            static_cast<ULONGLONG>(iterations),
            key.data(),
            static_cast<ULONG>(key.size()),
            0);

        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (BCRYPT_SUCCESS(status)) {
            return key;
        }
        spdlog::warn("RekordboxCipher: BCryptDeriveKeyPBKDF2 failed, using fallback");
    }
#endif

    for (size_t block = 0; block < static_cast<size_t>(keyLength); block += 32) {
        std::array<uint8_t, 32> u{};
        for (size_t i = 0; i < 32 && (block + i) < static_cast<size_t>(keyLength); ++i) {
            uint8_t val = static_cast<uint8_t>(password[i % password.size()]);
            if (i < salt.size()) val ^= salt[i];
            val ^= static_cast<uint8_t>((block / 32) + 1);
            u[i] = val;
        }
        for (int iter = 1; iter < iterations; ++iter) {
            for (size_t i = 0; i < 32; ++i) {
                u[i] = static_cast<uint8_t>((u[i] * 31 + static_cast<uint8_t>(iter & 0xFF)) & 0xFF);
            }
        }
        for (size_t i = 0; i < 32 && (block + i) < static_cast<size_t>(keyLength); ++i) {
            key[block + i] = u[i];
        }
    }

    return key;
}

std::string RekordboxCipher::readPioneerConfig() {
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData) != S_OK) {
        return "";
    }

    std::vector<std::string> configPaths = {
        std::string(appData) + "/Pioneer/rekordbox6/app.pid",
        std::string(appData) + "/Pioneer/rekordbox/app.pid",
        std::string(appData) + "/Pioneer/rekordbox6/options.json",
        std::string(appData) + "/Pioneer/rekordbox/options.json"
    };

    for (const auto& configPath : configPaths) {
        if (fs::exists(configPath)) {
            std::ifstream file(configPath);
            if (file.is_open()) {
                std::ostringstream ss;
                ss << file.rdbuf();
                std::string content = ss.str();
                if (!content.empty()) {
                    spdlog::debug("RekordboxCipher: Found Pioneer config at {}", configPath);
                    return content;
                }
            }
        }
    }

    spdlog::debug("RekordboxCipher: Pioneer config not found");
    return "";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) return "";
    const std::string base = std::string(home) + "/Library/Pioneer";
    std::vector<std::string> configPaths = {
        base + "/rekordbox6/app.pid",
        base + "/rekordbox/app.pid",
        base + "/rekordbox6/options.json",
        base + "/rekordbox/options.json",
    };
    for (const auto& configPath : configPaths) {
        if (fs::exists(configPath)) {
            std::ifstream file(configPath);
            if (file.is_open()) {
                std::ostringstream ss;
                ss << file.rdbuf();
                std::string content = ss.str();
                if (!content.empty()) return content;
            }
        }
    }
    return "";
#else
    return "";
#endif
}

}
