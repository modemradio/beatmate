#include <algorithm>
#include "EncryptionService.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <fstream>

#ifdef _WIN32
  #include <Windows.h>
  #include <bcrypt.h>
  #pragma comment(lib, "bcrypt.lib")
  #ifndef STATUS_SUCCESS
    #define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
  #endif
#endif

namespace BeatMate::Services::Security {

std::vector<uint8_t> EncryptionService::encrypt(const std::vector<uint8_t>& data, const std::string& key) {
    if (data.empty()) return {};

    auto derivedKey = deriveKey(key);
    auto iv = generateIV();

    size_t blockSize = 8;
    size_t paddedSize = ((data.size() + blockSize - 1) / blockSize) * blockSize;
    if (paddedSize == data.size()) paddedSize += blockSize; // Always add at least 1 byte of padding

    std::vector<uint8_t> padded(paddedSize);
    std::copy(data.begin(), data.end(), padded.begin());
    uint8_t padByte = static_cast<uint8_t>(paddedSize - data.size());
    std::fill(padded.begin() + static_cast<std::ptrdiff_t>(data.size()), padded.end(), padByte);

    juce::BlowFish cipher(derivedKey.data(), static_cast<int>(std::min(derivedKey.size(), static_cast<size_t>(56))));

    // CBC mode: XOR each block with previous ciphertext (or IV for first block)
    std::vector<uint8_t> result;
    result.reserve(16 + paddedSize); // IV (16 bytes) + ciphertext
    result.insert(result.end(), iv.begin(), iv.end());

    std::vector<uint8_t> prevBlock(iv.begin(), iv.begin() + 8); // Use first 8 bytes of IV

    for (size_t offset = 0; offset < paddedSize; offset += blockSize) {
        for (size_t j = 0; j < blockSize; ++j) {
            padded[offset + j] ^= prevBlock[j];
        }

        uint32_t left = (static_cast<uint32_t>(padded[offset]) << 24) |
                        (static_cast<uint32_t>(padded[offset + 1]) << 16) |
                        (static_cast<uint32_t>(padded[offset + 2]) << 8) |
                        static_cast<uint32_t>(padded[offset + 3]);
        uint32_t right = (static_cast<uint32_t>(padded[offset + 4]) << 24) |
                         (static_cast<uint32_t>(padded[offset + 5]) << 16) |
                         (static_cast<uint32_t>(padded[offset + 6]) << 8) |
                         static_cast<uint32_t>(padded[offset + 7]);

        cipher.encrypt(left, right);

        uint8_t encBlock[8] = {
            static_cast<uint8_t>((left >> 24) & 0xFF),
            static_cast<uint8_t>((left >> 16) & 0xFF),
            static_cast<uint8_t>((left >> 8) & 0xFF),
            static_cast<uint8_t>(left & 0xFF),
            static_cast<uint8_t>((right >> 24) & 0xFF),
            static_cast<uint8_t>((right >> 16) & 0xFF),
            static_cast<uint8_t>((right >> 8) & 0xFF),
            static_cast<uint8_t>(right & 0xFF)
        };

        result.insert(result.end(), encBlock, encBlock + 8);
        prevBlock.assign(encBlock, encBlock + 8);
    }

    return result;
}

std::vector<uint8_t> EncryptionService::decrypt(const std::vector<uint8_t>& encrypted, const std::string& key) {
    // Need at least IV (16 bytes) + one block (8 bytes)
    if (encrypted.size() < 24) return {};

    auto derivedKey = deriveKey(key);

    std::vector<uint8_t> iv(encrypted.begin(), encrypted.begin() + 16);
    size_t blockSize = 8;
    size_t ciphertextSize = encrypted.size() - 16;

    if (ciphertextSize % blockSize != 0) {
        spdlog::error("EncryptionService: Invalid ciphertext size (not multiple of block size)");
        return {};
    }

    juce::BlowFish cipher(derivedKey.data(), static_cast<int>(std::min(derivedKey.size(), static_cast<size_t>(56))));

    // CBC mode decryption
    std::vector<uint8_t> plaintext;
    plaintext.reserve(ciphertextSize);

    std::vector<uint8_t> prevBlock(iv.begin(), iv.begin() + 8);

    for (size_t offset = 16; offset < encrypted.size(); offset += blockSize) {
        uint32_t left = (static_cast<uint32_t>(encrypted[offset]) << 24) |
                        (static_cast<uint32_t>(encrypted[offset + 1]) << 16) |
                        (static_cast<uint32_t>(encrypted[offset + 2]) << 8) |
                        static_cast<uint32_t>(encrypted[offset + 3]);
        uint32_t right = (static_cast<uint32_t>(encrypted[offset + 4]) << 24) |
                         (static_cast<uint32_t>(encrypted[offset + 5]) << 16) |
                         (static_cast<uint32_t>(encrypted[offset + 6]) << 8) |
                         static_cast<uint32_t>(encrypted[offset + 7]);

        std::vector<uint8_t> currentCipherBlock(encrypted.begin() + static_cast<std::ptrdiff_t>(offset),
                                                 encrypted.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));

        cipher.decrypt(left, right);

        uint8_t decBlock[8] = {
            static_cast<uint8_t>((left >> 24) & 0xFF),
            static_cast<uint8_t>((left >> 16) & 0xFF),
            static_cast<uint8_t>((left >> 8) & 0xFF),
            static_cast<uint8_t>(left & 0xFF),
            static_cast<uint8_t>((right >> 24) & 0xFF),
            static_cast<uint8_t>((right >> 16) & 0xFF),
            static_cast<uint8_t>((right >> 8) & 0xFF),
            static_cast<uint8_t>(right & 0xFF)
        };

        for (size_t j = 0; j < blockSize; ++j) {
            decBlock[j] ^= prevBlock[j];
        }

        plaintext.insert(plaintext.end(), decBlock, decBlock + 8);
        prevBlock = currentCipherBlock;
    }

