#include "EffectPresetService.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {


nlohmann::json EffectPreset::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["effectType"] = effectType;
    j["category"] = category;
    j["author"] = author;
    j["wetDry"] = wetDry;
    j["bypassed"] = bypassed;
    j["createdAt"] = createdAt;
    j["modifiedAt"] = modifiedAt;
    nlohmann::json params = nlohmann::json::array();
    for (auto& p : parameters) {
        params.push_back({{"name", p.name}, {"value", p.value},
                          {"min", p.minValue}, {"max", p.maxValue}});
    }
    j["parameters"] = params;
    return j;
}

EffectPreset EffectPreset::fromJson(const nlohmann::json& j) {
    EffectPreset p;
    p.id = j.value("id", "");
    p.name = j.value("name", "Untitled");
    p.effectType = j.value("effectType", "");
    p.category = j.value("category", "user");
    p.author = j.value("author", "");
    p.wetDry = j.value("wetDry", 1.0f);
    p.bypassed = j.value("bypassed", false);
    p.createdAt = j.value("createdAt", (int64_t)0);
    p.modifiedAt = j.value("modifiedAt", (int64_t)0);
    if (j.contains("parameters") && j["parameters"].is_array()) {
        for (auto& pj : j["parameters"]) {
            EffectPresetParam ep;
            ep.name = pj.value("name", "");
            ep.value = pj.value("value", 0.0f);
            ep.minValue = pj.value("min", 0.0f);
            ep.maxValue = pj.value("max", 1.0f);
            p.parameters.push_back(ep);
        }
    }
    return p;
}


EffectPresetService::EffectPresetService() {
    initializeFactoryPresets();
}

bool EffectPresetService::loadPresetsFromDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    defaultDirectory_ = directory;
    namespace fs = std::filesystem;
    if (!fs::is_directory(directory)) return false;

    for (auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().extension() != ".bmpreset") continue;
        try {
            std::ifstream ifs(entry.path());
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            auto json = nlohmann::json::parse(content);
            auto preset = EffectPreset::fromJson(json);
            if (!preset.id.empty()) {
                auto it = std::find_if(presets_.begin(), presets_.end(),
                    [&](const EffectPreset& ep) { return ep.id == preset.id; });
                if (it == presets_.end()) presets_.push_back(std::move(preset));
            }
        } catch (const std::exception& e) {
            spdlog::warn("EffectPresetService: failed to load '{}': {}", entry.path().string(), e.what());
        }
    }
    spdlog::info("EffectPresetService: loaded {} presets from '{}'", presets_.size(), directory);
    return true;
}

bool EffectPresetService::savePreset(const EffectPreset& preset, const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    namespace fs = std::filesystem;
    std::string dir = directory.empty() ? defaultDirectory_ : directory;
    if (dir.empty()) dir = ".";

    if (!fs::is_directory(dir)) fs::create_directories(dir);

    std::string outPath = dir + "/" + preset.id + ".bmpreset";
    auto json = preset.toJson();
    std::ofstream ofs(outPath);
    if (!ofs) return false;
    ofs << json.dump(2);

    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const EffectPreset& ep) { return ep.id == preset.id; });
    if (it != presets_.end()) *it = preset;
    else presets_.push_back(preset);

    spdlog::info("EffectPresetService: saved preset '{}' ({})", preset.name, preset.id);
    return true;
}

bool EffectPresetService::deletePreset(const std::string& presetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const EffectPreset& ep) { return ep.id == presetId; });
    if (it == presets_.end()) return false;
    if (it->category == "factory") { spdlog::warn("Cannot delete factory preset"); return false; }
    presets_.erase(it);

    if (!defaultDirectory_.empty()) {
        namespace fs = std::filesystem;
        fs::path fp = fs::path(defaultDirectory_) / (presetId + ".bmpreset");
        if (fs::exists(fp)) fs::remove(fp);
    }
    return true;
}

std::vector<EffectPreset> EffectPresetService::getAllPresets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return presets_;
}

std::vector<EffectPreset> EffectPresetService::getPresetsForEffect(const std::string& effectType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EffectPreset> result;
    std::string lower = effectType;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& p : presets_) {
        std::string pLower = p.effectType;
        std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
        if (pLower == lower) result.push_back(p);
    }
    return result;
}

std::vector<EffectPreset> EffectPresetService::getPresetsInCategory(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EffectPreset> result;
    for (auto& p : presets_)
        if (p.category == category) result.push_back(p);
    return result;
}

