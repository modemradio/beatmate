#pragma once
#include <string>
#include <vector>
#include "../../core/audio/AudioTrack.h"

namespace BeatMate::Services::Normalization {

enum class LoudnessAlgorithm {
    BS1770,         // ITU-R BS.1770-4 (standard LUFS)
    ReplayGain2,    // ReplayGain 2.0
    EBU_R128,       // EBU R128 (based on BS.1770 with gating)
    K_System,       // Bob Katz K-System metering
    SimpleRMS,      // Basic RMS normalization
    PeakNormalize   // True peak normalization
};

struct LoudnessProfile {
    float integratedLUFS = -100.0f;
    float shortTermMax = -100.0f;
    float momentaryMax = -100.0f;
    float truePeak = -100.0f;
    float loudnessRange = 0.0f;        // LRA in LU
    float dynamicRange = 0.0f;         // DR score
    float replayGainDb = 0.0f;
    float replayGainPeak = 0.0f;

    std::vector<float> channelLUFS;
    std::vector<float> channelPeak;

    std::vector<float> momentaryLUFSHistory;    // 100ms resolution
    std::vector<float> shortTermLUFSHistory;    // 1s resolution
};

struct MultiAlgoOptions {
    LoudnessAlgorithm primaryAlgorithm = LoudnessAlgorithm::EBU_R128;
    float targetLoudness = -14.0f;
    float maxTruePeakDbTP = -1.0f;
    bool computeAllAlgorithms = false;
    bool generateHistory = true;
    int historyResolutionMs = 100;
};

struct MultiAlgoResult {
    bool success = false;
    LoudnessProfile profile;
    float appliedGainDb = 0.0f;

    float bs1770LUFS = -100.0f;
    float replayGainDb = 0.0f;
    float ebuR128LUFS = -100.0f;
    float kSystemLevel = -100.0f;
    float rmsDb = -100.0f;
    float peakDb = -100.0f;
};

class LoudnessNormalisationService {
public:
    LoudnessNormalisationService() = default;
    ~LoudnessNormalisationService() = default;

    LoudnessProfile analyzeFullProfile(const Core::AudioTrack& track) const;
    MultiAlgoResult analyzeMultiAlgorithm(const Core::AudioTrack& track,
                                            const MultiAlgoOptions& options) const;

    MultiAlgoResult normalize(Core::AudioTrack& track, const MultiAlgoOptions& options);

    float measureBS1770(const Core::AudioTrack& track) const;
    float measureReplayGain(const Core::AudioTrack& track) const;
    float measureEBU_R128(const Core::AudioTrack& track) const;
    float measureKSystem(const Core::AudioTrack& track, int kLevel = 14) const;

    float measureDynamicRange(const Core::AudioTrack& track) const;
    float measureCrestFactor(const Core::AudioTrack& track) const;

    std::vector<float> getMomentaryLUFSHistory(const Core::AudioTrack& track,
                                                 int resolutionMs = 100) const;
    std::vector<float> getShortTermLUFSHistory(const Core::AudioTrack& track,
                                                 int resolutionMs = 1000) const;

    static std::string algorithmName(LoudnessAlgorithm algo);
    static float luToDb(float lu);
    static float dbToLu(float db);

private:
    float applyKWeighting(float sample, int sampleRate, int channel) const;
    std::vector<float> kWeightFilter(const std::vector<float>& samples, int sampleRate,
                                       int channels) const;
};

} // namespace BeatMate::Services::Normalization
