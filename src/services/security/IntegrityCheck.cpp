#include "IntegrityCheck.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <fstream>

namespace BeatMate::Services::Security {

std::string IntegrityCheck::computeHash(const std::string& filePath) {
    juce::File file(filePath);
    if (!file.existsAsFile()) { spdlog::error("IntegrityCheck: Cannot open {}", filePath); return ""; }
    juce::FileInputStream stream(file);
    if (stream.failedToOpen()) { spdlog::error("IntegrityCheck: Cannot open {}", filePath); return ""; }
    juce::MemoryBlock fileData;
    stream.readIntoMemoryBlock(fileData);
    juce::SHA256 sha256(fileData.getData(), fileData.getSize());
    return sha256.toHexString().toStdString();
}

bool IntegrityCheck::verify(const std::string& filePath, const std::string& expectedHash) {
    std::string actual = computeHash(filePath);
    bool ok = (actual == expectedHash);
    if (!ok) spdlog::warn("IntegrityCheck: Hash mismatch for {}", filePath);
    return ok;
}

bool IntegrityCheck::verifyAll(const std::map<std::string, std::string>& fileHashes) {
    for (const auto& [file, hash] : fileHashes) {
        if (!verify(file, hash)) return false;
    }
    return true;
}

void IntegrityCheck::storeStartupHash(const std::string& exePath) {
    startupExePath_ = exePath;
    startupExeHash_ = computeHash(exePath);
    if (!startupExeHash_.empty()) {
        spdlog::info("IntegrityCheck: Startup hash stored ({}...)", startupExeHash_.substr(0, 16));
    }
}

bool IntegrityCheck::verifyBinaryIntegrity() const {
    if (startupExePath_.empty() || startupExeHash_.empty()) {
        spdlog::warn("IntegrityCheck: No startup hash available for verification");
        return true;
    }

    juce::File file(startupExePath_);
    if (!file.existsAsFile()) {
        spdlog::error("IntegrityCheck: Binary not found at {}", startupExePath_);
        return false;
    }

    juce::FileInputStream stream(file);
    if (stream.failedToOpen()) {
        spdlog::error("IntegrityCheck: Cannot open binary for verification");
        return false;
    }

    juce::MemoryBlock fileData;
    stream.readIntoMemoryBlock(fileData);
    juce::SHA256 sha256(fileData.getData(), fileData.getSize());
    auto currentHash = sha256.toHexString().toStdString();

    if (currentHash != startupExeHash_) {
        spdlog::error("IntegrityCheck: Binary has been modified since startup!");
        spdlog::error("IntegrityCheck: Expected {}..., got {}...",
                      startupExeHash_.substr(0, 16), currentHash.substr(0, 16));
        return false;
    }

    return true;
}

bool IntegrityCheck::checkCriticalSections() {
    if (!verifyBinaryIntegrity()) {
        spdlog::error("IntegrityCheck: Critical section check FAILED - binary patched");
        return false;
    }

    if (startupExePath_.empty()) return true;

    std::ifstream file(startupExePath_, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return true;

    auto fileSize = file.tellg();
    if (fileSize <= 0) return true;

    file.seekg(0, std::ios::beg);

    char mzSig[2] = {};
    file.read(mzSig, 2);
    if (mzSig[0] != 'M' || mzSig[1] != 'Z') {
        return true;
    }

    file.seekg(0x3C, std::ios::beg);
    uint32_t peOffset = 0;
    file.read(reinterpret_cast<char*>(&peOffset), sizeof(peOffset));

    if (peOffset == 0 || static_cast<std::streamoff>(peOffset + 4) >= fileSize) return true;

    file.seekg(peOffset, std::ios::beg);
    char peSig[4] = {};
    file.read(peSig, 4);
    if (peSig[0] != 'P' || peSig[1] != 'E' || peSig[2] != 0 || peSig[3] != 0) {
        return true;
    }

    uint16_t numberOfSections = 0;
    file.seekg(peOffset + 6, std::ios::beg);
    file.read(reinterpret_cast<char*>(&numberOfSections), sizeof(numberOfSections));

    uint16_t optionalHeaderSize = 0;
    file.seekg(peOffset + 20, std::ios::beg);
    file.read(reinterpret_cast<char*>(&optionalHeaderSize), sizeof(optionalHeaderSize));

    auto sectionStart = peOffset + 24 + optionalHeaderSize;

    for (uint16_t i = 0; i < numberOfSections; ++i) {
        auto sectionOffset = sectionStart + (i * 40);
        if (static_cast<std::streamoff>(sectionOffset + 40) > fileSize) break;

        file.seekg(sectionOffset, std::ios::beg);
        char sectionName[9] = {};
        file.read(sectionName, 8);

        if (std::string(sectionName) == ".text") {
            uint32_t virtualSize = 0, rawDataOffset = 0, rawDataSize = 0;
            file.seekg(sectionOffset + 8, std::ios::beg);
            file.read(reinterpret_cast<char*>(&virtualSize), sizeof(virtualSize));
            file.seekg(sectionOffset + 16, std::ios::beg);
            file.read(reinterpret_cast<char*>(&rawDataSize), sizeof(rawDataSize));
            file.seekg(sectionOffset + 20, std::ios::beg);
            file.read(reinterpret_cast<char*>(&rawDataOffset), sizeof(rawDataOffset));

            if (rawDataSize > 0 && static_cast<std::streamoff>(rawDataOffset + rawDataSize) <= fileSize) {
                std::vector<uint8_t> textSection(rawDataSize);
                file.seekg(rawDataOffset, std::ios::beg);
                file.read(reinterpret_cast<char*>(textSection.data()), rawDataSize);

                juce::SHA256 textHash(textSection.data(), textSection.size());
                auto hashStr = textHash.toHexString().toStdString();

                static std::string s_textSectionHash;
                if (s_textSectionHash.empty()) {
                    s_textSectionHash = hashStr;
                    spdlog::info("IntegrityCheck: .text section hash stored ({}...)", hashStr.substr(0, 16));
                } else if (s_textSectionHash != hashStr) {
                    spdlog::error("IntegrityCheck: .text section has been patched!");
                    return false;
                }
            }
            break;
        }
    }

    return true;
}

}
