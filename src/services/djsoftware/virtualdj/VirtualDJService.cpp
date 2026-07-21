#include "VirtualDJService.h"
#include "VirtualDJDatabase.h"
#include "VirtualDJRemote.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::VirtualDJ {

VirtualDJService::VirtualDJService()
    : database_(std::make_unique<VirtualDJDatabase>())
    , remote_(std::make_unique<VirtualDJRemote>()) {
}

VirtualDJService::~VirtualDJService() = default;

bool VirtualDJService::initialize() {
    if (database_->open()) {
        initialized_ = true;
        spdlog::info("VirtualDJService: Initialized");
        return true;
    }

    spdlog::warn("VirtualDJService: VirtualDJ database not found");
    return false;
}

bool VirtualDJService::isAvailable() const {
    return initialized_;
}

std::vector<Models::VirtualDJTrack> VirtualDJService::readDatabase() {
    if (!initialized_) return {};
    return database_->readAllTracks();
}

bool VirtualDJService::connectRemote(const std::string& ip, int port) {
    return remote_->connect(ip, port);
}

bool VirtualDJService::isRemoteConnected() const {
    return remote_->isConnected();
}

} // namespace BeatMate::Services::VirtualDJ
