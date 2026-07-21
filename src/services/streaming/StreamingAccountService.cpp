#include "StreamingAccountService.h"
#include "StreamingServiceBase.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

#include <chrono>
#include <fstream>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

StreamingAccountService::StreamingAccountService() {
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate");
    appDir.createDirectory();
    defaultPath_ = appDir.getChildFile("streaming_accounts.json").getFullPathName().toStdString();
}

bool StreamingAccountService::connectAccount(Models::StreamingServiceType service,
                                               const std::string& clientId,
                                               const std::string& redirectUri,
                                               const std::vector<std::string>& scopes) {
    auto svc = getService(service);
    if (!svc) {
        spdlog::error("StreamingAccountService: No service registered for {}",
                      serviceTypeToString(service));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        accounts_[service].serviceType = service;
        accounts_[service].status = AccountStatus::Connecting;
    }
    notifyChange(service, AccountStatus::Connecting);

    bool success = svc->authenticate(clientId, redirectUri, scopes);

    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        if (success) {
            accounts_[service].status = AccountStatus::Connected;
            accounts_[service].connectedAt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto token = svc->getToken();
            accounts_[service].accessToken = token.accessToken;
            accounts_[service].refreshToken = token.refreshToken;
            accounts_[service].tokenExpiresAt = token.expiresAt;
        } else {
            accounts_[service].status = AccountStatus::Error;
            accounts_[service].errorMessage = "Authentication failed";
        }
    }

    notifyChange(service, success ? AccountStatus::Connected : AccountStatus::Error);
    spdlog::info("StreamingAccountService: {} account {}", serviceTypeToString(service),
                 success ? "connected" : "failed to connect");

    if (success) saveAccounts();
    return success;
}

bool StreamingAccountService::disconnectAccount(Models::StreamingServiceType service) {
    auto svc = getService(service);
    if (svc) svc->logout();

    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        auto it = accounts_.find(service);
        if (it != accounts_.end()) {
            it->second.status = AccountStatus::Disconnected;
            it->second.accessToken.clear();
            it->second.refreshToken.clear();
        }
    }

    notifyChange(service, AccountStatus::Disconnected);
    spdlog::info("StreamingAccountService: {} account disconnected", serviceTypeToString(service));
    saveAccounts();
    return true;
}

bool StreamingAccountService::reconnectAccount(Models::StreamingServiceType service) {
    return refreshAccountToken(service);
}

bool StreamingAccountService::refreshAccountToken(Models::StreamingServiceType service) {
    auto svc = getService(service);
    if (!svc) return false;

    bool success = svc->refreshAccessToken();
    {
        std::lock_guard<std::mutex> lock(accountMutex_);
        if (success) {
            auto token = svc->getToken();
            accounts_[service].accessToken = token.accessToken;
            accounts_[service].refreshToken = token.refreshToken;
            accounts_[service].tokenExpiresAt = token.expiresAt;
            accounts_[service].status = AccountStatus::Connected;
        } else {
            accounts_[service].status = AccountStatus::TokenExpired;
        }
    }

    notifyChange(service, success ? AccountStatus::Connected : AccountStatus::TokenExpired);
    if (success) saveAccounts();
    return success;
}

StreamingAccount StreamingAccountService::getAccount(Models::StreamingServiceType service) const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    auto it = accounts_.find(service);
    return (it != accounts_.end()) ? it->second : StreamingAccount{};
}

std::vector<StreamingAccount> StreamingAccountService::getAllAccounts() const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    std::vector<StreamingAccount> result;
    for (const auto& [type, account] : accounts_) result.push_back(account);
    return result;
}

std::vector<StreamingAccount> StreamingAccountService::getConnectedAccounts() const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    std::vector<StreamingAccount> result;
    for (const auto& [type, account] : accounts_) {
        if (account.status == AccountStatus::Connected) result.push_back(account);
    }
    return result;
}

bool StreamingAccountService::isConnected(Models::StreamingServiceType service) const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    auto it = accounts_.find(service);
    return it != accounts_.end() && it->second.status == AccountStatus::Connected;
}

AccountStatus StreamingAccountService::getAccountStatus(Models::StreamingServiceType service) const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    auto it = accounts_.find(service);
    return (it != accounts_.end()) ? it->second.status : AccountStatus::Disconnected;
}

int StreamingAccountService::getConnectedAccountCount() const {
    return static_cast<int>(getConnectedAccounts().size());
}

void StreamingAccountService::registerService(Models::StreamingServiceType type,
                                                std::shared_ptr<StreamingServiceBase> service) {
    std::lock_guard<std::mutex> lock(accountMutex_);
    services_[type] = std::move(service);
    spdlog::debug("StreamingAccountService: Registered service {}", serviceTypeToString(type));
}

std::shared_ptr<StreamingServiceBase> StreamingAccountService::getService(
    Models::StreamingServiceType type) const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    auto it = services_.find(type);
    return (it != services_.end()) ? it->second : nullptr;
}

void StreamingAccountService::setPrimaryAccount(Models::StreamingServiceType service) {
    primaryAccount_ = service;
    spdlog::info("StreamingAccountService: Primary account set to {}", serviceTypeToString(service));
}

