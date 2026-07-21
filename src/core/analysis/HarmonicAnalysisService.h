#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct ChordInfo {
    double startTime = 0.0;
    double endTime = 0.0;
    std::string name;            // "Cm", "G7", "Fmaj7", etc.
    int rootNote = 0;            // 0-11 (C=0)
    std::string quality;         // "major", "minor", "dominant7", etc.
    float confidence = 0.0f;
};

struct HarmonicProfile {
    std::vector<float> chromagram;       // 12-bin averaged chroma
    std::vector<ChordInfo> chords;       // Chord progression
    std::string key;                     // Detected key
    std::string mode;                    // "major" or "minor"
    float tonal = 0.0f;                 // Tonality strength (0=atonal, 1=strongly tonal)
    std::vector<std::vector<float>> chromaOverTime;  // Chromagram per frame
    double chromaFrameDuration = 0.0;
};

class HarmonicAnalysisService {
public:
    HarmonicAnalysisService();
    ~HarmonicAnalysisService();

    HarmonicProfile analyze(const AudioTrack& track);

    void setChordResolution(double seconds) { chordResolution_ = seconds; }

private:
    std::vector<std::vector<float>> extractChromagram(const AudioTrack& track, int hopSize);

    ChordInfo recognizeChord(const std::vector<float>& chroma);

    struct ChordTemplate {
        std::string name;
        std::string quality;
        float pattern[12];
    };
    static std::vector<ChordTemplate> getChordTemplates();

    double chordResolution_ = 0.5; // One chord per 500ms
};

} // namespace BeatMate::Core
