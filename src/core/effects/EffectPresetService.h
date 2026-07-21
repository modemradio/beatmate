#pragma once
#include "../dsp/DSPProcessor.h"
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Core {


struct EffectPresetParam {
    std::string name;
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
};

struct EffectPreset {
    std::string id;
    std::string name;
    std::string effectType;
    std::string category;
    std::string author;
    std::vector<EffectPresetParam> parameters;
    float wetDry = 1.0f;
    bool bypassed = false;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;

    nlohmann::json toJson() const;
    static EffectPreset fromJson(const nlohmann::json& j);
};

class EffectPresetService {
public:
    EffectPresetService();
    ~EffectPresetService() = default;

    bool loadPresetsFromDirectory(const std::string& directory);
    bool savePreset(const EffectPreset& preset, const std::string& directory = "");
    bool deletePreset(const std::string& presetId);

    std::vector<EffectPreset> getAllPresets() const;
    std::vector<EffectPreset> getPresetsForEffect(const std::string& effectType) const;
    std::vector<EffectPreset> getPresetsInCategory(const std::string& category) const;
    std::vector<EffectPreset> searchPresets(const std::string& query) const;
    const EffectPreset* getPresetById(const std::string& id) const;

    bool applyPreset(const std::string& presetId, DSPProcessor* effect);

    EffectPreset capturePreset(const DSPProcessor* effect, const std::string& name,
                               const std::string& category = "user");

    void initializeFactoryPresets();

    void toggleFavorite(const std::string& presetId);
    bool isFavorite(const std::string& presetId) const;
    std::vector<EffectPreset> getFavorites() const;

    void markRecentlyUsed(const std::string& presetId);
    std::vector<EffectPreset> getRecentPresets(int maxCount = 10) const;

    int getPresetCount() const;

private:
    std::string generateId() const;
    int64_t currentTimestamp() const;

    std::vector<EffectPreset> presets_;
    std::vector<std::string> favorites_;
    std::vector<std::string> recentIds_;
    std::string defaultDirectory_;
    mutable std::mutex mutex_;
};

}
