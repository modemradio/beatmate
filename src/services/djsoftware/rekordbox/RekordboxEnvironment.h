#pragma once

#include <string>

namespace BeatMate::Services::Rekordbox {

class RekordboxEnvironment {
public:
    RekordboxEnvironment() = default;
    ~RekordboxEnvironment() = default;

    bool isInstalled() const;
    std::string findDatabasePath() const;
    std::string findXmlPath() const;
    std::string getInstallPath() const;
    std::string getVersion() const;
    int getMajorVersion() const;

private:
    std::string getAppDataPath() const;
};

} // namespace BeatMate::Services::Rekordbox
