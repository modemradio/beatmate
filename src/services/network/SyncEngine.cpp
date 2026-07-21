#include "SyncEngine.h"
#include "HttpClient.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Network {
bool SyncEngine::syncWithRemote(const std::string& endpoint) {
    HttpClient client;
    auto response = client.get(endpoint, {{"Authorization", "Bearer " + token_}});
    if (response.success) { spdlog::info("SyncEngine: Synced with {}", endpoint); return true; }
    spdlog::error("SyncEngine: Sync failed: {}", response.error);
    return false;
}
} // namespace BeatMate::Services::Network
