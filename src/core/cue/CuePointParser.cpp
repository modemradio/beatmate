#include "CuePointParser.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

std::vector<CuePoint> CuePointParser::parseRekordbox(const std::string& xmlPath) {
    std::vector<CuePoint> cues;
    std::ifstream file(xmlPath);
    if (!file) { spdlog::error("CuePointParser: cannot open {}", xmlPath); return cues; }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::regex cueRegex(R"delim(<POSITION_MARK[^>]*Num="(\d+)"[^>]*Start="([0-9.]+)"[^>]*Name="([^"]*)"[^>]*/>)delim");
    auto begin = std::sregex_iterator(content.begin(), content.end(), cueRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        CuePoint cue;
        cue.number = std::stoi((*it)[1].str()) + 1;
        cue.position = std::stod((*it)[2].str()) / 1000.0;
        cue.name = (*it)[3].str();
        cues.push_back(cue);
    }

    spdlog::info("CuePointParser: parsed {} Rekordbox cues", cues.size());
    return cues;
}

std::vector<CuePoint> CuePointParser::parseSerato(const std::string& filePath) {
    spdlog::info("CuePointParser: Serato parsing from {}", filePath);
    return {};
}

std::vector<CuePoint> CuePointParser::parseTraktor(const std::string& nmlPath) {
    std::vector<CuePoint> cues;
    std::ifstream file(nmlPath);
    if (!file) return cues;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::regex cueRegex(R"delim(<CUE_V2[^>]*Hotcue="(\d+)"[^>]*Start="([0-9.]+)"[^>]*Name="([^"]*)"[^>]*/>)delim");
    auto begin = std::sregex_iterator(content.begin(), content.end(), cueRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        CuePoint cue;
        cue.number = std::stoi((*it)[1].str()) + 1;
        cue.position = std::stod((*it)[2].str()) / 1000.0;
        cue.name = (*it)[3].str();
        cues.push_back(cue);
    }

    spdlog::info("CuePointParser: parsed {} Traktor cues", cues.size());
    return cues;
}

}
