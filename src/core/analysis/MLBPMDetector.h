#pragma once

#include "BPMDetector.h"
#include <string>
#include <memory>

namespace BeatMate::Core {

class AudioTrack;

class MLBPMDetector {
public:
    MLBPMDetector();
    ~MLBPMDetector();

    bool loadModel(const std::string& modelPath);
    bool isModelLoaded() const { return modelLoaded_; }

    BPMResult detect(const AudioTrack& track);

private:
    struct OnnxSession;
    std::unique_ptr<OnnxSession> session_;
    bool modelLoaded_ = false;
    std::string modelPath_;

    std::unique_ptr<BPMDetector> fallbackDetector_;

    std::vector<float> extractFeatures(const AudioTrack& track);
};

} // namespace BeatMate::Core