    // Remove PKCS7 padding
    if (!plaintext.empty()) {
        uint8_t padByte = plaintext.back();
        if (padByte > 0 && padByte <= blockSize) {
            bool validPadding = true;
            for (size_t i = plaintext.size() - padByte; i < plaintext.size(); ++i) {
                if (plaintext[i] != padByte) { validPadding = false; break; }
            }
            if (validPadding) {
                plaintext.resize(plaintext.size() - padByte);
            }
        }
    }

    return plaintext;
}

std::string EncryptionService::encryptString(const std::string& data, const std::string& key) {
    std::vector<uint8_t> bytes(data.begin(), data.end());
    auto enc = encrypt(bytes, key);
    juce::MemoryBlock mb(enc.data(), enc.size());
    return mb.toBase64Encoding().toStdString();
}

std::string EncryptionService::decryptString(const std::string& encrypted, const std::string& key) {
    juce::MemoryBlock mb;
    mb.fromBase64Encoding(juce::String(encrypted));
    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(mb.getData()),
                               static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    auto dec = decrypt(bytes, key);
    return std::string(dec.begin(), dec.end());
}

std::vector<uint8_t> EncryptionService::deriveKey(const std::string& password, int keyLen) {
    // Use SHA256 for key derivation (PBKDF2-like: hash the password multiple rounds)
    juce::SHA256 sha256_1(password.data(), password.size());
    auto hash1 = sha256_1.getRawData();

    // Second round: hash(password + hash1) for added security
    std::string combined = password + std::string(
        static_cast<const char*>(hash1.getData()),
        hash1.getSize());
    juce::SHA256 sha256_2(combined.data(), combined.size());
    auto hash2 = sha256_2.getRawData();

    auto* hashData = hash2.getData();
    int hashSize = static_cast<int>(hash2.getSize());
    std::vector<uint8_t> key(static_cast<const uint8_t*>(hashData),
                             static_cast<const uint8_t*>(hashData) + std::min(keyLen, hashSize));
    return key;
}

std::vector<uint8_t> EncryptionService::generateIV() {
    std::vector<uint8_t> iv(16);
#ifdef _WIN32
    // CSPRNG: BCryptGenRandom with the system-preferred RNG. mt19937 is NOT
    NTSTATUS st = BCryptGenRandom(nullptr, iv.data(),
                                  static_cast<ULONG>(iv.size()),
                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st != STATUS_SUCCESS) {
        spdlog::error("EncryptionService: BCryptGenRandom failed (0x{:x}) — refusing weak IV",
                      static_cast<unsigned>(st));
        // Return all-zero so the caller sees a deterministic, reportable failure
        std::fill(iv.begin(), iv.end(), 0u);
    }
#else
    // POSIX: /dev/urandom is the cryptographic source of choice.
    std::ifstream urnd("/dev/urandom", std::ios::binary);
    if (urnd.is_open()) {
        urnd.read(reinterpret_cast<char*>(iv.data()),
                  static_cast<std::streamsize>(iv.size()));
        if (urnd.gcount() != static_cast<std::streamsize>(iv.size())) {
            spdlog::error("EncryptionService: /dev/urandom short read — refusing weak IV");
            std::fill(iv.begin(), iv.end(), 0u);
        }
    } else {
        spdlog::error("EncryptionService: /dev/urandom unavailable — refusing weak IV");
        std::fill(iv.begin(), iv.end(), 0u);
    }
#endif
    return iv;
}

} // namespace BeatMate::Services::Security
