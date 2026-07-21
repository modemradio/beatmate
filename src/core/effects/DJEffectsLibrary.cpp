#include "DJEffectsLibrary.h"
#include "../dsp/ReverbProcessor.h"
#include "../dsp/DelayProcessor.h"
#include "../dsp/FlangerProcessor.h"
#include "../dsp/PhaserProcessor.h"
#include "../dsp/ChorusProcessor.h"
#include "../dsp/FilterProcessor.h"
#include "../dsp/BitCrusherProcessor.h"
#include "../dsp/DistortionProcessor.h"
#include "../dsp/CompressorProcessor.h"
#include "../dsp/EQProcessor.h"
#include "../dsp/VinylSimulator.h"
#include "../dsp/GainProcessor.h"
#include "../dsp/PanProcessor.h"
#include "../dsp/NoiseGate.h"
#include "../dsp/LimiterProcessor.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

std::map<std::string, DJEffectsLibrary::EffectFactory>& DJEffectsLibrary::customFactories() {
    static std::map<std::string, EffectFactory> factories;
    return factories;
}

std::map<std::string, EffectInfo>& DJEffectsLibrary::customInfoMap() {
    static std::map<std::string, EffectInfo> infoMap;
    return infoMap;
}

std::unique_ptr<DSPProcessor> DJEffectsLibrary::createEffect(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "reverb") return std::make_unique<ReverbProcessor>();
    if (lower == "delay" || lower == "echo") return std::make_unique<DelayProcessor>();
    if (lower == "flanger") return std::make_unique<FlangerProcessor>();
    if (lower == "phaser") return std::make_unique<PhaserProcessor>();
    if (lower == "chorus") return std::make_unique<ChorusProcessor>();
    if (lower == "filter" || lower == "lowpass" || lower == "highpass") return std::make_unique<FilterProcessor>();
    if (lower == "bitcrush" || lower == "bitcrusher") return std::make_unique<BitCrusherProcessor>();
    if (lower == "distortion" || lower == "overdrive") return std::make_unique<DistortionProcessor>();
    if (lower == "compressor") return std::make_unique<CompressorProcessor>();
    if (lower == "limiter") return std::make_unique<LimiterProcessor>();
    if (lower == "eq" || lower == "equalizer") return std::make_unique<EQProcessor>();
    if (lower == "vinyl" || lower == "vinylsim") return std::make_unique<VinylSimulator>();
    if (lower == "gain" || lower == "volume") return std::make_unique<GainProcessor>();
    if (lower == "pan" || lower == "panner") return std::make_unique<PanProcessor>();
    if (lower == "gate" || lower == "noisegate") return std::make_unique<NoiseGate>();

    auto& factories = customFactories();
    auto it = factories.find(lower);
    if (it != factories.end()) return it->second();

    spdlog::warn("DJEffectsLibrary: unknown effect '{}'", name);
    return nullptr;
}

std::vector<std::string> DJEffectsLibrary::getAvailableEffects() {
    std::vector<std::string> effects = {
        "Reverb", "Delay", "Flanger", "Phaser", "Chorus",
        "Filter", "BitCrusher", "Distortion", "Compressor", "Limiter",
        "EQ", "Vinyl", "Gain", "Pan", "Gate"
    };
    for (auto& [name, factory] : customFactories()) {
        effects.push_back(name);
    }
    return effects;
}

std::vector<EffectInfo> DJEffectsLibrary::getEffectInfoList() {
    std::vector<EffectInfo> list = {
        {"reverb",      "Reverb",       "Spatial",      "Space and ambience simulation",          3, false, true},
        {"delay",       "Delay",        "Delay",        "Echo/delay with feedback",                3, true,  true},
        {"flanger",     "Flanger",      "Modulation",   "Flanging modulation effect",              3, true,  true},
        {"phaser",      "Phaser",       "Modulation",   "Phase shifting modulation",               3, true,  true},
        {"chorus",      "Chorus",       "Modulation",   "Chorus width and detuning",               3, false, true},
        {"filter",      "Filter",       "Filter",       "Low/High/Band pass filter with resonance",3, false, true},
        {"bitcrusher",  "BitCrusher",   "Distortion",   "Bit depth and sample rate reduction",     2, false, true},
        {"distortion",  "Distortion",   "Distortion",   "Overdrive and distortion",                2, false, true},
        {"compressor",  "Compressor",   "Dynamics",     "Dynamic range compressor",                4, false, true},
        {"limiter",     "Limiter",      "Dynamics",     "Brick-wall limiter",                      2, false, true},
        {"eq",          "EQ",           "Filter",       "3-band parametric equalizer",              6, false, true},
        {"vinyl",       "Vinyl Sim",    "Creative",     "Vinyl crackle and wear simulation",       3, false, true},
        {"gain",        "Gain",         "Utility",      "Volume gain adjustment",                   1, false, true},
        {"pan",         "Pan",          "Utility",      "Stereo panning",                           1, false, true},
        {"gate",        "Noise Gate",   "Dynamics",     "Noise gate / expander",                    3, false, true},
    };
    for (auto& [name, info] : customInfoMap()) {
        list.push_back(info);
    }
    return list;
}

