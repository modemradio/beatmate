#include "LicenseHeartbeatService.h"
#include "WordPressLicenseClient.h"
#include "../security/LicenseService.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace BeatMate::Services::WordPress {

using BeatMate::Services::Security::LicenseService;
using nlohmann::json;

namespace {
// Persisted state path: %APPDATA%\BeatMate\license_state.json
static std::filesystem::path stateFilePath()
{
    // [COPIE MAC] dossier de données par plateforme (Windows inchangé).
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    std::filesystem::path base = appdata ? std::filesystem::path(appdata)
                                         : std::filesystem::path(".");
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    std::filesystem::path base = (home ? std::filesystem::path(home)
                                       : std::filesystem::path("."))
                                 / "Library" / "Application Support";
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = (home ? std::filesystem::path(home)
                                       : std::filesystem::path("."))
                                 / ".config";
#endif
    auto dir = base / "BeatMate";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / "license_state.json";
}
} // namespace

LicenseHeartbeatService::LicenseHeartbeatService(std::shared_ptr<WordPressLicenseClient> client,
                                                 LicenseService& licenseService)
    : client_(std::move(client)), licenseService_(licenseService)
{
    // Restore prior counter — without this, a reboot resets the offline-grace
    loadPersistedState();
}

void LicenseHeartbeatService::loadPersistedState()
{
    try {
        auto p = stateFilePath();
        if (!std::filesystem::exists(p)) return;
        std::ifstream f(p);
        if (!f.is_open()) return;
        json j; f >> j;
        consecutiveFailures_ = j.value("consecutive_failures", 0);
        spdlog::info("LicenseHeartbeat: restored consecutiveFailures={}",
                     consecutiveFailures_.load());
    } catch (const std::exception& e) {
        spdlog::warn("LicenseHeartbeat: failed to load state: {}", e.what());
    }
}

void LicenseHeartbeatService::persistState() const
{
    try {
        json j;
        j["consecutive_failures"] = consecutiveFailures_.load();
        auto p = stateFilePath();
        std::ofstream f(p, std::ios::trunc);
        if (f.is_open()) f << j.dump();
    } catch (const std::exception& e) {
        spdlog::warn("LicenseHeartbeat: failed to persist state: {}", e.what());
    }
}

void LicenseHeartbeatService::start(int firstDelayMs, int periodMs, int maxConsecutiveOfflineFailures)
{
    periodMs_ = periodMs;
    maxOfflineFailures_ = maxConsecutiveOfflineFailures;
    firstTickDone_ = false;
    running_ = true;
    // First tick after firstDelayMs, then we re-arm with periodMs in timerCallback.
    startTimer(firstDelayMs);
}

void LicenseHeartbeatService::stop()
{
    running_ = false;
    stopTimer();
}

void LicenseHeartbeatService::timerCallback()
{
    if (!firstTickDone_) {
        firstTickDone_ = true;
        startTimer(periodMs_);  // re-arm at the configured period
    }
    runOnce();
}

void LicenseHeartbeatService::runOnce()
{
    if (!running_) return;
    if (!licenseService_.isActivated()) return;       // skip if not licensed
    auto key = licenseService_.getKey();
    auto sig = licenseService_.getSignature();
    if (key.empty() || sig.empty()) return;           // skip if state inconsistent

    client_->heartbeat(key, sig, [this](HeartbeatResult r) {
        switch (r.action) {
            case HeartbeatAction::None:
                consecutiveFailures_ = 0;
                persistState();
                break;

            case HeartbeatAction::Revoke:
            case HeartbeatAction::Deactivate:
                licenseService_.forceLocalRevocation("server: " + r.message);
                consecutiveFailures_ = 0;
                persistState();
                break;

            case HeartbeatAction::Expire:
                licenseService_.forceLocalRevocation("expired");
                consecutiveFailures_ = 0;
                persistState();
                break;

            case HeartbeatAction::NetworkFail: {
                int n = consecutiveFailures_.fetch_add(1) + 1;
                persistState();
                spdlog::info("Heartbeat: network failure #{} ({})", n, r.message);
                if (n >= maxOfflineFailures_) {
                    // Tolerated grace window exhausted.
                    licenseService_.forceLocalRevocation(
                        "offline grace exhausted (" + std::to_string(n) + " consecutive failures)");
                }
                break;
            }
        }
    });
}

} // namespace BeatMate::Services::WordPress