std::vector<EffectPreset> EffectPresetService::searchPresets(const std::string& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lower = query;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::vector<EffectPreset> result;
    for (auto& p : presets_) {
        std::string nameLower = p.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        std::string typeLower = p.effectType;
        std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), ::tolower);
        if (nameLower.find(lower) != std::string::npos || typeLower.find(lower) != std::string::npos)
            result.push_back(p);
    }
    return result;
}

const EffectPreset* EffectPresetService::getPresetById(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& p : presets_)
        if (p.id == id) return &p;
    return nullptr;
}

bool EffectPresetService::applyPreset(const std::string& presetId, DSPProcessor* effect) {
    if (!effect) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(presets_.begin(), presets_.end(),
        [&](const EffectPreset& ep) { return ep.id == presetId; });
    if (it == presets_.end()) return false;

    effect->setWetDry(it->wetDry);
    effect->setBypass(it->bypassed);
    spdlog::info("EffectPresetService: applied preset '{}' to effect '{}'", it->name, effect->getName());
    return true;
}

EffectPreset EffectPresetService::capturePreset(const DSPProcessor* effect, const std::string& name,
                                                 const std::string& category) {
    EffectPreset p;
    p.id = generateId();
    p.name = name;
    p.category = category;
    p.createdAt = currentTimestamp();
    p.modifiedAt = p.createdAt;
    if (effect) {
        p.effectType = effect->getName();
        p.wetDry = effect->getWetDry();
        p.bypassed = effect->isBypassed();
    }
    return p;
}

void EffectPresetService::initializeFactoryPresets() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto addFactory = [&](const std::string& name, const std::string& type,
                          std::vector<EffectPresetParam> params, float wet = 1.0f) {
        EffectPreset p;
        p.id = "factory_" + type + "_" + name;
        std::transform(p.id.begin(), p.id.end(), p.id.begin(), [](char c) { return c == ' ' ? '_' : (char)::tolower(c); });
        p.name = name;
        p.effectType = type;
        p.category = "factory";
        p.author = "BeatMate";
        p.parameters = std::move(params);
        p.wetDry = wet;
        p.createdAt = 0;
        p.modifiedAt = 0;
        presets_.push_back(std::move(p));
    };

    addFactory("Small Room", "Reverb", {{"roomSize", 0.3f, 0.0f, 1.0f}, {"damping", 0.7f, 0.0f, 1.0f}, {"decay", 0.4f, 0.0f, 1.0f}}, 0.3f);
    addFactory("Large Hall", "Reverb", {{"roomSize", 0.9f, 0.0f, 1.0f}, {"damping", 0.3f, 0.0f, 1.0f}, {"decay", 0.8f, 0.0f, 1.0f}}, 0.5f);
    addFactory("Cathedral", "Reverb", {{"roomSize", 1.0f, 0.0f, 1.0f}, {"damping", 0.2f, 0.0f, 1.0f}, {"decay", 0.95f, 0.0f, 1.0f}}, 0.6f);
    addFactory("Plate", "Reverb", {{"roomSize", 0.5f, 0.0f, 1.0f}, {"damping", 0.5f, 0.0f, 1.0f}, {"decay", 0.6f, 0.0f, 1.0f}}, 0.4f);

    addFactory("Short Slapback", "Delay", {{"time", 0.08f, 0.0f, 2.0f}, {"feedback", 0.2f, 0.0f, 1.0f}}, 0.4f);
    addFactory("Quarter Note", "Delay", {{"time", 0.5f, 0.0f, 2.0f}, {"feedback", 0.4f, 0.0f, 1.0f}}, 0.35f);
    addFactory("Dub Echo", "Delay", {{"time", 0.375f, 0.0f, 2.0f}, {"feedback", 0.65f, 0.0f, 1.0f}}, 0.5f);
    addFactory("Ping Pong", "Delay", {{"time", 0.25f, 0.0f, 2.0f}, {"feedback", 0.5f, 0.0f, 1.0f}, {"stereoSpread", 1.0f, 0.0f, 1.0f}}, 0.4f);

    addFactory("DJ Low Pass", "Filter", {{"cutoff", 0.3f, 0.0f, 1.0f}, {"resonance", 0.4f, 0.0f, 1.0f}, {"type", 0.0f, 0.0f, 2.0f}}, 1.0f);
    addFactory("DJ High Pass", "Filter", {{"cutoff", 0.7f, 0.0f, 1.0f}, {"resonance", 0.4f, 0.0f, 1.0f}, {"type", 1.0f, 0.0f, 2.0f}}, 1.0f);
    addFactory("Resonant Sweep", "Filter", {{"cutoff", 0.5f, 0.0f, 1.0f}, {"resonance", 0.8f, 0.0f, 1.0f}}, 1.0f);

    addFactory("Classic Flanger", "Flanger", {{"depth", 0.5f, 0.0f, 1.0f}, {"rate", 0.3f, 0.0f, 1.0f}, {"feedback", 0.5f, 0.0f, 1.0f}}, 0.5f);
    addFactory("Jet Flanger", "Flanger", {{"depth", 0.9f, 0.0f, 1.0f}, {"rate", 0.15f, 0.0f, 1.0f}, {"feedback", 0.8f, 0.0f, 1.0f}}, 0.7f);

    addFactory("Slow Phaser", "Phaser", {{"depth", 0.6f, 0.0f, 1.0f}, {"rate", 0.2f, 0.0f, 1.0f}, {"stages", 4.0f, 2.0f, 12.0f}}, 0.5f);
    addFactory("Fast Phaser", "Phaser", {{"depth", 0.7f, 0.0f, 1.0f}, {"rate", 0.7f, 0.0f, 1.0f}, {"stages", 6.0f, 2.0f, 12.0f}}, 0.5f);

    addFactory("Warm Overdrive", "Distortion", {{"drive", 0.3f, 0.0f, 1.0f}, {"tone", 0.6f, 0.0f, 1.0f}}, 0.5f);
    addFactory("Hard Clip", "Distortion", {{"drive", 0.8f, 0.0f, 1.0f}, {"tone", 0.4f, 0.0f, 1.0f}}, 0.4f);

    addFactory("Lo-Fi", "BitCrusher", {{"bits", 8.0f, 1.0f, 16.0f}, {"sampleReduction", 0.3f, 0.0f, 1.0f}}, 0.5f);
    addFactory("Retro 8-bit", "BitCrusher", {{"bits", 4.0f, 1.0f, 16.0f}, {"sampleReduction", 0.6f, 0.0f, 1.0f}}, 0.6f);

    addFactory("DJ Master", "Compressor", {{"threshold", -12.0f, -60.0f, 0.0f}, {"ratio", 4.0f, 1.0f, 20.0f}, {"attack", 10.0f, 0.1f, 100.0f}, {"release", 100.0f, 10.0f, 1000.0f}}, 1.0f);
    addFactory("Sidechain Pump", "Compressor", {{"threshold", -20.0f, -60.0f, 0.0f}, {"ratio", 8.0f, 1.0f, 20.0f}, {"attack", 1.0f, 0.1f, 100.0f}, {"release", 200.0f, 10.0f, 1000.0f}}, 1.0f);

    addFactory("Subtle Vinyl", "Vinyl", {{"crackle", 0.2f, 0.0f, 1.0f}, {"wear", 0.1f, 0.0f, 1.0f}, {"warp", 0.05f, 0.0f, 1.0f}}, 0.3f);
    addFactory("Dusty Record", "Vinyl", {{"crackle", 0.6f, 0.0f, 1.0f}, {"wear", 0.5f, 0.0f, 1.0f}, {"warp", 0.2f, 0.0f, 1.0f}}, 0.5f);

    spdlog::info("EffectPresetService: initialized {} factory presets", presets_.size());
}

