#include "DJSoftwareAutoConfig.h"
#include "SettingsManager.h"
#include "../djsoftware/DJSoftwareDetector.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Config {
DJSoftwareAutoConfig::DJSoftwareAutoConfig(std::shared_ptr<SettingsManager> settings) : settings_(std::move(settings)) {}
void DJSoftwareAutoConfig::autoDetectAndConfigure() {
    DJSoftware::DJSoftwareDetector detector;
    auto detected = detector.detect();
    for (const auto& info : detected) {
        settings_->set("djsoftware." + info.name + ".installed", true);
        settings_->set("djsoftware." + info.name + ".path", info.installPath);
        settings_->set("djsoftware." + info.name + ".database", info.databasePath);
        spdlog::info("DJSoftwareAutoConfig: Configured {} at {}", info.name, info.installPath);
    }
    settings_->save();
}
} // namespace BeatMate::Services::Config
