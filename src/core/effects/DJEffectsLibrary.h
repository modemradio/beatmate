#pragma once
#include "../dsp/DSPProcessor.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace BeatMate::Core {

struct EffectInfo {
    std::string name;
    std::string displayName;
    std::string category;       // "Filter", "Delay", "Modulation", "Dynamics", "Distortion", "Spatial", "Utility", "Creative"
    std::string description;
    int parameterCount = 0;
    bool isBeatSynced = false;
    bool isStereo = true;
};

class DJEffectsLibrary {
public:
    static std::unique_ptr<DSPProcessor> createEffect(const std::string& name);

    static std::vector<std::string> getAvailableEffects();
    static std::vector<EffectInfo> getEffectInfoList();
    static std::vector<std::string> getCategories();
    static std::vector<EffectInfo> getEffectsByCategory(const std::string& category);

    static EffectInfo getEffectInfo(const std::string& name);
    static bool effectExists(const std::string& name);

    struct EffectChainPreset {
        std::string name;
        std::string category;
        std::vector<std::string> effectNames;
        std::vector<float> wetDryValues;
    };
    static std::vector<EffectChainPreset> getFactoryChainPresets();
    static EffectChainPreset getChainPreset(const std::string& name);

    using EffectFactory = std::function<std::unique_ptr<DSPProcessor>()>;
    static void registerCustomEffect(const std::string& name, EffectFactory factory, const EffectInfo& info);
    static void unregisterCustomEffect(const std::string& name);

private:
    static std::map<std::string, EffectFactory>& customFactories();
    static std::map<std::string, EffectInfo>& customInfoMap();
};

} // namespace BeatMate::Core