void EffectPresetService::toggleFavorite(const std::string& presetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(favorites_.begin(), favorites_.end(), presetId);
    if (it != favorites_.end()) favorites_.erase(it);
    else favorites_.push_back(presetId);
}

bool EffectPresetService::isFavorite(const std::string& presetId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(favorites_.begin(), favorites_.end(), presetId) != favorites_.end();
}

std::vector<EffectPreset> EffectPresetService::getFavorites() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EffectPreset> result;
    for (auto& fid : favorites_) {
        for (auto& p : presets_)
            if (p.id == fid) { result.push_back(p); break; }
    }
    return result;
}

void EffectPresetService::markRecentlyUsed(const std::string& presetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    recentIds_.erase(std::remove(recentIds_.begin(), recentIds_.end(), presetId), recentIds_.end());
    recentIds_.insert(recentIds_.begin(), presetId);
    if (recentIds_.size() > 50) recentIds_.resize(50);
}

std::vector<EffectPreset> EffectPresetService::getRecentPresets(int maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EffectPreset> result;
    int count = 0;
    for (auto& rid : recentIds_) {
        if (count >= maxCount) break;
        for (auto& p : presets_)
            if (p.id == rid) { result.push_back(p); ++count; break; }
    }
    return result;
}

int EffectPresetService::getPresetCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(presets_.size());
}

std::string EffectPresetService::generateId() const {
    static std::mt19937 rng(std::random_device{}());
    static const char chars[] = "0123456789abcdef";
    std::string id = "preset_";
    for (int i = 0; i < 16; ++i) id += chars[rng() % 16];
    return id;
}

int64_t EffectPresetService::currentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace BeatMate::Core
