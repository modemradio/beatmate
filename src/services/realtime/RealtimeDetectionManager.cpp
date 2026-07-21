#include "RealtimeDetectionManager.h"
#include "RekordboxMonitor.h"
#include "VirtualDJMonitor.h"
#include "RealtimeCoordinator.h"
#include "../djsoftware/rekordbox/RekordboxEnvironment.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <filesystem>

namespace BeatMate::Services::Realtime {

RealtimeDetectionManager::RealtimeDetectionManager() {}
RealtimeDetectionManager::~RealtimeDetectionManager() { stop(); }

void RealtimeDetectionManager::start() {
    if (running_) return;
    running_ = true;
    spdlog::info("RealtimeDetectionManager: Starting all monitors...");

    {
        BeatMate::Services::Rekordbox::RekordboxEnvironment env;
        auto dbPath = env.findDatabasePath();
        if (!dbPath.empty()) {
            rekordboxMonitor_ = std::make_unique<RekordboxMonitor>();
            rekordboxMonitor_->setDatabasePath(dbPath);
            rekordboxMonitor_->setTrackChangedCallback(
                [this](const std::string& title, const std::string& artist, double bpm) {
                    spdlog::info("RealtimeDetectionManager: Rekordbox track change: '{}' - '{}'", title, artist);
                    if (trackDetectedCallback_)
                        trackDetectedCallback_(title, artist);
                });
            rekordboxMonitor_->start(2000);
            spdlog::info("RealtimeDetectionManager: Rekordbox monitor started (db: {})", dbPath);
        } else {
            spdlog::info("RealtimeDetectionManager: Rekordbox database not found, monitor not started");
        }
    }

    {
        std::string vdjPath;
#ifdef _WIN32
        if (auto* docs = std::getenv("USERPROFILE"))
            vdjPath = std::string(docs) + "/Documents/VirtualDJ";
#endif
        if (!vdjPath.empty() && std::filesystem::exists(vdjPath)) {
            virtualDJMonitor_ = std::make_unique<VirtualDJMonitor>();
            virtualDJMonitor_->setTrackChangedCallback(
                [this](int deck, const std::string& title, const std::string& artist) {
                    spdlog::info("RealtimeDetectionManager: VDJ deck {} track change: '{}' - '{}'", deck, title, artist);
                    if (trackDetectedCallback_)
                        trackDetectedCallback_(title, artist);
                });
            virtualDJMonitor_->start("127.0.0.1", 8080);
            spdlog::info("RealtimeDetectionManager: VirtualDJ monitor started");
        } else {
            spdlog::info("RealtimeDetectionManager: VirtualDJ not detected, monitor not started");
        }
    }

    coordinator_ = std::make_unique<RealtimeCoordinator>();
    coordinator_->start(5000);

    spdlog::info("RealtimeDetectionManager: All monitors started");
}

void RealtimeDetectionManager::stop() {
    if (!running_) return;
    running_ = false;

    if (rekordboxMonitor_) {
        rekordboxMonitor_->stop();
        rekordboxMonitor_.reset();
    }
    if (virtualDJMonitor_) {
        virtualDJMonitor_->stop();
        virtualDJMonitor_.reset();
    }
    if (coordinator_) {
        coordinator_->stop();
        coordinator_.reset();
    }

    spdlog::info("RealtimeDetectionManager: Stopped all monitors");
}

}
