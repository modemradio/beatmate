#include "SeratoService.h"
#include "SeratoDatabase.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Serato {

SeratoService::SeratoService()
    : database_(std::make_unique<SeratoDatabase>()) {
}

SeratoService::~SeratoService() = default;

bool SeratoService::initialize() {
    if (database_->open()) {
        initialized_ = true;
        spdlog::info("SeratoService: Initialized");
        return true;
    }
    spdlog::warn("SeratoService: Serato database not found");
    return false;
}

bool SeratoService::isAvailable() const {
    return initialized_;
}

std::vector<Models::SeratoTrack> SeratoService::readDatabase() {
    if (!initialized_) return {};
    return database_->readAllTracks();
}

std::vector<std::string> SeratoService::readCrates() {
    if (!initialized_) return {};
    return database_->readCrateNames();
}

std::vector<Models::SeratoTrack> SeratoService::getTracksInCrate(const std::string& crateName) {
    if (!initialized_) return {};
    return database_->readCrateTracks(crateName);
}

} // namespace BeatMate::Services::Serato
