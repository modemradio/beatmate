#pragma once
#include <string>
#include <utility>
#include <vector>
namespace BeatMate::Core {
class HarmonicMixer {
public:
    HarmonicMixer() = default;
    std::vector<std::string> getCompatibleKeys(const std::string& camelotKey);
    bool areCompatible(const std::string& keyA, const std::string& keyB);
    float getHarmonicScore(const std::string& keyA, const std::string& keyB);
private:
    std::pair<int, char> parseCamelot(const std::string& key);
};
} // namespace BeatMate::Core
