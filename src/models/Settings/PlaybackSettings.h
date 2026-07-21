#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

enum class CrossfadeCurve : int {
    Linear = 0,
    EqualPower = 1,
    SCurve = 2,
    FastCut = 3,
    SlowFade = 4,
    ConstantPower = 5
};

NLOHMANN_JSON_SERIALIZE_ENUM(CrossfadeCurve, {
    { CrossfadeCurve::Linear, "Linear" },
    { CrossfadeCurve::EqualPower, "EqualPower" },
    { CrossfadeCurve::SCurve, "SCurve" },
    { CrossfadeCurve::FastCut, "FastCut" },
    { CrossfadeCurve::SlowFade, "SlowFade" },
    { CrossfadeCurve::ConstantPower, "ConstantPower" }
})

struct PlaybackSettings {
    std::string defaultDevice;
    std::string previewDevice;
    int bufferSize = 512;               // samples (64, 128, 256, 512, 1024, 2048)
    int sampleRate = 44100;             // Hz (44100, 48000, 96000)
    int bitDepth = 24;                  // bits (16, 24, 32)

    double crossfadeDuration = 8.0;     // seconds
    CrossfadeCurve crossfadeCurve = CrossfadeCurve::EqualPower;
    bool autoCrossfade = false;

    bool autoPlay = false;
    bool autoQueue = false;
    bool repeatMode = false;
    bool shuffleMode = false;
    bool gaplessPlayback = true;

    float masterVolume = 1.0f;          // 0-1
    float previewVolume = 1.0f;         // 0-1
    float headroomDb = -3.0f;           // headroom in dB

    bool normalizeVolume = false;
    float targetLoudness = -14.0f;      // LUFS
    bool useReplayGain = true;

    bool keyLock = false;
    std::string keyLockAlgorithm = "elastique"; // "elastique", "soundtouch", "rubberband"

    double tempoRange = 8.0;            // +/- percentage for tempo fader
    bool syncTempo = false;
    bool quantizeEnabled = true;
    int quantizeResolution = 4;         // beats (1, 2, 4, 8, 16)

    bool masterLimiter = true;
    float limiterThreshold = -0.3f;     // dBFS

    bool cuePauseAtPoint = true;
    bool cueFlashOnBeat = true;

    bool autoGain = true;
    float autoGainTarget = -14.0f;      // LUFS

    double outputLatencyMs = 0.0;

    PlaybackSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlaybackSettings,
        defaultDevice, previewDevice, bufferSize, sampleRate, bitDepth,
        crossfadeDuration, crossfadeCurve, autoCrossfade,
        autoPlay, autoQueue, repeatMode, shuffleMode, gaplessPlayback,
        masterVolume, previewVolume, headroomDb,
        normalizeVolume, targetLoudness, useReplayGain,
        keyLock, keyLockAlgorithm,
        tempoRange, syncTempo, quantizeEnabled, quantizeResolution,
        masterLimiter, limiterThreshold,
        cuePauseAtPoint, cueFlashOnBeat,
        autoGain, autoGainTarget,
        outputLatencyMs
    )
};

} // namespace BeatMate::Models::Settings
