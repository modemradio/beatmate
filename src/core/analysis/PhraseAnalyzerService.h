#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct PhraseInfo {
    double startTime = 0.0;
    double endTime = 0.0;
    int bars = 0;            // 4, 8, 16, or 32
    int phraseNumber = 0;    // Sequential phrase number
    float energy = 0.0f;     // Average energy 0-1
    float confidence = 0.0f;
    std::string label;       // "Phrase 1 (8 bars)"
    bool isDownbeat = false; // Starts on a strong downbeat
};

struct PhraseAnalysisResult {
    std::vector<PhraseInfo> phrases4;    // 4-bar phrases
    std::vector<PhraseInfo> phrases8;    // 8-bar phrases
    std::vector<PhraseInfo> phrases16;   // 16-bar phrases
    std::vector<PhraseInfo> phrases32;   // 32-bar phrases
    int dominantPhraseLength = 8;        // Most common phrase length
    double bpm = 0.0;
};

class PhraseAnalyzerService {
public:
    PhraseAnalyzerService();
    ~PhraseAnalyzerService();

    PhraseAnalysisResult analyze(const AudioTrack& track, double bpm = 0.0);

private:
    std::vector<float> computeBarEnergies(const AudioTrack& track, double barDuration);

    std::vector<PhraseInfo> detectPhrases(const std::vector<float>& barEnergies,
                                           double barDuration, int phraseBars);

    int findDominantPhrase(const std::vector<float>& barEnergies, double barDuration);
};

} // namespace BeatMate::Core
