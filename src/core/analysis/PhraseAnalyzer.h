#pragma once

#include <vector>

namespace BeatMate::Core {
    class AudioTrack;
}

namespace BeatMate::Core::Analysis {

enum class PhraseType : int {
    Intro   = 0,
    Up      = 1,
    Down    = 2,
    Chorus  = 3,
    Bridge  = 4,
    Verse   = 5,
    Outro   = 6,
    Unknown = 7
};

struct Phrase {
    double startSec     = 0.0;
    double endSec       = 0.0;
    PhraseType type     = PhraseType::Unknown;
    float confidence    = 0.0f; // 0..1
};

class PhraseAnalyzer {
public:
    PhraseAnalyzer() = default;
    ~PhraseAnalyzer() = default;

    // audio : PCM mono (ou premier canal), sampleRate en Hz
    std::vector<Phrase> analyze(const float* audio,
                                int numSamples,
                                double sampleRate,
                                double bpm = 0.0);

    std::vector<Phrase> analyze(const Core::AudioTrack& track);

    static const char* phraseLabel(PhraseType t);

    void setEnvelopeWindowSec(double s)   { envWindowSec_ = s; }
    void setPhraseBars(int bars)          { phraseBars_   = bars; }
    void setIntroOutroSec(double s)       { introOutroSec_ = s; }

private:
    std::vector<float>  computeEnergyEnvelope(const float* audio,
                                              int numSamples,
                                              double sampleRate,
                                              double& outHopSec) const;

    std::vector<float>  computeOnsetDensity(const float* audio,
                                            int numSamples,
                                            double sampleRate,
                                            double segmentSec) const;

    std::vector<double> segmentBoundaries(int numSamples,
                                          double sampleRate,
                                          double bpm,
                                          const std::vector<float>& envelope,
                                          double envHopSec) const;

    PhraseType classifyPhrase(double startSec,
                              double endSec,
                              double trackDurationSec,
                              const std::vector<float>& envelope,
                              double envHopSec,
                              const std::vector<float>& onsetDensity,
                              double densityHopSec,
                              float envMedian,
                              float envMax,
                              float densityMedian,
                              float densityMax,
                              int phraseIndex,
                              int numPhrases,
                              float& outConfidence) const;

    double envWindowSec_   = 0.5;   // RMS envelope window
    double densitySegSec_  = 2.0;   // onset-density resolution
    int    phraseBars_     = 8;     // target phrase length (bars)
    double introOutroSec_  = 16.0;  // intro/outro heuristic window
};

} // namespace BeatMate::Core::Analysis
