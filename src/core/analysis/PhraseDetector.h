#pragma once

#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct Phrase {
    double startTime = 0.0;
    double endTime = 0.0;
    std::string type;
    int bars = 4;
    float confidence = 0.0f;
};

class PhraseDetector {
public:
    PhraseDetector();
    ~PhraseDetector();

    std::vector<Phrase> detect(const AudioTrack& track, double bpm);

private:
    std::vector<double> findPhraseEnds(const std::vector<float>& energy, double bpm,
                                        int sampleRate, int hopSize);
};

}
