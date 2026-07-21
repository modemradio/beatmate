#pragma once

#include "StructureDetector.h"
#include <vector>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct SelfSimilarityResult {
    std::vector<std::vector<float>> matrix;  // NxN self-similarity matrix
    std::vector<float> noveltyCurve;
    std::vector<double> boundaries;          // Section boundaries in seconds
    std::vector<Section> sections;
    int resolution = 0;                      // Frames in matrix
    double hopDuration = 0.0;                // Seconds per frame
};

class AdvancedStructuralAnalysisService {
public:
    AdvancedStructuralAnalysisService();
    ~AdvancedStructuralAnalysisService();

    SelfSimilarityResult analyze(const AudioTrack& track);

    void setFeatureType(int type) { featureType_ = type; } // 0=MFCC, 1=chroma, 2=combined
    void setResolution(double hopSeconds) { hopSeconds_ = hopSeconds; }
    void setKernelSize(int size) { kernelSize_ = size; }

private:
    std::vector<std::vector<float>> extractFeatures(const AudioTrack& track);

    std::vector<std::vector<float>> computeSSM(const std::vector<std::vector<float>>& features);

    void enhanceSSM(std::vector<std::vector<float>>& ssm);

    std::vector<float> computeNoveltyCurve(const std::vector<std::vector<float>>& ssm);

    std::vector<double> findBoundaries(const std::vector<float>& novelty, double hopDuration);

    std::vector<Section> classifySections(const std::vector<std::vector<float>>& ssm,
                                           const std::vector<double>& boundaries,
                                           const AudioTrack& track,
                                           const std::vector<float>& novelty,
                                           double frameHop);

    int featureType_ = 2;      // Combined features
    double hopSeconds_ = 0.5;  // 500ms resolution
    int kernelSize_ = 16;      // Checkerboard kernel size
};

} // namespace BeatMate::Core
