#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "MIDIMapping.h"

namespace BeatMate::Models {

struct ControllerProfile {
    int64_t id = 0;
    std::string name;
    std::string vendor;
    std::string model;
    std::string description;

    std::vector<MIDIMapping> mappings;

    std::string layoutImage;

    std::string midiInputDevice;
    std::string midiOutputDevice;

    std::string version;
    std::string author;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;
    bool isBuiltIn = false;
    bool isActive = false;

    int deckCount = 2;

    bool hasJogWheel = false;
    bool hasPads = false;
    bool hasMotorizedFaders = false;
    bool hasDisplay = false;
    int padCount = 8;
    int faderCount = 2;
    int knobCount = 0;

    ControllerProfile() = default;

    ControllerProfile(const std::string& name, const std::string& vendor, const std::string& model)
        : name(name), vendor(vendor), model(model) {}

    bool operator==(const ControllerProfile& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ControllerProfile,
        id, name, vendor, model, description,
        mappings, layoutImage,
        midiInputDevice, midiOutputDevice,
        version, author, createdAt, modifiedAt, isBuiltIn, isActive,
        deckCount, hasJogWheel, hasPads, hasMotorizedFaders, hasDisplay,
        padCount, faderCount, knobCount
    )
};

}
