#include "StemTransitionService.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

StemTransitionService::StemTransitionService() = default;

void StemTransitionService::setTransitionConfig(const StemTransitionConfig& config) {
    config_ = config;
    if (config_.customCurves.empty()) {
        config_.customCurves = generateCurves(config_.type, config_.transitionLengthBeats);
    }
}

void StemTransitionService::startTransition(StemPlaybackRouter& /*routerA*/,
                                              StemPlaybackRouter& /*routerB*/,
                                              TransitionProgressCallback callback, double currentBeat) {
    if (config_.bpm <= 0.0) {
        spdlog::warn("StemTransitionService: refusing transition without a valid BPM");
        return;
    }
    isActive_ = true;
    transitionStartBeat_ = currentBeat;
    lastProgress_.store(0.0);
    progressCallback_ = std::move(callback);

    if (config_.customCurves.empty()) {
        config_.customCurves = generateCurves(config_.type, config_.transitionLengthBeats);
    }

    spdlog::info("StemTransitionService: Starting {} transition ({} beats at {} BPM)",
                 transitionTypeName(config_.type), config_.transitionLengthBeats, config_.bpm);

    if (progressCallback_) progressCallback_(0.0, "Starting transition");
}

void StemTransitionService::updateTransition(double currentBeat) {
    if (!isActive_) return;

    double progress = (currentBeat - transitionStartBeat_) / config_.transitionLengthBeats;
    progress = std::clamp(progress, 0.0, 1.0);
    lastProgress_.store(progress);

    if (progressCallback_) {
        std::string phase = (progress < 0.25) ? "Intro" :
                            (progress < 0.75) ? "Crossfading" : "Outro";
        progressCallback_(progress, phase);
    }

    if (progress >= 1.0) {
        isActive_ = false;
        spdlog::info("StemTransitionService: Transition complete");
        if (progressCallback_) progressCallback_(1.0, "Complete");
    }
}

void StemTransitionService::cancelTransition() {
    isActive_ = false;
    spdlog::info("StemTransitionService: Transition cancelled");
}

double StemTransitionService::getTransitionProgress() const {
    return lastProgress_.load();
}

void StemTransitionService::setBufferSize(int frames, int channels) {
    if (frames <= 0)   frames   = 1;
    if (channels <= 0) channels = 2;
    if (frames <= scratchFrames_ && channels == scratchChannels_) return;
    scratchFrames_   = std::max(frames, scratchFrames_);
    scratchChannels_ = channels;
    const size_t needed = static_cast<size_t>(scratchFrames_) *
                          static_cast<size_t>(scratchChannels_);
    scratchA_.assign(needed, 0.0f);
    scratchB_.assign(needed, 0.0f);
}

void StemTransitionService::processTransitionBlock(StemPlaybackRouter& routerA,
                                                     StemPlaybackRouter& routerB,
                                                     float* output, int numFrames, int numChannels,
                                                     int64_t startFrame, double currentBeat) {
    if (!isActive_) {
        routerA.processBlock(output, numFrames, numChannels, startFrame);
        return;
    }

    // Les buffers scratch doivent couvrir ce bloc (préparés par setBufferSize).
    if (numFrames > scratchFrames_ || numChannels != scratchChannels_) {
        setBufferSize(numFrames, numChannels);
    }

    const int samples = numFrames * numChannels;
    std::fill_n(scratchA_.data(), samples, 0.0f);
    std::fill_n(scratchB_.data(), samples, 0.0f);

    routerA.processBlock(scratchA_.data(), numFrames, numChannels, startFrame);
    routerB.processBlock(scratchB_.data(), numFrames, numChannels, startFrame);

    const double sr = (sampleRate_ > 0.0) ? sampleRate_ : 44100.0;
    const double beatsPerSample = config_.bpm / 60.0 / sr;

    for (int f = 0; f < numFrames; ++f) {
        double beat = currentBeat + f * beatsPerSample;
        double relBeat = beat - transitionStartBeat_;

        // Aggregate per-stem gains into a SINGLE pair (fadeOut for A, fadeIn for B)
        float fadeOutSum = 0.0f;
        float fadeInSum  = 0.0f;
        const int curveCount = static_cast<int>(config_.customCurves.size());
        if (curveCount > 0) {
            for (const auto& curve : config_.customCurves) {
                fadeOutSum += computeFadeGain(relBeat, curve.fadeOutStartBeat, curve.fadeOutEndBeat,
                                              false, curve.useEqualPower);
                fadeInSum  += computeFadeGain(relBeat, curve.fadeInStartBeat, curve.fadeInEndBeat,
                                              true, curve.useEqualPower);
            }
        }
        const float fadeOutGain = (curveCount > 0) ? (fadeOutSum / curveCount) : 1.0f;
        const float fadeInGain  = (curveCount > 0) ? (fadeInSum  / curveCount) : 0.0f;

        // Apply gains ONCE and mix with soft-clip (tanh) instead of hard-clamp
        for (int ch = 0; ch < numChannels; ++ch) {
            int idx = f * numChannels + ch;
            const float mixed = scratchA_[idx] * fadeOutGain + scratchB_[idx] * fadeInGain;
            output[idx] = std::tanh(mixed);
        }
    }

    updateTransition(currentBeat + numFrames * beatsPerSample);
}


StemTransitionConfig StemTransitionService::getPreset(StemTransitionType type, double bpm,
                                                        double lengthBeats) {
    StemTransitionConfig config;
    config.type = type;
    config.bpm = bpm;
    config.transitionLengthBeats = lengthBeats;
    config.customCurves = generateCurves(type, lengthBeats);
    return config;
}

