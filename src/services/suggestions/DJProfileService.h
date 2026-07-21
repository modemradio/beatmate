#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../../models/DJProfile.h"

namespace BeatMate::Services::Suggestions {

class SmartSuggestEngine;

class DJProfileService {
public:
    DJProfileService();

    std::vector<Models::DJProfile> listProfiles();
    std::optional<Models::DJProfile> loadProfile(const std::string& name);
    bool saveProfile(const Models::DJProfile& profile);
    bool deleteProfile(const std::string& name);

    void applyProfile(const Models::DJProfile& profile,
                      SmartSuggestEngine& engine);

    static std::vector<Models::DJProfile> seededProfiles();

private:
    std::string filePath_;
    std::mutex  lock_;

    std::vector<Models::DJProfile> readAll();
    bool                            writeAll(const std::vector<Models::DJProfile>& all);
    void                            ensureSeeded();
};

} // namespace BeatMate::Services::Suggestions
