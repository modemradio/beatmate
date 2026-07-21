#pragma once

#include "BPMDetector.h"
#include <memory>

namespace BeatMate::Core {

class MLBPMDetector;

class HybridBPMDetector {
public:
    HybridBPMDetector();
    ~HybridBPMDetector();

    bool loadMLModel(const std::string& modelPath);

    BPMResult detect(const AudioTrack& track);

    void setMLWeight(double weight) { mlWeight_ = weight; }

private:
    std::unique_ptr<BPMDetector> classicDetector_;
    std::unique_ptr<MLBPMDetector> mlDetector_;
    double mlWeight_ = 0.6;
};

} // namespace BeatMate::Core
