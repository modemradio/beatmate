#include "WordPressLicenseClient.h"
#include "../network/HttpClient.h"
#include "../security/HardwareId.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace BeatMate::Services::WordPress {

using nlohmann::json;
using BeatMate::Services::Network::HttpClient;

static void persistUpdatesUntil(int64_t epoch)
{
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr)
        return;
    std::filesystem::path dir = std::filesystem::path(appData) / "BeatMate";
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr)
        return;
#if defined(__APPLE__)
    std::filesystem::path dir = std::filesystem::path(home)
                                / "Library" / "Application Support" / "BeatMate";
#else
    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "BeatMate";
#endif
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / "updates_until.txt", std::ios::trunc);
    if (out)
        out << epoch;
}
using BeatMate::Services::Network::HttpResponse;
using BeatMate::Services::Security::HardwareId;

WordPressLicenseClient::WordPressLicenseClient(std::shared_ptr<HttpClient> http,
                                               std::string baseUrl,
                                               std::string apiKey)
    : http_(std::move(http)), baseUrl_(std::move(baseUrl)), apiKey_(std::move(apiKey))
{
    while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

std::string WordPressLicenseClient::endpoint(const char* path) const
{
    return baseUrl_ + "/wp-json/beatmate/v1" + path;
}

// Parse a server-issued expiry timestamp.
static int64_t parseExpires(const std::string& iso)
{
    if (iso.empty()) return 0;

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

    auto build = [&](int Y, int Mo, int D, int H, int Mi, int S) -> int64_t {
        std::tm tm{};
        tm.tm_year = Y - 1900; tm.tm_mon  = Mo - 1; tm.tm_mday = D;
        tm.tm_hour = H;        tm.tm_min  = Mi;     tm.tm_sec  = S;
        tm.tm_isdst = 0;
#ifdef _WIN32
        return static_cast<int64_t>(_mkgmtime(&tm));
#else
        return static_cast<int64_t>(timegm(&tm));
#endif
    };

    // 1) ISO8601 strict — "%Y-%m-%dT%H:%M:%SZ".
    {
        char z = 0;
        if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
                        &y, &mo, &d, &h, &mi, &s, &z) >= 6) {
            // Accept either trailing 'Z', '+00:00' or nothing — all assumed UTC.
            return build(y, mo, d, h, mi, s);
        }
    }

    // 2) ISO8601 with fractional seconds — "%Y-%m-%dT%H:%M:%S.NNNZ".
    {
        if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.",
                        &y, &mo, &d, &h, &mi, &s) == 6) {
            return build(y, mo, d, h, mi, s);
        }
    }

    // 3) Legacy SQL "Y-m-d H:M:S" fallback.
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
                    &y, &mo, &d, &h, &mi, &s) == 6) {
        return build(y, mo, d, h, mi, s);
    }

    // 4) Date-only "Y-m-d" — treat as midnight UTC.
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2d", &y, &mo, &d) == 3) {
        return build(y, mo, d, 0, 0, 0);
    }

    return 0;
}

static std::map<std::string, std::string> baseHeaders(const std::string& apiKey)
{
    return {
        { "X-BeatMate-API-Key", apiKey },
        { "Content-Type",       "application/json" },
        { "Accept",             "application/json" },
    };
}

