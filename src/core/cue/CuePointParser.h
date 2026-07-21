#pragma once
#include "HotCueManager.h"
#include <string>
#include <vector>
namespace BeatMate::Core {
class CuePointParser {
public:
    static std::vector<CuePoint> parseRekordbox(const std::string& xmlPath);
    static std::vector<CuePoint> parseSerato(const std::string& filePath);
    static std::vector<CuePoint> parseTraktor(const std::string& nmlPath);
};
} // namespace BeatMate::Core
