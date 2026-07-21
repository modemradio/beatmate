#include "HarmonicMixer.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

std::pair<int, char> HarmonicMixer::parseCamelot(const std::string& key) {
    if (key.size() < 2) return {0, 'A'};
    char letter = key.back();
    int num = std::stoi(key.substr(0, key.size() - 1));
    return {num, letter};
}

std::vector<std::string> HarmonicMixer::getCompatibleKeys(const std::string& camelotKey) {
    auto [num, letter] = parseCamelot(camelotKey);
    std::vector<std::string> compatible;

    compatible.push_back(camelotKey);

    int plus1 = (num % 12) + 1;
    compatible.push_back(std::to_string(plus1) + letter);

    int minus1 = ((num - 2 + 12) % 12) + 1;
    compatible.push_back(std::to_string(minus1) + letter);

    char parallel = (letter == 'A') ? 'B' : 'A';
    compatible.push_back(std::to_string(num) + parallel);

    return compatible;
}

bool HarmonicMixer::areCompatible(const std::string& keyA, const std::string& keyB) {
    auto compatibles = getCompatibleKeys(keyA);
    return std::find(compatibles.begin(), compatibles.end(), keyB) != compatibles.end();
}

float HarmonicMixer::getHarmonicScore(const std::string& keyA, const std::string& keyB) {
    if (keyA == keyB) return 1.0f;

    auto [numA, letterA] = parseCamelot(keyA);
    auto [numB, letterB] = parseCamelot(keyB);

    if (numA == numB && letterA != letterB) return 0.8f;

    int diff = std::abs(numA - numB);
    if (diff == 1 || diff == 11) {
        if (letterA == letterB) return 0.9f;
        return 0.6f;
    }

    if (diff == 2 || diff == 10) return 0.5f;

    return 0.2f;
}

}
