#include "HybridBPMDetector.h"
#include "MLBPMDetector.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

HybridBPMDetector::HybridBPMDetector()
    : classicDetector_(std::make_unique<BPMDetector>()),
      mlDetector_(std::make_unique<MLBPMDetector>()) {
}

HybridBPMDetector::~HybridBPMDetector() = default;

bool HybridBPMDetector::loadMLModel(const std::string& modelPath) {
    return mlDetector_->loadModel(modelPath);
}

BPMResult HybridBPMDetector::detect(const AudioTrack& track) {
    auto classicResult = classicDetector_->detect(track);
    auto mlResult = mlDetector_->detect(track);

    BPMResult result;

    double diff = std::fabs(classicResult.bpm - mlResult.bpm);

    if (diff < 2.0) {
        double totalWeight = (1.0 - mlWeight_) * classicResult.confidence +
                            mlWeight_ * mlResult.confidence;
        if (totalWeight > 0) {
            result.bpm = ((1.0 - mlWeight_) * classicResult.confidence * classicResult.bpm +
                         mlWeight_ * mlResult.confidence * mlResult.bpm) / totalWeight;
        } else {
            result.bpm = (classicResult.bpm + mlResult.bpm) / 2.0;
        }
        result.confidence = std::min(1.0, (classicResult.confidence + mlResult.confidence) / 1.5);
    } else if (diff < 5.0) {
        if (mlResult.confidence > classicResult.confidence && mlDetector_->isModelLoaded()) {
            result.bpm = mlResult.bpm;
            result.confidence = mlResult.confidence * 0.9;
        } else {
            result.bpm = classicResult.bpm;
            result.confidence = classicResult.confidence * 0.9;
        }
    } else {
        double ratio = classicResult.bpm / mlResult.bpm;
        if (std::fabs(ratio - 2.0) < 0.1 || std::fabs(ratio - 0.5) < 0.1) {
            // Octave double/moitié — préférer celui dans la plage DJ
            double bpmA = classicResult.bpm;
            double bpmB = mlResult.bpm;
            bool aInRange = (bpmA >= 100 && bpmA <= 150);
            bool bInRange = (bpmB >= 100 && bpmB <= 150);

            if (aInRange && !bInRange) {
                result.bpm = bpmA;
                result.confidence = classicResult.confidence;
            } else if (bInRange && !aInRange) {
                result.bpm = bpmB;
                result.confidence = mlResult.confidence;
            } else {
                result.bpm = (classicResult.confidence >= mlResult.confidence) ?
                    bpmA : bpmB;
                result.confidence = std::max(classicResult.confidence, mlResult.confidence) * 0.8;
            }
        } else {
            if (classicResult.confidence >= mlResult.confidence) {
                result.bpm = classicResult.bpm;
                result.confidence = classicResult.confidence * 0.7;
            } else {
                result.bpm = mlResult.bpm;
                result.confidence = mlResult.confidence * 0.7;
            }
        }
    }

    // Beats du détecteur classique — issus de la détection d'onsets
    result.beats = classicResult.beats;
    result.offset = classicResult.offset;

    spdlog::info("HybridBPM: classic={:.1f} ml={:.1f} -> final={:.1f} (conf={:.0f}%)",
                 classicResult.bpm, mlResult.bpm, result.bpm, result.confidence * 100);

    return result;
}

} // namespace BeatMate::Core
