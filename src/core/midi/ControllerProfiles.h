#pragma once
#include "MIDIController.h"
#include <vector>
namespace BeatMate::Core {
class ControllerProfiles {
public:
    static ControllerProfile getDDJ400();
    static ControllerProfile getDDJ800();
    static ControllerProfile getDDJ1000();

    static ControllerProfile getKontrolS2();
    static ControllerProfile getKontrolS4();

    static ControllerProfile getMC7000();

    static ControllerProfile getGeneric();

    static std::vector<std::string> getAvailableProfiles();
    static ControllerProfile getProfile(const std::string& name);
    static bool exportProfile(const ControllerProfile& profile, const std::string& path);
    static ControllerProfile importProfile(const std::string& path);
};
}