std::vector<std::string> DJEffectsLibrary::getCategories() {
    return {"Filter", "Delay", "Modulation", "Dynamics", "Distortion", "Spatial", "Creative", "Utility"};
}

std::vector<EffectInfo> DJEffectsLibrary::getEffectsByCategory(const std::string& category) {
    auto all = getEffectInfoList();
    std::vector<EffectInfo> result;
    std::string catLower = category;
    std::transform(catLower.begin(), catLower.end(), catLower.begin(), ::tolower);
    for (auto& info : all) {
        std::string infoCatLower = info.category;
        std::transform(infoCatLower.begin(), infoCatLower.end(), infoCatLower.begin(), ::tolower);
        if (infoCatLower == catLower) result.push_back(info);
    }
    return result;
}

EffectInfo DJEffectsLibrary::getEffectInfo(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto all = getEffectInfoList();
    for (auto& info : all) {
        std::string infoLower = info.name;
        std::transform(infoLower.begin(), infoLower.end(), infoLower.begin(), ::tolower);
        if (infoLower == lower) return info;
    }
    return {"unknown", "Unknown", "Utility", "Unknown effect", 0, false, false};
}

bool DJEffectsLibrary::effectExists(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto all = getEffectInfoList();
    for (auto& info : all) {
        std::string infoLower = info.name;
        std::transform(infoLower.begin(), infoLower.end(), infoLower.begin(), ::tolower);
        if (infoLower == lower) return true;
    }
    return false;
}

std::vector<DJEffectsLibrary::EffectChainPreset> DJEffectsLibrary::getFactoryChainPresets() {
    return {
        {"Club Standard",       "Performance", {"EQ", "Compressor", "Limiter"},              {1.0f, 1.0f, 1.0f}},
        {"Dub Send",            "Creative",    {"Delay", "Reverb", "Filter"},                 {0.4f, 0.5f, 0.8f}},
        {"Lo-Fi Vibes",         "Creative",    {"Vinyl", "BitCrusher", "Filter"},             {0.5f, 0.3f, 0.7f}},
        {"Build-Up FX",         "Transition",  {"Filter", "Flanger", "Reverb"},               {1.0f, 0.5f, 0.3f}},
        {"Breakdown",           "Transition",  {"Filter", "Delay", "Reverb"},                 {1.0f, 0.5f, 0.6f}},
        {"Modulation Stack",    "Creative",    {"Chorus", "Phaser", "Flanger"},               {0.4f, 0.4f, 0.4f}},
        {"Radio Ready",         "Utility",     {"EQ", "Compressor", "Limiter", "Gain"},       {1.0f, 1.0f, 1.0f, 1.0f}},
        {"Vocal FX",            "Performance", {"Reverb", "Delay", "Compressor"},             {0.3f, 0.25f, 1.0f}},
        {"Heavy Drop",          "Transition",  {"Distortion", "Filter", "Compressor"},        {0.6f, 1.0f, 1.0f}},
        {"Ambient Wash",        "Creative",    {"Reverb", "Chorus", "Delay"},                 {0.7f, 0.5f, 0.4f}},
    };
}

DJEffectsLibrary::EffectChainPreset DJEffectsLibrary::getChainPreset(const std::string& name) {
    auto presets = getFactoryChainPresets();
    for (auto& p : presets) {
        if (p.name == name) return p;
    }
    return {"Default", "Utility", {}, {}};
}

void DJEffectsLibrary::registerCustomEffect(const std::string& name, EffectFactory factory, const EffectInfo& info) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    customFactories()[lower] = std::move(factory);
    customInfoMap()[lower] = info;
    spdlog::info("DJEffectsLibrary: registered custom effect '{}'", name);
}

void DJEffectsLibrary::unregisterCustomEffect(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    customFactories().erase(lower);
    customInfoMap().erase(lower);
}

} // namespace BeatMate::Core
