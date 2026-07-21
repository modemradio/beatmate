#include "MixPointLinkService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include "EnergyAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

MixPointLinkService::MixPointLinkService() = default;
MixPointLinkService::~MixPointLinkService() = default;

std::vector<double> MixPointLinkService::findEnergyDropPoints(const std::vector<float>& energy,
                                                                double segDuration) {
    std::vector<double> points;
    int windowSize = 4;

    for (int i = windowSize; i < static_cast<int>(energy.size()) - windowSize; ++i) {
        float before = 0.0f, after = 0.0f;
        for (int j = i - windowSize; j < i; ++j) before += energy[j];
        for (int j = i; j < i + windowSize; ++j) after += energy[j];
        before /= windowSize;
        after /= windowSize;

        if (before > 0.5f && after < before * 0.6f) {
            points.push_back(i * segDuration);
        }
    }

    // Always include the last 25% of the track as potential mix-out
    double trackEnd = energy.size() * segDuration;
    points.push_back(trackEnd * 0.75);
    points.push_back(trackEnd * 0.85);

    return points;
}

std::vector<double> MixPointLinkService::findEnergyRisePoints(const std::vector<float>& energy,
                                                                double segDuration) {
    std::vector<double> points;
    int windowSize = 4;

    // Include the first 25% of track as potential mix-in
    points.push_back(0.0);
    double trackEnd = energy.size() * segDuration;
    points.push_back(trackEnd * 0.05);
    points.push_back(trackEnd * 0.15);

    for (int i = windowSize; i < static_cast<int>(energy.size()) - windowSize; ++i) {
        float before = 0.0f, after = 0.0f;
        for (int j = i - windowSize; j < i; ++j) before += energy[j];
        for (int j = i; j < i + windowSize; ++j) after += energy[j];
        before /= windowSize;
        after /= windowSize;

        if (after > 0.5f && after > before * 1.5f) {
            points.push_back(i * segDuration);
        }
    }

    return points;
}

float MixPointLinkService::scoreMixPoint(const std::vector<float>& energyA, double outPoint,
                                           const std::vector<float>& energyB, double inPoint,
                                           double segDuration, double bpmA, double bpmB) {
    float score = 50.0f;

    double bpmDiff = std::fabs(bpmA - bpmB);
    if (bpmDiff < 1.0) score += 25.0f;
    else if (bpmDiff < 3.0) score += 15.0f;
    else if (bpmDiff < 6.0) score += 5.0f;
    else score -= 20.0f;

    int outIdx = static_cast<int>(outPoint / segDuration);
    int inIdx = static_cast<int>(inPoint / segDuration);

    if (outIdx >= 0 && outIdx < static_cast<int>(energyA.size()) &&
        inIdx >= 0 && inIdx < static_cast<int>(energyB.size())) {
        float eA = energyA[outIdx];
        float eB = energyB[inIdx];

        // Ideal: A is fading down, B is building up
        if (eA > 0.3f && eA < 0.7f && eB < 0.5f) score += 15.0f;
        // Both at low energy = good for smooth mix
        if (eA < 0.3f && eB < 0.3f) score += 10.0f;
    }

    double trackADuration = energyA.size() * segDuration;
    double relOutPos = outPoint / trackADuration;
    if (relOutPos > 0.7) score += 10.0f;
    if (relOutPos > 0.85) score += 5.0f;

    double trackBDuration = energyB.size() * segDuration;
    double relInPos = inPoint / trackBDuration;
    if (relInPos < 0.2) score += 10.0f;
    if (relInPos < 0.1) score += 5.0f;

    return std::clamp(score, 0.0f, 100.0f);
}

MixPointResult MixPointLinkService::findMixPoints(const AudioTrack& trackA, double bpmA,
                                                     const std::vector<float>& energyA,
                                                     const AudioTrack& trackB, double bpmB,
                                                     const std::vector<float>& energyB) {
    spdlog::info("MixPointLinkService: finding mix points");

    MixPointResult result;
    result.bpmA = bpmA;
    result.bpmB = bpmB;

    double segDuration = 0.5;

    auto outPoints = findEnergyDropPoints(energyA, segDuration);
    auto inPoints = findEnergyRisePoints(energyB, segDuration);

    double barDuration = 240.0 / ((bpmA + bpmB) / 2.0);
    double mixDuration = preferredMixBars_ * barDuration;

    for (double outPt : outPoints) {
        for (double inPt : inPoints) {
            float score = scoreMixPoint(energyA, outPt, energyB, inPt, segDuration, bpmA, bpmB);

            if (score >= minScore_) {
                MixPoint mp;
                mp.outPoint = outPt;
                mp.inPoint = inPt;
                mp.mixDuration = mixDuration;
                mp.mixBars = preferredMixBars_;
                mp.score = score;

                int outIdx = static_cast<int>(outPt / segDuration);
                if (outIdx < static_cast<int>(energyA.size()) && energyA[outIdx] < 0.3f) {
                    mp.type = "breakdown";
                    mp.description = "Mix during breakdown";
                } else if (outPt > energyA.size() * segDuration * 0.85) {
                    mp.type = "outro-intro";
                    mp.description = "Classic outro-to-intro transition";
                } else {
                    mp.type = "energy";
                    mp.description = "Energy-based mix point";
                }

                result.candidates.push_back(mp);
            }
        }
    }

    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const MixPoint& a, const MixPoint& b) { return a.score > b.score; });

    if (!result.candidates.empty()) {
        result.bestMatch = result.candidates[0];
        result.overallScore = result.bestMatch.score;
    }

    spdlog::info("MixPointLinkService: {} candidates, best score={:.0f} ({})",
                 result.candidates.size(), result.overallScore,
                 result.bestMatch.type);
    return result;
}

MixPointResult MixPointLinkService::findMixPoints(const AudioTrack& trackA, const AudioTrack& trackB) {
    BPMDetector bpmDet;
    EnergyAnalyzer energyDet;

    auto bpmResultA = bpmDet.detect(trackA);
    auto bpmResultB = bpmDet.detect(trackB);
    auto energyResultA = energyDet.analyze(trackA, 0.5);
    auto energyResultB = energyDet.analyze(trackB, 0.5);

    return findMixPoints(trackA, bpmResultA.bpm, energyResultA.curve,
                          trackB, bpmResultB.bpm, energyResultB.curve);
}

} // namespace BeatMate::Core
