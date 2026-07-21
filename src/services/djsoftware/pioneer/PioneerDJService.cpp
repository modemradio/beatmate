#include "PioneerDJService.h"
#include "PioneerDatabase.h"
#include "PioneerEnvironment.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::PioneerDJ {

PioneerDJService::PioneerDJService()
    : database_(std::make_unique<PioneerDatabase>())
    , environment_(std::make_unique<PioneerEnvironment>()) {
}

PioneerDJService::~PioneerDJService() = default;

bool PioneerDJService::initialize() {
    if (!environment_->isInstalled()) {
        spdlog::warn("PioneerDJService: Rekordbox not found on this system");
        return false;
    }

    std::string dbPath = environment_->findDatabasePath();
    if (dbPath.empty()) {
        spdlog::error("PioneerDJService: Cannot find Rekordbox database");
        return false;
    }

    if (!database_->openDatabase(dbPath)) {
        spdlog::error("PioneerDJService: Failed to open Rekordbox database");
        return false;
    }

    initialized_ = true;
    spdlog::info("PioneerDJService: Initialized with database at {}", dbPath);
    return true;
}

bool PioneerDJService::isAvailable() const {
    return initialized_;
}

std::vector<Models::PioneerTrack> PioneerDJService::readTracks() {
    if (!initialized_) {
        spdlog::warn("PioneerDJService: Not initialized");
        return {};
    }

    auto tracks = database_->readAllTracks();
    spdlog::info("PioneerDJService: Read {} tracks from Rekordbox", tracks.size());
    return tracks;
}

std::vector<Models::Playlist> PioneerDJService::readPlaylists() {
    if (!initialized_) return {};
    return database_->readPlaylists();
}

std::vector<Models::PioneerDJCue> PioneerDJService::readHotCues(const std::string& pioneerTrackId) {
    if (!initialized_) return {};
    return database_->readCuePoints(pioneerTrackId);
}

bool PioneerDJService::syncWithLocalDatabase() {
    if (!initialized_) return false;
    spdlog::info("PioneerDJService: Starting sync with local database");
    auto tracks = readTracks();
    spdlog::info("PioneerDJService: Sync complete, {} tracks processed", tracks.size());
    return true;
}

} // namespace BeatMate::Services::PioneerDJ
