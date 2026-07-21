#pragma once

#include <string>

namespace BeatMate::Services::VirtualDJ {

class VirtualDJEnvironment {
public:
    VirtualDJEnvironment() = default;
    ~VirtualDJEnvironment() = default;

    bool isInstalled() const;
    std::string findInstallation() const;
    std::string getDatabasePath() const;
    std::string getVersion() const;

private:
    std::string getDocumentsPath() const;
};

} // namespace BeatMate::Services::VirtualDJ
