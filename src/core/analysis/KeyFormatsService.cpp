#include "KeyFormatsService.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

KeyFormatsService::KeyFormatsService() = default;
KeyFormatsService::~KeyFormatsService() = default;

// Camelot wheel mapping: pitchClass -> camelot number
static const int camelotMinor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
static const int camelotMajor[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };

// OpenKey: same numbers as Camelot but 'm' for minor, 'd' for major
static const int openKeyMinor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
static const int openKeyMajor[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };

static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* noteNamesFlat[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
static const char* germanMinor[12] = { "c-Moll", "cis-Moll", "d-Moll", "dis-Moll", "e-Moll", "f-Moll",
                                        "fis-Moll", "g-Moll", "gis-Moll", "a-Moll", "b-Moll", "h-Moll" };
static const char* germanMajor[12] = { "C-Dur", "Cis-Dur", "D-Dur", "Dis-Dur", "E-Dur", "F-Dur",
                                        "Fis-Dur", "G-Dur", "Gis-Dur", "A-Dur", "B-Dur", "H-Dur" };

KeyInfo KeyFormatsService::buildKeyInfo(int pitchClass, bool isMinor) {
    KeyInfo info;
    info.pitchClass = pitchClass % 12;
    info.isMinor = isMinor;
    info.standard = toStandard(info.pitchClass, isMinor);
    info.camelot = toCamelot(info.pitchClass, isMinor);
    info.openKey = toOpenKey(info.pitchClass, isMinor);
    info.german = toGerman(info.pitchClass, isMinor);
    return info;
}

std::string KeyFormatsService::toStandard(int pitchClass, bool isMinor) {
    pitchClass = pitchClass % 12;
    std::string s = noteNames[pitchClass];
    if (isMinor) s += "m";
    return s;
}

std::string KeyFormatsService::toCamelot(int pitchClass, bool isMinor) {
    pitchClass = pitchClass % 12;
    int num = isMinor ? camelotMinor[pitchClass] : camelotMajor[pitchClass];
    return std::to_string(num) + (isMinor ? "A" : "B");
}

std::string KeyFormatsService::toOpenKey(int pitchClass, bool isMinor) {
    pitchClass = pitchClass % 12;
    int num = isMinor ? openKeyMinor[pitchClass] : openKeyMajor[pitchClass];
    return std::to_string(num) + (isMinor ? "m" : "d");
}

std::string KeyFormatsService::toGerman(int pitchClass, bool isMinor) {
    pitchClass = pitchClass % 12;
    return isMinor ? germanMinor[pitchClass] : germanMajor[pitchClass];
}

int KeyFormatsService::parsePitchClass(const std::string& noteName) {
    static const std::map<std::string, int> noteMap = {
        {"C", 0}, {"C#", 1}, {"Db", 1}, {"D", 2}, {"D#", 3}, {"Eb", 3},
        {"E", 4}, {"F", 5}, {"F#", 6}, {"Gb", 6}, {"G", 7}, {"G#", 8},
        {"Ab", 8}, {"A", 9}, {"A#", 10}, {"Bb", 10}, {"B", 11}, {"Cb", 11}
    };
    auto it = noteMap.find(noteName);
    return (it != noteMap.end()) ? it->second : -1;
}

KeyInfo KeyFormatsService::fromStandard(const std::string& key) {
    if (key.empty()) return {};

    bool isMinor = false;
    std::string notePart = key;

    if (key.back() == 'm') {
        isMinor = true;
        notePart = key.substr(0, key.size() - 1);
    }

    int pc = parsePitchClass(notePart);
    if (pc < 0) {
        spdlog::warn("KeyFormatsService: unknown standard key '{}'", key);
        return {};
    }

    return buildKeyInfo(pc, isMinor);
}

KeyInfo KeyFormatsService::fromCamelot(const std::string& key) {
    if (key.size() < 2) return {};

    char mode = key.back();
    bool isMinor = (mode == 'A' || mode == 'a');
    int num = 0;
    try {
        num = std::stoi(key.substr(0, key.size() - 1));
    } catch (...) {
        spdlog::warn("KeyFormatsService: invalid camelot key '{}'", key);
        return {};
    }

    if (num < 1 || num > 12) return {};

    const int* table = isMinor ? camelotMinor : camelotMajor;
    for (int pc = 0; pc < 12; ++pc) {
        if (table[pc] == num) return buildKeyInfo(pc, isMinor);
    }

    return {};
}

KeyInfo KeyFormatsService::fromOpenKey(const std::string& key) {
    if (key.size() < 2) return {};

    char mode = key.back();
    bool isMinor = (mode == 'm' || mode == 'M');
    int num = 0;
    try {
        num = std::stoi(key.substr(0, key.size() - 1));
    } catch (...) {
        spdlog::warn("KeyFormatsService: invalid open key '{}'", key);
        return {};
    }

    if (num < 1 || num > 12) return {};

    const int* table = isMinor ? openKeyMinor : openKeyMajor;
    for (int pc = 0; pc < 12; ++pc) {
        if (table[pc] == num) return buildKeyInfo(pc, isMinor);
    }

    return {};
}

KeyInfo KeyFormatsService::fromGerman(const std::string& key) {
    for (int pc = 0; pc < 12; ++pc) {
        if (key == germanMinor[pc]) return buildKeyInfo(pc, true);
        if (key == germanMajor[pc]) return buildKeyInfo(pc, false);
    }
    spdlog::warn("KeyFormatsService: unknown German key '{}'", key);
    return {};
}

KeyInfo KeyFormatsService::autoDetect(const std::string& key) {
    if (key.empty()) return {};

    if ((key.back() == 'A' || key.back() == 'B') && std::isdigit(key[0])) {
        auto result = fromCamelot(key);
        if (result.pitchClass >= 0) return result;
    }

    if ((key.back() == 'm' || key.back() == 'd') && std::isdigit(key[0])) {
        auto result = fromOpenKey(key);
        if (result.pitchClass >= 0) return result;
    }

    if (key.find("Dur") != std::string::npos || key.find("Moll") != std::string::npos) {
        return fromGerman(key);
    }

    return fromStandard(key);
}

std::vector<KeyInfo> KeyFormatsService::getCompatibleKeys(const KeyInfo& key) {
    std::vector<KeyInfo> compatible;

    compatible.push_back(key);

    int camNum = key.isMinor ? camelotMinor[key.pitchClass] : camelotMajor[key.pitchClass];

    int nextNum = (camNum % 12) + 1;
    int prevNum = ((camNum - 2 + 12) % 12) + 1;

    const int* table = key.isMinor ? camelotMinor : camelotMajor;
    for (int pc = 0; pc < 12; ++pc) {
        if (table[pc] == nextNum) compatible.push_back(buildKeyInfo(pc, key.isMinor));
        if (table[pc] == prevNum) compatible.push_back(buildKeyInfo(pc, key.isMinor));
    }

    const int* otherTable = key.isMinor ? camelotMajor : camelotMinor;
    for (int pc = 0; pc < 12; ++pc) {
        if (otherTable[pc] == camNum) compatible.push_back(buildKeyInfo(pc, !key.isMinor));
    }

    return compatible;
}

bool KeyFormatsService::areCompatible(const KeyInfo& a, const KeyInfo& b) {
    return camelotDistance(a, b) <= 1;
}

int KeyFormatsService::camelotDistance(const KeyInfo& a, const KeyInfo& b) {
    int camA = a.isMinor ? camelotMinor[a.pitchClass] : camelotMajor[a.pitchClass];
    int camB = b.isMinor ? camelotMinor[b.pitchClass] : camelotMajor[b.pitchClass];

    int numDist = std::min(std::abs(camA - camB), 12 - std::abs(camA - camB));

    int modeDist = (a.isMinor != b.isMinor) ? 1 : 0;

    if (camA == camB && a.isMinor != b.isMinor) return 0; // Relative keys
    return numDist + (a.isMinor != b.isMinor ? 1 : 0);
}

std::vector<KeyInfo> KeyFormatsService::getAllKeys() {
    std::vector<KeyInfo> keys;
    for (int pc = 0; pc < 12; ++pc) {
        keys.push_back(buildKeyInfo(pc, false)); // major
        keys.push_back(buildKeyInfo(pc, true));  // minor
    }
    return keys;
}

} // namespace BeatMate::Core
