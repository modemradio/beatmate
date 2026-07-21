#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../../core/audio/AudioTrack.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Normalization {

enum class NormalizationPreset {
    Spotify,      // -14 LUFS
    AppleMusic,   // -16 LUFS
    YouTubeMusic, // -14 LUFS
    Tidal,        // -14 LUFS
    Club,         // -11 LUFS
    Broadcast,    // -23 LUFS (EBU R128)
    Podcast,      // -16 LUFS
    Custom
};

enum class NormalizationMode {
    Peak,         // Target peak level
    RMS,          // Target RMS level
    LUFS,         // ITU-R BS.1770 loudness
    ReplayGain    // ReplayGain standard
};

struct NormalizationOptions {
    NormalizationMode mode = NormalizationMode::LUFS;
    float targetLUFS = -14.0f;
    float targetPeakDb = -1.0f;
    float targetRMSDb = -18.0f;
    float maxTruePeakDb = -1.0f;    // True peak limiter
    bool useLimiter = true;
    float limiterThreshold = -0.3f;
    float limiterRelease = 50.0f;   // ms
    bool applyDCOffset = true;
};

struct NormalizationResult {
    bool success = false;
    float originalLUFS = 0.0f;
    float originalPeak = 0.0f;
    float originalRMS = 0.0f;
    float appliedGainDb = 0.0f;
    float resultingLUFS = 0.0f;
    float resultingPeak = 0.0f;
    float resultingTruePeak = 0.0f;
    bool limiterEngaged = false;
    float loudnessRange = 0.0f;     // LRA
};

using NormalizationProgressCallback = std::function<void(float progress, const std::string& stage)>;

class NormalizationService {
public:
    NormalizationService() = default;
    ~NormalizationService() = default;

    NormalizationResult normalize(Core::AudioTrack& track, const NormalizationOptions& options,
                                    NormalizationProgressCallback progress = nullptr);
    NormalizationResult normalizeWithPreset(Core::AudioTrack& track, NormalizationPreset preset,
                                              NormalizationProgressCallback progress = nullptr);

    std::vector<NormalizationResult> normalizeBatch(std::vector<Core::AudioTrack*>& tracks,
                                                      const NormalizationOptions& options,
                                                      NormalizationProgressCallback progress = nullptr);

    NormalizationResult analyzeTrack(const Core::AudioTrack& track) const;
    float measureIntegratedLUFS(const Core::AudioTrack& track) const;
    float measureShortTermLUFS(const Core::AudioTrack& track, double positionSeconds) const;
    float measureMomentaryLUFS(const Core::AudioTrack& track, double positionSeconds) const;
    float measureTruePeak(const Core::AudioTrack& track) const;
    float measureLoudnessRange(const Core::AudioTrack& track) const;

    static NormalizationOptions getPresetOptions(NormalizationPreset preset);
    static std::string presetName(NormalizationPreset preset);
    static float presetTargetLUFS(NormalizationPreset preset);

private:
    void removeDCOffset(std::vector<float>& samples, int channels);
    void applyGain(std::vector<float>& samples, float gainLinear);
    void applyLimiter(std::vector<float>& samples, float thresholdDb, float releaseMs,
                       int sampleRate, bool& limiterEngaged);
};

} // namespace BeatMate::Services::Normalization
