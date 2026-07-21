#pragma once
#include "MIDIEngine.h"
#include <functional>
#include <string>
#include <map>
namespace BeatMate::Core {
struct ControllerProfile { std::string name; std::string manufacturer; std::map<int, std::string> ccMappings; std::map<int, std::string> noteMappings; };
class MIDIController {
public:
    MIDIController() = default;
    void loadProfile(const ControllerProfile& profile) { profile_ = profile; }
    std::string processCC(int cc, int value);
    std::string processNote(int note, int velocity);
    const ControllerProfile& getProfile() const { return profile_; }
private:
    ControllerProfile profile_;
};
} // namespace BeatMate::Core
