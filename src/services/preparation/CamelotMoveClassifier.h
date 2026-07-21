#pragma once

#include <string>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::Services::Preparation {

enum class CamelotMove : int {
    Same         = 0,
    PlusOne      = 1,
    MinusOne     = 2,
    Relative     = 3,
    EnergyBoost  = 4,
    Dominant     = 5,
    MoodShift    = 6,
    Diagonal     = 7,
    Risky        = 8,
    Clash        = 9,
    Unknown      = 10
};

class CamelotMoveClassifier {
public:
    struct Result {
        CamelotMove move = CamelotMove::Unknown;
        std::string label;
        juce::Colour color;
        int distance = -1;
    };

    CamelotMoveClassifier() = default;

    Result classify(const std::string& fromCamelot, const std::string& toCamelot) const;

    static const char*  moveLabel(CamelotMove m);
    static juce::Colour moveColor(CamelotMove m);

private:
    static bool parseCamelot(const std::string& s, int& number, char& letter);
};

}
