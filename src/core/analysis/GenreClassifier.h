#pragma once

#include <memory>
#include <string>
#include <vector>

namespace BeatMate::Core {

class AudioTrack;

struct GenreScore {
    std::string genre;
    float confidence = 0.0f;
};

class GenreClassifier {
public:
    GenreClassifier();
    ~GenreClassifier();

    bool loadModel(const std::string& modelPath);
    std::vector<GenreScore> classify(const AudioTrack& track);

private:
    struct Features {
        float bpm = 0;
        float spectralCentroid = 0;
        float zeroCrossingRate = 0;
        float rmsEnergy = 0;
        float spectralRolloff = 0;
        std::vector<float> mfcc;
    };

    Features extractFeatures(const AudioTrack& track);
    std::vector<GenreScore> classifyFromFeatures(const Features& features);

    bool modelLoaded_ = false;
};

}
