#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../models/StreamingTrack.h"

namespace BeatMate::Services::Streaming {

class StreamingServiceBase;

enum class AccountStatus {
    Disconnected,
    Connecting,
    Connected,
    TokenExpired,
    Error
};

struct StreamingAccount {
    Models::StreamingServiceType serviceType;
    std::string accountId;
    std::string displayName;
    std::string email;
    std::string tier;            // "free", "premium", "hifi"
    std::string country;
    std::string avatarUrl;
    AccountStatus status = AccountStatus::Disconnected;
    std::string accessToken;
    std::string refreshToken;
    int64_t tokenExpiresAt = 0;
    int64_t connectedAt = 0;
    int64_t lastSyncAt = 0;
    std::string errorMessage;
};

using AccountChangeCallback = std::function<void(Models::StreamingServiceType, AccountStatus)>;

class StreamingAccountService {
public:
    StreamingAccountService();
    ~StreamingAccountService() = default;

    bool connectAccount(Models::StreamingServiceType service,
                        const std::string& clientId,
                        const std::string& redirectUri = "",
                        const std::vector<std::string>& scopes = {});
    bool disconnectAccount(Models::StreamingServiceType service);
    bool reconnectAccount(Models::StreamingServiceType service);
    bool refreshAccountToken(Models::StreamingServiceType service);

    StreamingAccount getAccount(Models::StreamingServiceType service) const;
    std::vector<StreamingAccount> getAllAccounts() const;
    std::vector<StreamingAccount> getConnectedAccounts() const;
    bool isConnected(Models::StreamingServiceType service) const;
    AccountStatus getAccountStatus(Models::StreamingServiceType service) const;
    int getConnectedAccountCount() const;

    void registerService(Models::StreamingServiceType type,
                         std::shared_ptr<StreamingServiceBase> service);
    std::shared_ptr<StreamingServiceBase> getService(Models::StreamingServiceType type) const;

    void setPrimaryAccount(Models::StreamingServiceType service);
    Models::StreamingServiceType getPrimaryAccount() const { return primaryAccount_; }

    bool saveAccounts(const std::string& filePath = "") const;
    bool loadAccounts(const std::string& filePath = "");

    void registerChangeCallback(AccountChangeCallback callback);

    static std::string serviceTypeToString(Models::StreamingServiceType type);
    static Models::StreamingServiceType stringToServiceType(const std::string& name);
    static std::vector<Models::StreamingServiceType> getSupportedServices();

private:
    void notifyChange(Models::StreamingServiceType service, AccountStatus status);

    mutable std::mutex accountMutex_;
    std::map<Models::StreamingServiceType, StreamingAccount> accounts_;
    std::map<Models::StreamingServiceType, std::shared_ptr<StreamingServiceBase>> services_;

    Models::StreamingServiceType primaryAccount_ = Models::StreamingServiceType::Spotify;
    std::vector<AccountChangeCallback> changeCallbacks_;
    std::string defaultPath_;
};

} // namespace BeatMate::Services::Streaming
