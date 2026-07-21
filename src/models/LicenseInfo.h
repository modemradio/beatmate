#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class LicenseType : int {
    Trial = 0,
    Personal = 1,
    Professional = 2,
    Family = 3,
    Enterprise = 4
};

NLOHMANN_JSON_SERIALIZE_ENUM(LicenseType, {
    { LicenseType::Trial, "Trial" },
    { LicenseType::Personal, "Personal" },
    { LicenseType::Professional, "Professional" },
    { LicenseType::Family, "Family" },
    { LicenseType::Enterprise, "Enterprise" }
})

struct LicenseInfo {
    std::string key;                // license key string
    LicenseType type = LicenseType::Trial;
    int64_t activatedAt = 0;        // unix timestamp
    int64_t expiresAt = 0;          // unix timestamp (0 = perpetual)
    std::string machineId;          // current machine identifier
    int maxMachines = 1;            // max concurrent activations
    bool isValid = false;

    std::string email;
    std::string name;
    std::string organization;

    bool hasStreamingAccess = false;
    bool hasStemSeparation = false;
    bool hasCloudSync = false;
    bool hasAdvancedAnalysis = false;
    bool hasExportFeatures = false;
    bool hasMIDIControl = false;
    int maxTracks = -1;             // -1 = unlimited

    bool isSubscription = false;
    std::string subscriptionPlan;   // "monthly", "yearly"
    int64_t nextBillingDate = 0;
    std::string paymentMethod;

    struct Activation {
        std::string machineId;
        std::string machineName;
        std::string os;
        int64_t activatedAt = 0;
        bool isActive = true;

        Activation() = default;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Activation,
            machineId, machineName, os, activatedAt, isActive
        )
    };

    std::vector<Activation> activations;

    LicenseInfo() = default;

    LicenseInfo(const std::string& key, LicenseType type)
        : key(key), type(type) {}

    // Check if license is expired
    [[nodiscard]] bool isExpired() const {
        if (expiresAt == 0) return false; // perpetual
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        return nowSec > expiresAt;
    }

    [[nodiscard]] int daysRemaining() const {
        if (expiresAt == 0) return -1; // perpetual
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        int64_t remaining = expiresAt - nowSec;
        if (remaining <= 0) return 0;
        return static_cast<int>(remaining / 86400);
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LicenseInfo,
        key, type, activatedAt, expiresAt, machineId, maxMachines, isValid,
        email, name, organization,
        hasStreamingAccess, hasStemSeparation, hasCloudSync,
        hasAdvancedAnalysis, hasExportFeatures, hasMIDIControl, maxTracks,
        isSubscription, subscriptionPlan, nextBillingDate, paymentMethod,
        activations
    )
};

} // namespace BeatMate::Models
