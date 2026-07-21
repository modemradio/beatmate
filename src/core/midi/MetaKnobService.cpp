#include "MetaKnobService.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

float MetaKnobMapping::computeValue(float metaPosition) const {
    float pos = inverted ? (1.0f - metaPosition) : metaPosition;
    if (curveFactor != 1.0f && curveFactor > 0.0f) {
        pos = std::pow(pos, curveFactor);
    }
    return curveStart + pos * (curveEnd - curveStart);
}

MetaKnobService::MetaKnobService() {
    currentProfile_.id = "default";
    currentProfile_.name = "Default";
    currentProfile_.category = "user";
}

void MetaKnobService::setPosition(float position) {
    position_.store(std::clamp(position, 0.0f, 1.0f));
    applyMappings();
}

void MetaKnobService::loadProfile(const MetaKnobProfile& profile) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile_ = profile;
    spdlog::info("MetaKnobService: loaded profile '{}'", profile.name);
}

MetaKnobProfile MetaKnobService::getCurrentProfile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile_;
}

void MetaKnobService::clearProfile() {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProfile_.mappings.clear();
}

void MetaKnobService::addMapping(const MetaKnobMapping& mapping) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& mappings = currentProfile_.mappings;
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
        [&](const MetaKnobMapping& m) { return m.parameterId == mapping.parameterId; }),
        mappings.end());
    mappings.push_back(mapping);
    spdlog::debug("MetaKnobService: added mapping for '{}'", mapping.parameterId);
}

void MetaKnobService::removeMapping(const std::string& parameterId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& mappings = currentProfile_.mappings;
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
        [&](const MetaKnobMapping& m) { return m.parameterId == parameterId; }),
        mappings.end());
}

void MetaKnobService::updateMapping(const std::string& parameterId, const MetaKnobMapping& mapping) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : currentProfile_.mappings) {
        if (m.parameterId == parameterId) {
            m = mapping;
            return;
        }
    }
    currentProfile_.mappings.push_back(mapping);
}

std::vector<MetaKnobMapping> MetaKnobService::getMappings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProfile_.mappings;
}

void MetaKnobService::setParameterCallback(ParameterCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    paramCallback_ = std::move(callback);
}

void MetaKnobService::applyMappings() {
    std::lock_guard<std::mutex> lock(mutex_);
    float pos = position_.load();
    for (auto& mapping : currentProfile_.mappings) {
        float value = mapping.computeValue(pos);
        if (paramCallback_) {
            paramCallback_(mapping.parameterId, value);
        }
    }
}