bool StreamingAccountService::saveAccounts(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(accountMutex_);
    std::string path = filePath.empty() ? defaultPath_ : filePath;

    json j = json::object();
    j["primaryAccount"] = serviceTypeToString(primaryAccount_);
    j["accounts"] = json::array();

    for (const auto& [type, acct] : accounts_) {
        j["accounts"].push_back({
            {"serviceType", serviceTypeToString(type)},
            {"accountId", acct.accountId},
            {"displayName", acct.displayName},
            {"email", acct.email},
            {"tier", acct.tier},
            {"country", acct.country},
            {"avatarUrl", acct.avatarUrl},
            {"refreshToken", acct.refreshToken},
            {"connectedAt", acct.connectedAt},
            {"lastSyncAt", acct.lastSyncAt}
        });
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    return true;
}

bool StreamingAccountService::loadAccounts(const std::string& filePath) {
    std::string path = filePath.empty() ? defaultPath_ : filePath;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;

    try {
        auto j = json::parse(ifs);
        std::lock_guard<std::mutex> lock(accountMutex_);

        if (j.contains("primaryAccount")) {
            primaryAccount_ = stringToServiceType(j["primaryAccount"].get<std::string>());
        }

        if (j.contains("accounts")) {
            for (const auto& item : j["accounts"]) {
                auto type = stringToServiceType(item.value("serviceType", "Spotify"));
                StreamingAccount acct;
                acct.serviceType = type;
                acct.accountId = item.value("accountId", "");
                acct.displayName = item.value("displayName", "");
                acct.email = item.value("email", "");
                acct.tier = item.value("tier", "");
                acct.country = item.value("country", "");
                acct.avatarUrl = item.value("avatarUrl", "");
                acct.refreshToken = item.value("refreshToken", "");
                acct.connectedAt = item.value("connectedAt", static_cast<int64_t>(0));
                acct.lastSyncAt = item.value("lastSyncAt", static_cast<int64_t>(0));
                acct.status = acct.refreshToken.empty() ? AccountStatus::Disconnected
                                                         : AccountStatus::TokenExpired;
                accounts_[type] = acct;
            }
        }

        spdlog::info("StreamingAccountService: Loaded {} accounts", accounts_.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("StreamingAccountService: Load error: {}", e.what());
        return false;
    }
}

void StreamingAccountService::registerChangeCallback(AccountChangeCallback callback) {
    changeCallbacks_.push_back(std::move(callback));
}

void StreamingAccountService::notifyChange(Models::StreamingServiceType service, AccountStatus status) {
    for (const auto& cb : changeCallbacks_) {
        cb(service, status);
    }
}

std::string StreamingAccountService::serviceTypeToString(Models::StreamingServiceType type) {
    switch (type) {
        case Models::StreamingServiceType::Spotify:      return "Spotify";
        case Models::StreamingServiceType::AppleMusic:   return "AppleMusic";
        case Models::StreamingServiceType::SoundCloud:   return "SoundCloud";
        case Models::StreamingServiceType::Tidal:        return "Tidal";
        case Models::StreamingServiceType::YouTubeMusic: return "YouTubeMusic";
        case Models::StreamingServiceType::AmazonMusic:  return "AmazonMusic";
        case Models::StreamingServiceType::Beatport:     return "Beatport";
        case Models::StreamingServiceType::Deezer:       return "Deezer";
        case Models::StreamingServiceType::Bandcamp:     return "Bandcamp";
    }
    return "Unknown";
}

Models::StreamingServiceType StreamingAccountService::stringToServiceType(const std::string& name) {
    if (name == "Spotify") return Models::StreamingServiceType::Spotify;
    if (name == "AppleMusic") return Models::StreamingServiceType::AppleMusic;
    if (name == "SoundCloud") return Models::StreamingServiceType::SoundCloud;
    if (name == "Tidal") return Models::StreamingServiceType::Tidal;
    if (name == "YouTubeMusic") return Models::StreamingServiceType::YouTubeMusic;
    if (name == "AmazonMusic") return Models::StreamingServiceType::AmazonMusic;
    if (name == "Beatport") return Models::StreamingServiceType::Beatport;
    if (name == "Deezer") return Models::StreamingServiceType::Deezer;
    if (name == "Bandcamp") return Models::StreamingServiceType::Bandcamp;
    return Models::StreamingServiceType::Spotify;
}

std::vector<Models::StreamingServiceType> StreamingAccountService::getSupportedServices() {
    return {
        Models::StreamingServiceType::Spotify,
        Models::StreamingServiceType::AppleMusic,
        Models::StreamingServiceType::SoundCloud,
        Models::StreamingServiceType::Tidal,
        Models::StreamingServiceType::YouTubeMusic,
        Models::StreamingServiceType::AmazonMusic,
        Models::StreamingServiceType::Beatport,
        Models::StreamingServiceType::Deezer,
        Models::StreamingServiceType::Bandcamp
    };
}

} // namespace BeatMate::Services::Streaming
