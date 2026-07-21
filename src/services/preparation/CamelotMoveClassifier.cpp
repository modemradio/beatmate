#include "CamelotMoveClassifier.h"

#include <cctype>
#include <cstdlib>

namespace BeatMate::Services::Preparation {

bool CamelotMoveClassifier::parseCamelot(const std::string& s, int& number, char& letter) {
    if (s.size() < 2 || s.size() > 3) return false;
    const std::string digits = s.substr(0, s.size() - 1);
    char last = static_cast<char>(std::toupper(s.back()));
    if (last != 'A' && last != 'B') return false;
    for (char c : digits) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    int n = std::atoi(digits.c_str());
    if (n < 1 || n > 12) return false;
    number = n;
    letter = last;
    return true;
}

CamelotMoveClassifier::Result
CamelotMoveClassifier::classify(const std::string& fromCamelot,
                                const std::string& toCamelot) const {
    Result r;
    int fromN = 0, toN = 0;
    char fromL = '\0', toL = '\0';

    if (!parseCamelot(fromCamelot, fromN, fromL) || !parseCamelot(toCamelot, toN, toL)) {
        r.move = CamelotMove::Unknown;
        r.label = moveLabel(r.move);
        r.color = moveColor(r.move);
        return r;
    }

    int diff = toN - fromN;
    int circ = std::abs(diff);
    if (circ > 6) circ = 12 - circ;
    r.distance = circ;

    bool sameLetter = (fromL == toL);
    int signedMod = ((diff % 12) + 12) % 12;  // 0..11

    if (fromN == toN && sameLetter)                       r.move = CamelotMove::Same;
    else if (fromN == toN && !sameLetter)                 r.move = CamelotMove::Relative;
    else if (sameLetter && signedMod == 1)                r.move = CamelotMove::PlusOne;
    else if (sameLetter && signedMod == 11)               r.move = CamelotMove::MinusOne;
    else if (sameLetter && signedMod == 7)                r.move = CamelotMove::EnergyBoost;
    else if (sameLetter && signedMod == 5)                r.move = CamelotMove::Dominant;
    else if (sameLetter && signedMod == 2)                r.move = CamelotMove::MoodShift;
    else if (!sameLetter && circ == 1)                    r.move = CamelotMove::Diagonal;
    else if (circ <= 2)                                   r.move = CamelotMove::Risky;
    else                                                  r.move = CamelotMove::Clash;

    r.label = moveLabel(r.move);
    r.color = moveColor(r.move);
    return r;
}

const char* CamelotMoveClassifier::moveLabel(CamelotMove m) {
    switch (m) {
        case CamelotMove::Same:         return "Same";
        case CamelotMove::PlusOne:      return "+1";
        case CamelotMove::MinusOne:     return "-1";
        case CamelotMove::Relative:     return "Relative";
        case CamelotMove::EnergyBoost:  return "Energy Boost";
        case CamelotMove::Dominant:     return "Dominant";
        case CamelotMove::MoodShift:    return "Mood Shift";
        case CamelotMove::Diagonal:     return "Diagonal";
        case CamelotMove::Risky:        return "Risky";
        case CamelotMove::Clash:        return "Clash";
        case CamelotMove::Unknown:      return "?";
    }
    return "?";
}

juce::Colour CamelotMoveClassifier::moveColor(CamelotMove m) {
    switch (m) {
        case CamelotMove::Same:         return juce::Colour::fromRGB( 80, 200, 120);
        case CamelotMove::PlusOne:      return juce::Colour::fromRGB( 60, 200, 180);
        case CamelotMove::MinusOne:     return juce::Colour::fromRGB( 80, 160, 220);
        case CamelotMove::Relative:     return juce::Colour::fromRGB(120, 180, 240);
        case CamelotMove::EnergyBoost:  return juce::Colour::fromRGB(255, 140,  40);
        case CamelotMove::Dominant:     return juce::Colour::fromRGB(200, 160,  80);
        case CamelotMove::MoodShift:    return juce::Colour::fromRGB(160, 200, 120);
        case CamelotMove::Diagonal:     return juce::Colour::fromRGB(220, 180,  80);
        case CamelotMove::Risky:        return juce::Colour::fromRGB(220, 120,  80);
        case CamelotMove::Clash:        return juce::Colour::fromRGB(220,  60,  60);
        case CamelotMove::Unknown:      return juce::Colour::fromRGB(120, 120, 120);
    }
    return juce::Colour::fromRGB(120, 120, 120);
}

} // namespace BeatMate::Services::Preparation
