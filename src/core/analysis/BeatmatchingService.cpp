#include "BeatmatchingService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include "KeyDetector.h"
#include "EnergyAnalyzer.h"
#include "KeyFormatsService.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BeatmatchingService::BeatmatchingService() = default;
BeatmatchingService::~BeatmatchingService() = default;

float BeatmatchingService::computeTempoScore(double bpmA, double bpmB) {
    if (bpmA <= 0 || bpmB <= 0) return 0.0f;

    double diff = std::fabs(bpmA - bpmB);

    double halfDiff = std::fabs(bpmA - bpmB * 2.0);
    double doubleDiff = std::fabs(bpmA * 2.0 - bpmB);
    diff = std::min({diff, halfDiff, doubleDiff});

    if (diff < 0.5) return 100.0f;
    if (diff < 1.0) return 95.0f;
    if (diff < 2.0) return 85.0f;
    if (diff < 4.0) return 70.0f;
    if (diff < 6.0) return 50.0f;
    if (diff < maxBPMDiff_) return 30.0f;
    return 10.0f;
}

float BeatmatchingService::computeKeyScore(const std::string& keyA, const std::string& keyB) {
    if (keyA.empty() || keyB.empty()) return 50.0f;

    KeyFormatsService keyService;
    auto infoA = keyService.autoDetect(keyA);
    auto infoB = keyService.autoDetect(keyB);

    int distance = keyService.camelotDistance(infoA, infoB);

    switch (distance) {
        case 0: return 100.0f;
        case 1: return 90.0f;
        case 2: return 70.0f;
        case 3: return 50.0f;
        case 4: return 30.0f;
        case 5: return 15.0f;
        case 6: return 5.0f;
        default: return 10.0f;
    }
}

float BeatmatchingService::computeEnergyScore(float energyA, float energyB) {
    float diff = std::fabs(energyA - energyB);
    return std::max(0.0f, 100.0f - diff * 15.0f);
}

std::string BeatmatchingService::getRecommendation(float score) {
    if (score >= 85.0f) return "Perfect";
    if (score >= 65.0f) return "Good";
    if (score >= 45.0f) return "Acceptable";
    if (score >= 25.0f) return "Risky";
    return "Clash";
}

BeatmatchScore BeatmatchingService::computeScore(double bpmA, const std::string& keyA, float energyA,
                                                    double bpmB, const std::string& keyB, float energyB) {
    BeatmatchScore result;

    result.tempoCompatibility = computeTempoScore(bpmA, bpmB);
    result.keyCompatibility = computeKeyScore(keyA, keyB);
    result.energyCompatibility = computeEnergyScore(energyA, energyB);
    result.rhythmCompatibility = result.tempoCompatibility;

    result.overallScore = result.tempoCompatibility * 0.35f +
                          result.keyCompatibility * 0.35f +
                          result.energyCompatibility * 0.15f +
                          result.rhythmCompatibility * 0.15f;

    result.bpmDifference = std::fabs(bpmA - bpmB);

    KeyFormatsService keyService;
    auto infoA = keyService.autoDetect(keyA);
    auto infoB = keyService.autoDetect(keyB);
    result.camelotDistance = keyService.camelotDistance(infoA, infoB);

    if (result.keyCompatibility < 70.0f && result.tempoCompatibility > 70.0f) {
        int pitchDiff = (infoA.pitchClass - infoB.pitchClass + 12) % 12;
        if (pitchDiff > 6) pitchDiff -= 12;
        result.suggestedPitchShift = static_cast<double>(pitchDiff);
    }

    result.recommendation = getRecommendation(result.overallScore);

    spdlog::info("BeatmatchingService: score={:.0f} (tempo={:.0f}, key={:.0f}, energy={:.0f}) - {}",
                 result.overallScore, result.tempoCompatibility, result.keyCompatibility,
                 result.energyCompatibility, result.recommendation);
    return result;
}

BeatmatchScore BeatmatchingService::computeScore(const AudioTrack& trackA, const AudioTrack& trackB) {
    spdlog::info("BeatmatchingService: analyzing pair");

    BPMDetector bpmDet;
    KeyDetector keyDet;
    EnergyAnalyzer energyDet;

    auto bpmA = bpmDet.detect(trackA);
    auto bpmB = bpmDet.detect(trackB);
    auto keyA = keyDet.detect(trackA);
    auto keyB = keyDet.detect(trackB);
    auto energyA = energyDet.analyze(trackA);
    auto energyB = energyDet.analyze(trackB);

    return computeScore(bpmA.bpm, keyA.key, static_cast<float>(energyA.overall),
                        bpmB.bpm, keyB.key, static_cast<float>(energyB.overall));
}

} // namespace BeatMate::Core