std::vector<MetaKnobProfile> MetaKnobService::getFactoryProfiles() {
    std::vector<MetaKnobProfile> profiles;

    {
        MetaKnobProfile p;
        p.id = "factory_filter_sweep";
        p.name = "Filter Sweep";
        p.category = "factory";
        p.mappings = {
            {"filter.cutoff", 0.0f, 1.0f, false, 1.0f, "Cutoff"},
            {"filter.resonance", 0.1f, 0.8f, false, 0.7f, "Resonance"},
            {"filter.wetDry", 0.0f, 1.0f, false, 1.0f, "Wet/Dry"}
        };
        profiles.push_back(p);
    }

    {
        MetaKnobProfile p;
        p.id = "factory_build_up";
        p.name = "Build Up";
        p.category = "factory";
        p.mappings = {
            {"filter.cutoff", 0.2f, 1.0f, false, 1.5f, "Filter"},
            {"reverb.wetDry", 0.0f, 0.7f, false, 2.0f, "Reverb"},
            {"delay.feedback", 0.0f, 0.6f, false, 1.5f, "Delay FB"},
            {"gain.volume", 1.0f, 1.3f, false, 1.0f, "Volume"}
        };
        profiles.push_back(p);
    }

    {
        MetaKnobProfile p;
        p.id = "factory_breakdown";
        p.name = "Breakdown";
        p.category = "factory";
        p.mappings = {
            {"reverb.wetDry", 0.0f, 0.8f, false, 1.0f, "Reverb"},
            {"delay.wetDry", 0.0f, 0.5f, false, 1.0f, "Delay"},
            {"filter.cutoff", 1.0f, 0.3f, false, 1.0f, "Filter"},
            {"eq.low", 0.0f, -6.0f, false, 1.0f, "Bass Cut"}
        };
        profiles.push_back(p);
    }

    {
        MetaKnobProfile p;
        p.id = "factory_dub_echo";
        p.name = "Dub Echo";
        p.category = "factory";
        p.mappings = {
            {"delay.wetDry", 0.0f, 0.7f, false, 1.0f, "Send Level"},
            {"delay.feedback", 0.2f, 0.75f, false, 1.0f, "Feedback"},
            {"delay.time", 0.25f, 0.5f, false, 1.0f, "Time"},
            {"filter.cutoff", 1.0f, 0.4f, false, 1.0f, "Tone"}
        };
        profiles.push_back(p);
    }

    {
        MetaKnobProfile p;
        p.id = "factory_lofi";
        p.name = "Lo-Fi Degrade";
        p.category = "factory";
        p.mappings = {
            {"bitcrusher.bits", 16.0f, 4.0f, false, 1.0f, "Bit Depth"},
            {"bitcrusher.rate", 0.0f, 0.8f, false, 1.0f, "Downsample"},
            {"vinyl.crackle", 0.0f, 0.6f, false, 0.7f, "Crackle"},
            {"filter.cutoff", 1.0f, 0.5f, false, 1.0f, "Tone"}
        };
        profiles.push_back(p);
    }

    {
        MetaKnobProfile p;
        p.id = "factory_wash_out";
        p.name = "Wash Out";
        p.category = "factory";
        p.mappings = {
            {"reverb.wetDry", 0.0f, 1.0f, false, 1.5f, "Reverb"},
            {"reverb.decay", 0.3f, 0.95f, false, 1.0f, "Decay"},
            {"filter.cutoff", 0.5f, 1.0f, false, 1.0f, "Filter"},
            {"gain.volume", 1.0f, 0.0f, false, 2.0f, "Fade Out"}
        };
        profiles.push_back(p);
    }

    return profiles;
}

MetaKnobProfile MetaKnobService::getFactoryProfile(const std::string& name) {
    auto profiles = getFactoryProfiles();
    for (auto& p : profiles)
        if (p.name == name) return p;
    return {"default", "Default", "factory", {}};
}

nlohmann::json MetaKnobService::toJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j;
    j["position"] = position_.load();
    j["profile"]["id"] = currentProfile_.id;
    j["profile"]["name"] = currentProfile_.name;
    j["profile"]["category"] = currentProfile_.category;
    nlohmann::json mappings = nlohmann::json::array();
    for (auto& m : currentProfile_.mappings) {
        mappings.push_back({
            {"parameterId", m.parameterId}, {"curveStart", m.curveStart},
            {"curveEnd", m.curveEnd}, {"inverted", m.inverted},
            {"curveFactor", m.curveFactor}, {"label", m.label}
        });
    }
    j["profile"]["mappings"] = mappings;
    return j;
}

void MetaKnobService::fromJson(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (j.contains("position")) position_.store(j["position"].get<float>());
    if (j.contains("profile")) {
        auto& pj = j["profile"];
        currentProfile_.id = pj.value("id", "default");
        currentProfile_.name = pj.value("name", "Default");
        currentProfile_.category = pj.value("category", "user");
        currentProfile_.mappings.clear();
        if (pj.contains("mappings") && pj["mappings"].is_array()) {
            for (auto& mj : pj["mappings"]) {
                MetaKnobMapping m;
                m.parameterId = mj.value("parameterId", "");
                m.curveStart = mj.value("curveStart", 0.0f);
                m.curveEnd = mj.value("curveEnd", 1.0f);
                m.inverted = mj.value("inverted", false);
                m.curveFactor = mj.value("curveFactor", 1.0f);
                m.label = mj.value("label", "");
                currentProfile_.mappings.push_back(m);
            }
        }
    }
}

} // namespace BeatMate::Core
