#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

enum class KeyNotation {
    Standard,    // Am, C, F#m, etc.
    Camelot,     // 8A, 1B, etc.
    OpenKey,     // 1m, 1d, etc.
    German       // a-Moll, C-Dur, etc.
};

struct KeyInfo {
    int pitchClass = 0;      // 0-11 (C=0)
    bool isMinor = false;
    std::string standard;    // "Am"
    std::string camelot;     // "8A"
    std::string openKey;     // "1m"
    std::string german;      // "a-Moll"
};

class KeyFormatsService {
public:
    KeyFormatsService();
    ~KeyFormatsService();

    KeyInfo fromStandard(const std::string& key);
    KeyInfo fromCamelot(const std::string& key);
    KeyInfo fromOpenKey(const std::string& key);
    KeyInfo fromGerman(const std::string& key);

    KeyInfo autoDetect(const std::string& key);

    std::string toStandard(int pitchClass, bool isMinor);
    std::string toCamelot(int pitchClass, bool isMinor);
    std::string toOpenKey(int pitchClass, bool isMinor);
    std::string toGerman(int pitchClass, bool isMinor);

    std::vector<KeyInfo> getCompatibleKeys(const KeyInfo& key);

    bool areCompatible(const KeyInfo& a, const KeyInfo& b);

    int camelotDistance(const KeyInfo& a, const KeyInfo& b);

    std::vector<KeyInfo> getAllKeys();

private:
    KeyInfo buildKeyInfo(int pitchClass, bool isMinor);
    int parsePitchClass(const std::string& noteName);
};

} // namespace BeatMate::Core
