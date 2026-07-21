#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "StemPlaybackRouter.h"

namespace BeatMate::Core {

enum class StemTransitionType {
    VocalBridge,        // Fade out vocals of A, fade in vocals of B
    InstrumentalSwap,   // Swap instrumentals while keeping vocals
    DrumsFirst,         // Bring in drums of B first, then rest
    BassSwap,           // Swap bass lines
    FullCrossfade,      // Equal-power crossfade on all stems
    EchoOut,            // Echo-out effect on outgoing stems
    FilterSweep,        // Low-pass filter sweep
    AcapellaIntro,      // Start with vocals only of B
    DropSwap,           // Cut all, drop in B
    Custom              // User-defined per-stem fade curves
};

struct StemFadeCurve {
    StemType stem;
    double fadeOutStartBeat = 0.0;
    double fadeOutEndBeat = 0.0;
    double fadeInStartBeat = 0.0;
    double fadeInEndBeat = 0.0;
    bool useEqualPower = true;
};

struct StemTransitionConfig {
    StemTransitionType type = StemTransitionType::FullCrossfade;
    double transitionLengthBeats = 16.0;
    double bpm = 0.0;
    std::vector<StemFadeCurve> customCurves;
    bool autoAlignBeats = true;
    float echoFeedback = 0.5f;
    float filterCutoff = 1.0f;
};

using TransitionProgressCallback = std::function<void(double progress, const std::string& phase)>;

class StemTransitionService {
public:
    StemTransitionService();
    ~StemTransitionService() = default;

    void setTransitionConfig(const StemTransitionConfig& config);
    StemTransitionConfig getTransitionConfig() const { return config_; }

    void startTransition(StemPlaybackRouter& routerA, StemPlaybackRouter& routerB,
                          TransitionProgressCallback callback = nullptr, double currentBeat = 0.0);
    void updateTransition(double currentBeat);
    void cancelTransition();
    bool isTransitioning() const { return isActive_; }
    double getTransitionProgress() const;

    void processTransitionBlock(StemPlaybackRouter& routerA, StemPlaybackRouter& routerB,
                                  float* output, int numFrames, int numChannels,
                                  int64_t startFrame, double currentBeat);

    // Pre-allocates scratch buffers so processTransitionBlock never allocates on the audio thread.
    void setBufferSize(int frames, int channels);

    void setSampleRate(double sr) { sampleRate_ = (sr > 0.0) ? sr : 44100.0; }

    static StemTransitionConfig getPreset(StemTransitionType type, double bpm,
                                           double lengthBeats = 16.0);
    static std::vector<StemFadeCurve> generateCurves(StemTransitionType type, double lengthBeats);
    static std::string transitionTypeName(StemTransitionType type);

private:
    float computeFadeGain(double currentBeat, double startBeat, double endBeat, bool fadeIn,
                           bool equalPower) const;

    StemTransitionConfig config_;
    bool isActive_ = false;
    double transitionStartBeat_ = 0.0;
    std::atomic<double> lastProgress_{0.0};
    TransitionProgressCallback progressCallback_;

    // Scratch buffers pre-sized by setBufferSize — no audio-thread allocation.
    std::vector<float> scratchA_;
    std::vector<float> scratchB_;
    int scratchFrames_   = 0;
    int scratchChannels_ = 0;
    double sampleRate_   = 44100.0;
};

} // namespace BeatMate::Core
