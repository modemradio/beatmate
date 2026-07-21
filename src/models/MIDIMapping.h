#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

enum class MIDIMappingType : int {
    CC = 0,             // Control Change
    Note = 1,           // Note On/Off
    PitchBend = 2,
    Aftertouch = 3,
    ProgramChange = 4
};

NLOHMANN_JSON_SERIALIZE_ENUM(MIDIMappingType, {
    { MIDIMappingType::CC, "CC" },
    { MIDIMappingType::Note, "Note" },
    { MIDIMappingType::PitchBend, "PitchBend" },
    { MIDIMappingType::Aftertouch, "Aftertouch" },
    { MIDIMappingType::ProgramChange, "ProgramChange" }
})

enum class MIDIBehavior : int {
    Toggle = 0,         // on/off toggle
    Momentary = 1,      // active while held
    Absolute = 2,       // absolute value (0-127)
    Relative = 3,       // relative encoder
    RelativeBinary = 4, // relative binary offset
    Increment = 5,
    Decrement = 6
};

NLOHMANN_JSON_SERIALIZE_ENUM(MIDIBehavior, {
    { MIDIBehavior::Toggle, "Toggle" },
    { MIDIBehavior::Momentary, "Momentary" },
    { MIDIBehavior::Absolute, "Absolute" },
    { MIDIBehavior::Relative, "Relative" },
    { MIDIBehavior::RelativeBinary, "RelativeBinary" },
    { MIDIBehavior::Increment, "Increment" },
    { MIDIBehavior::Decrement, "Decrement" }
})

struct MIDIMapping {
    int channel = 0;            // MIDI channel (0-15)
    int control = 0;            // CC number or note number
    int note = 0;               // note number (for Note type)
    std::string targetParameter; // target parameter name, e.g. "deck1.volume"
    MIDIMappingType mappingType = MIDIMappingType::CC;
    MIDIBehavior behavior = MIDIBehavior::Absolute;

    float rangeMin = 0.0f;
    float rangeMax = 1.0f;

    bool inverted = false;

    bool softTakeover = false;

    std::string label;
    std::string group;           // group for organization, e.g. "Deck A", "Mixer"

    bool hasFeedback = false;
    int feedbackChannel = 0;
    int feedbackControl = 0;

    MIDIMapping() = default;

    MIDIMapping(int channel, int control, const std::string& targetParameter, MIDIMappingType type)
        : channel(channel), control(control), targetParameter(targetParameter), mappingType(type) {}

    bool operator==(const MIDIMapping& other) const {
        return channel == other.channel && control == other.control &&
               mappingType == other.mappingType && targetParameter == other.targetParameter;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MIDIMapping,
        channel, control, note, targetParameter, mappingType, behavior,
        rangeMin, rangeMax, inverted, softTakeover,
        label, group, hasFeedback, feedbackChannel, feedbackControl
    )
};

} // namespace BeatMate::Models
