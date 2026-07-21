#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
namespace BeatMate::Services::Security {
class IntegrityCheck {
public:
    IntegrityCheck() = default;
    std::string computeHash(const std::string& filePath);
    bool verify(const std::string& filePath, const std::string& expectedHash);
    bool verifyAll(const std::map<std::string, std::string>& fileHashes);
    void setReferenceHashes(const std::map<std::string, std::string>& hashes) { refHashes_ = hashes; }

    // Anti-tampering: check critical sections of the binary
    bool checkCriticalSections();

    // Store the initial hash computed at startup for later comparison
    void storeStartupHash(const std::string& exePath);

    // Verify that the binary hasn't been modified since startup
    bool verifyBinaryIntegrity() const;

private:
    std::map<std::string, std::string> refHashes_;
    std::string startupExePath_;
    std::string startupExeHash_;
};
} // namespace BeatMate::Services::Security