void WordPressLicenseClient::activate(const std::string& key,
                                      const std::string& email,
                                      const std::string& prenom,
                                      const std::string& nom,
                                      const std::string& machineName,
                                      std::function<void(ActivationResult)> cb)
{
    auto http = http_;
    auto url  = endpoint("/activate");
    auto hdrs = baseHeaders(apiKey_);

    HardwareId hw;
    auto hwid = hw.getHardwareId();
    auto mac  = hw.getMACAddress();

    json body = {
        {"key", key}, {"hwid", hwid}, {"mac", mac},
        {"machine_name", machineName},
        {"nom", nom}, {"prenom", prenom}, {"email", email}
    };
    auto payload = body.dump();

    std::thread([http, url, hdrs, payload, cb]() {
        ActivationResult r;
        auto resp = http->post(url, payload, hdrs);
        r.httpStatus = resp.statusCode;
        if (!resp.success || resp.body.empty()) {
            r.error = resp.error.empty() ? "Network error" : resp.error;
            if (cb) cb(r);
            return;
        }
        try {
            auto j = json::parse(resp.body);
            r.success = j.value("success", false);
            if (!r.success) {
                r.error = j.value("error", "Activation rejected");
            } else {
                r.type           = j.value("type", "");
                r.signature      = j.value("signature", "");
                r.expiresAtEpoch = parseExpires(j.value("expires_at", ""));
                if (r.signature.size() != 64) {
                    r.success = false;
                    r.error   = "Server returned no/short signature — refusing activation.";
                } else {
                    int64_t uu = 0;
                    if (j.contains("updates_until") && ! j["updates_until"].is_null())
                        uu = parseExpires(j.value("updates_until", ""));
                    persistUpdatesUntil(uu);
                }
            }
        } catch (const std::exception& e) {
            r.success = false;
            r.error   = std::string("Bad JSON from server: ") + e.what();
        }
        if (cb) cb(r);
    }).detach();
}

void WordPressLicenseClient::deactivate(const std::string& key,
                                        std::function<void(DeactivationResult)> cb)
{
    auto http = http_;
    auto url  = endpoint("/deactivate");
    auto hdrs = baseHeaders(apiKey_);

    HardwareId hw;
    json body = {{"key", key}, {"hwid", hw.getHardwareId()}, {"mac", hw.getMACAddress()}};
    auto payload = body.dump();

    std::thread([http, url, hdrs, payload, cb]() {
        DeactivationResult r;
        auto resp = http->post(url, payload, hdrs);
        r.httpStatus = resp.statusCode;
        if (!resp.success || resp.body.empty()) {
            r.error = resp.error.empty() ? "Network error" : resp.error;
            if (cb) cb(r);
            return;
        }
        try {
            auto j = json::parse(resp.body);
            r.success = j.value("success", false);
            if (!r.success) r.error = j.value("error", "Deactivation rejected");
        } catch (const std::exception& e) {
            r.error = std::string("Bad JSON: ") + e.what();
        }
        if (cb) cb(r);
    }).detach();
}

void WordPressLicenseClient::heartbeat(const std::string& key,
                                       const std::string& signature,
                                       std::function<void(HeartbeatResult)> cb)
{
    auto http = http_;
    auto url  = endpoint("/heartbeat");
    auto hdrs = baseHeaders(apiKey_);

    HardwareId hw;
    json body = {
        {"key", key},
        {"hwid", hw.getHardwareId()},
        {"mac", hw.getMACAddress()},
        {"app_version", appVersion_},
        {"signature", signature}
    };
    auto payload = body.dump();

    std::thread([http, url, hdrs, payload, cb]() {
        HeartbeatResult r;
        auto resp = http->post(url, payload, hdrs);
        r.httpStatus = resp.statusCode;
        if (!resp.success || resp.body.empty()) {
            r.action  = HeartbeatAction::NetworkFail;
            r.message = resp.error.empty() ? "Network error" : resp.error;
            if (cb) cb(r);
            return;
        }
        try {
            auto j = json::parse(resp.body);
            r.valid   = j.value("valid", false);
            auto act  = j.value("action", "none");
            r.message = j.value("message", "");
            if      (act == "revoke")     r.action = HeartbeatAction::Revoke;
            else if (act == "expire")     r.action = HeartbeatAction::Expire;
            else if (act == "deactivate") r.action = HeartbeatAction::Deactivate;
            else                          r.action = HeartbeatAction::None;
        } catch (const std::exception& e) {
            r.action  = HeartbeatAction::NetworkFail;
            r.message = std::string("Bad JSON: ") + e.what();
        }
        if (cb) cb(r);
    }).detach();
}

} // namespace BeatMate::Services::WordPress
