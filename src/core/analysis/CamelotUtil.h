#pragma once
#include <cctype>
#include <map>
#include <string>
#include <utility>

namespace BeatMate::Core {

// Musical-key (Em, F#m, Bb, Amin...) ou Camelot deja formate -> Camelot ("8A").
inline std::string toCamelot(std::string k)
{
    std::string s;
    for (char c : k) if (c != ' ' && c != '\t') s += c;
    if (s.empty()) return "";

    if (s.size() >= 2 && s.size() <= 3
        && std::isdigit(static_cast<unsigned char>(s[0]))
        && (std::toupper(static_cast<unsigned char>(s.back())) == 'A'
            || std::toupper(static_cast<unsigned char>(s.back())) == 'B')) {
        const int num = std::atoi(s.substr(0, s.size() - 1).c_str());
        if (num >= 1 && num <= 12)
            return std::to_string(num)
                 + static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
        return "";
    }

    bool minor = false;
    if (s.size() >= 3 && s.substr(s.size() - 3) == "min") { minor = true; s.resize(s.size() - 3); }
    else if (s.size() >= 3 && s.substr(s.size() - 3) == "maj") { s.resize(s.size() - 3); }
    else if (s.size() >= 5 && s.substr(s.size() - 5) == "minor") { minor = true; s.resize(s.size() - 5); }
    else if (s.size() >= 5 && s.substr(s.size() - 5) == "major") { s.resize(s.size() - 5); }
    else if (s.back() == 'm' && s.size() >= 2) { minor = true; s.pop_back(); }
    else if (std::islower(static_cast<unsigned char>(s[0]))) { minor = true; }
    if (s.empty()) return "";

    s[0] = (char) std::toupper(static_cast<unsigned char>(s[0]));
    static const std::pair<std::string, std::string> kEnh[] = {
        {"Db","C#"}, {"Eb","D#"}, {"Gb","F#"}, {"Ab","G#"}, {"Bb","A#"}
    };
    for (auto& e : kEnh) if (s == e.first) s = e.second;

    static const std::map<std::string, std::string> kMajor = {
        {"C","8B"},{"C#","3B"},{"D","10B"},{"D#","5B"},{"E","12B"},
        {"F","7B"},{"F#","2B"},{"G","9B"},{"G#","4B"},{"A","11B"},
        {"A#","6B"},{"B","1B"}
    };
    static const std::map<std::string, std::string> kMinor = {
        {"C","5A"},{"C#","12A"},{"D","7A"},{"D#","2A"},{"E","9A"},
        {"F","4A"},{"F#","11A"},{"G","6A"},{"G#","1A"},{"A","8A"},
        {"A#","3A"},{"B","10A"}
    };
    const auto& tab = minor ? kMinor : kMajor;
    auto it = tab.find(s);
    return (it != tab.end()) ? it->second : std::string{};
}

} // namespace BeatMate::Core
