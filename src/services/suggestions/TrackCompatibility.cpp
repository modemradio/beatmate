#include "TrackCompatibility.h"
#include "HarmonicSuggestionEngine.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Services::Suggestions {

CompatibilityScore TrackCompatibility::calculateScore(const Models::Track& a, const Models::Track& b) {
    CompatibilityScore score;
    score.bpm = bpmCompatibility(a.bpm, b.bpm);
    score.key = keyCompatibility(a.camelotKey.empty() ? a.key : a.camelotKey,
                                  b.camelotKey.empty() ? b.key : b.camelotKey);
    score.energy = energyCompatibility(a.energy, b.energy);
    score.genre = genreCompatibility(a.genre, b.genre);
    score.overall = score.bpm * 0.3f + score.key * 0.3f + score.energy * 0.2f + score.genre * 0.2f;
    return score;
}

float TrackCompatibility::bpmCompatibility(double bpm1, double bpm2) {
    if (bpm1 <= 0 || bpm2 <= 0) return 0.5f;
    double diff = std::abs(bpm1 - bpm2);
    double maxDiff = bpm1 * 0.06; // 6% tolerance
    if (diff <= maxDiff) return static_cast<float>(1.0 - diff / maxDiff);
    double diffHalf = std::abs(bpm1 - bpm2 * 2.0);
    if (diffHalf <= maxDiff) return static_cast<float>(0.8 * (1.0 - diffHalf / maxDiff));
    return 0.0f;
}

float TrackCompatibility::keyCompatibility(const std::string& key1, const std::string& key2) {
    if (key1.empty() || key2.empty()) return 0.5f;
    if (key1 == key2) return 1.0f;
    auto compatible = HarmonicSuggestionEngine::getCompatibleKeys(key1);
    if (std::find(compatible.begin(), compatible.end(), key2) != compatible.end()) return 0.85f;
    return 0.0f;
}

float TrackCompatibility::energyCompatibility(float e1, float e2) {
    float diff = std::abs(e1 - e2);
    return std::max(0.0f, 1.0f - diff / 5.0f);
}

float TrackCompatibility::genreCompatibility(const std::string& g1, const std::string& g2) {
    if (g1.empty() || g2.empty()) return 0.5f;
    return (g1 == g2) ? 1.0f : 0.2f;
}

} // namespace BeatMate::Services::Suggestions