std::vector<StemFadeCurve> StemTransitionService::generateCurves(StemTransitionType type,
                                                                   double lengthBeats) {
    std::vector<StemFadeCurve> curves;

    auto makeCurve = [](StemType stem, double foStart, double foEnd, double fiStart, double fiEnd) {
        StemFadeCurve c;
        c.stem = stem;
        c.fadeOutStartBeat = foStart;
        c.fadeOutEndBeat = foEnd;
        c.fadeInStartBeat = fiStart;
        c.fadeInEndBeat = fiEnd;
        return c;
    };

    switch (type) {
        case StemTransitionType::VocalBridge:
            curves.push_back(makeCurve(StemType::Vocals, 0, lengthBeats * 0.5, lengthBeats * 0.5, lengthBeats));
            curves.push_back(makeCurve(StemType::Drums, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Bass, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Other, 0, lengthBeats, 0, lengthBeats));
            break;

        case StemTransitionType::InstrumentalSwap:
            curves.push_back(makeCurve(StemType::Vocals, lengthBeats, lengthBeats, 0, 0)); // Keep vocals
            curves.push_back(makeCurve(StemType::Drums, 0, lengthBeats * 0.5, 0, lengthBeats * 0.5));
            curves.push_back(makeCurve(StemType::Bass, lengthBeats * 0.25, lengthBeats * 0.75, lengthBeats * 0.25, lengthBeats * 0.75));
            curves.push_back(makeCurve(StemType::Other, lengthBeats * 0.5, lengthBeats, lengthBeats * 0.5, lengthBeats));
            break;

        case StemTransitionType::DrumsFirst:
            curves.push_back(makeCurve(StemType::Drums, lengthBeats * 0.5, lengthBeats, 0, lengthBeats * 0.25));
            curves.push_back(makeCurve(StemType::Bass, 0, lengthBeats * 0.75, lengthBeats * 0.25, lengthBeats * 0.75));
            curves.push_back(makeCurve(StemType::Vocals, 0, lengthBeats * 0.5, lengthBeats * 0.5, lengthBeats));
            curves.push_back(makeCurve(StemType::Other, 0, lengthBeats * 0.75, lengthBeats * 0.5, lengthBeats));
            break;

        case StemTransitionType::BassSwap:
            curves.push_back(makeCurve(StemType::Bass, 0, lengthBeats * 0.25, lengthBeats * 0.25, lengthBeats * 0.5));
            curves.push_back(makeCurve(StemType::Drums, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Vocals, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Other, 0, lengthBeats, 0, lengthBeats));
            break;

        case StemTransitionType::AcapellaIntro:
            curves.push_back(makeCurve(StemType::Vocals, 0, lengthBeats * 0.5, 0, 0)); // B vocals come in immediately
            curves.push_back(makeCurve(StemType::Drums, 0, lengthBeats * 0.25, lengthBeats * 0.5, lengthBeats));
            curves.push_back(makeCurve(StemType::Bass, 0, lengthBeats * 0.25, lengthBeats * 0.5, lengthBeats));
            curves.push_back(makeCurve(StemType::Other, 0, lengthBeats * 0.25, lengthBeats * 0.5, lengthBeats));
            break;

        case StemTransitionType::DropSwap: {
            double dropBeat = lengthBeats * 0.5;
            curves.push_back(makeCurve(StemType::Vocals, dropBeat - 1, dropBeat, dropBeat, dropBeat + 1));
            curves.push_back(makeCurve(StemType::Drums, dropBeat - 1, dropBeat, dropBeat, dropBeat + 1));
            curves.push_back(makeCurve(StemType::Bass, dropBeat - 1, dropBeat, dropBeat, dropBeat + 1));
            curves.push_back(makeCurve(StemType::Other, dropBeat - 1, dropBeat, dropBeat, dropBeat + 1));
            break;
        }

        case StemTransitionType::FullCrossfade:
        default:
            curves.push_back(makeCurve(StemType::Vocals, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Drums, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Bass, 0, lengthBeats, 0, lengthBeats));
            curves.push_back(makeCurve(StemType::Other, 0, lengthBeats, 0, lengthBeats));
            break;
    }

    return curves;
}

std::string StemTransitionService::transitionTypeName(StemTransitionType type) {
    switch (type) {
        case StemTransitionType::VocalBridge:      return "Vocal Bridge";
        case StemTransitionType::InstrumentalSwap:  return "Instrumental Swap";
        case StemTransitionType::DrumsFirst:        return "Drums First";
        case StemTransitionType::BassSwap:          return "Bass Swap";
        case StemTransitionType::FullCrossfade:     return "Full Crossfade";
        case StemTransitionType::EchoOut:           return "Echo Out";
        case StemTransitionType::FilterSweep:       return "Filter Sweep";
        case StemTransitionType::AcapellaIntro:     return "Acapella Intro";
        case StemTransitionType::DropSwap:          return "Drop Swap";
        case StemTransitionType::Custom:            return "Custom";
    }
    return "Unknown";
}

float StemTransitionService::computeFadeGain(double currentBeat, double startBeat, double endBeat,
                                               bool fadeIn, bool equalPower) const {
    if (endBeat <= startBeat) return fadeIn ? 1.0f : 0.0f;

    double progress = (currentBeat - startBeat) / (endBeat - startBeat);
    progress = std::clamp(progress, 0.0, 1.0);

    float gain;
    if (fadeIn) {
        gain = static_cast<float>(progress);
    } else {
        gain = static_cast<float>(1.0 - progress);
    }

    if (equalPower) {
        gain = std::sin(gain * 3.14159265f * 0.5f);
    }

    return gain;
}

} // namespace BeatMate::Core
