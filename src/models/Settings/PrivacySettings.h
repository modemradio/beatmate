#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct PrivacySettings {
    bool enableTelemetry = false;
    bool shareAnonymousData = false;
    bool shareCrashReports = true;
    bool shareUsageStatistics = false;

    bool keepPlayHistory = true;
    bool keepSearchHistory = true;
    bool keepBrowseHistory = true;
    int maxPlayHistory = 10000;
    int maxSearchHistory = 500;

    int64_t lastHistoryCleared = 0;

    bool collectFeatureUsage = false;
    bool collectPerformanceData = false;
    bool collectErrorLogs = true;

    bool allowThirdPartyAnalytics = false;
    bool allowThirdPartyCookies = false;
    bool allowStreamingDataSharing = false;

    bool encryptDatabase = false;
    bool encryptCache = false;
    bool secureDeletion = false;

    bool useProxy = false;
    std::string proxyHost;
    int proxyPort = 8080;
    std::string proxyUsername;
    std::string proxyPassword;
    bool verifySSL = true;

    bool rememberCredentials = true;
    bool autoLogin = true;
    int sessionTimeoutMinutes = 0;

    PrivacySettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PrivacySettings,
        enableTelemetry, shareAnonymousData, shareCrashReports, shareUsageStatistics,
        keepPlayHistory, keepSearchHistory, keepBrowseHistory,
        maxPlayHistory, maxSearchHistory,
        lastHistoryCleared,
        collectFeatureUsage, collectPerformanceData, collectErrorLogs,
        allowThirdPartyAnalytics, allowThirdPartyCookies, allowStreamingDataSharing,
        encryptDatabase, encryptCache, secureDeletion,
        useProxy, proxyHost, proxyPort, proxyUsername, proxyPassword, verifySSL,
        rememberCredentials, autoLogin, sessionTimeoutMinutes
    )
};

}
