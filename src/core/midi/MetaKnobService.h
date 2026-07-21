#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace BeatMate::Core {

// MetaKnobService - One knob controlling multiple parameters simultaneously

struct MetaKnobMapping {
    std::string parameterId;        // Target parameter identifier
    float curveStart = 0.0f;        // Output value when meta knob is at 0
    float curveEnd = 1.0f;          // Output value when meta knob is at 1
    bool inverted = false;          // Invert the mapping curve
    float curveFactor = 1.0f;       // 1.0=linear, <1=log, >1=exp
    std::string label;              // Display label

    // Compute mapped value from meta knob position [0..1]
    float computeValue(float metaPosition) const;
};

struct MetaKnobProfile {
    std::string id;
    std::string name;
    std::string category;           // "factory", "user"
    std::vector<MetaKnobMapping> mappings;
};

class MetaKnobService {
public:
    using ParameterCallback = std::function<void(const std::string& parameterId, float value)>;

    MetaKnobService();
    ~MetaKnobService() = default;

    // Set the meta knob position [0..1]
    void setPosition(float position);
    float getPosition() const { return position_.load(); }

    void loadProfile(const MetaKnobProfile& profile);
    MetaKnobProfile getCurrentProfile() const;
    void clearProfile();

    void addMapping(const MetaKnobMapping& mapping);
    void removeMapping(const std::string& parameterId);
    void updateMapping(const std::string& parameterId, const MetaKnobMapping& mapping);
    std::vector<MetaKnobMapping> getMappings() const;

    void setParameterCallback(ParameterCallback callback);

    static std::vector<MetaKnobProfile> getFactoryProfiles();
    static MetaKnobProfile getFactoryProfile(const std::string& name);

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    void applyMappings();

    std::atomic<float> position_{0.5f};
    MetaKnobProfile currentProfile_;
    ParameterCallback paramCallback_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Core
