#pragma once
#include <memory>
namespace BeatMate::Services::Config { class SettingsManager; }
namespace BeatMate::Services::Config {
class DJSoftwareAutoConfig {
public:
    explicit DJSoftwareAutoConfig(std::shared_ptr<SettingsManager> settings);
    void autoDetectAndConfigure();
private:
    std::shared_ptr<SettingsManager> settings_;
};
}
