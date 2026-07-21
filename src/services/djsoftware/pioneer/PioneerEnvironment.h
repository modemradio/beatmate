#pragma once

#include <string>

namespace BeatMate::Services::PioneerDJ {

class PioneerEnvironment {
public:
    PioneerEnvironment() = default;
    ~PioneerEnvironment() = default;

    bool isInstalled() const;
    std::string findDatabasePath() const;
    std::string findXmlPath() const;
    std::string getInstallPath() const;
    std::string getVersion() const;
    int getMajorVersion() const;

private:
    std::string getAppDataPath() const;
};

} // namespace BeatMate::Services::PioneerDJ
