#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../audio/AudioTrack.h"

namespace BeatMate::Core {

enum class StemType { Vocals = 0, Drums = 1, Bass = 2, Other = 3, Melody = 4, Count = 5 };

struct StemState {
    bool muted = false;
    bool soloed = false;
    float volume = 1.0f;
    float pan = 0.0f;           // -1..1
    float lowEQ = 0.0f;        // dB
    float midEQ = 0.0f;
    float highEQ = 0.0f;
    bool bypassEQ = false;
};

using StemStateCallback = std::function<void(StemType, const StemState&)>;

class StemPlaybackRouter {
public:
    StemPlaybackRouter();
    ~StemPlaybackRouter() = default;

    void loadStem(StemType type, std::shared_ptr<AudioTrack> audio);
    void unloadStem(StemType type);
    void unloadAll();
    bool hasStem(StemType type) const;
    int getLoadedStemCount() const;

    void setStemMuted(StemType type, bool muted);
    void setStemSoloed(StemType type, bool soloed);
    void setStemVolume(StemType type, float volume);
    void setStemPan(StemType type, float pan);
    void setStemEQ(StemType type, float low, float mid, float high);
    void setBypassEQ(StemType type, bool bypass);
    void resetStem(StemType type);
    void resetAll();

    StemState getStemState(StemType type) const;
    bool isMuted(StemType type) const;
    bool isSoloed(StemType type) const;
    bool hasAnySoloed() const;

    // Audio processing - call from audio thread
    void processBlock(float* outputBuffer, int numFrames, int numChannels,
                       int64_t startFrame);
    void getStemmixSamples(float* dest, int numFrames, int numChannels, int64_t startFrame);

    void setVocalsOnly();
    void setInstrumentalOnly();
    void setDrumsAndBassOnly();
    void setAcapella();
    void setKaraoke();

    void registerCallback(StemStateCallback callback);
    static std::string stemTypeName(StemType type);

private:
    float computeStemGain(StemType type) const;
    void applyEQ(float& sample, StemType type, bool isLeft) const;
    void notifyCallbacks(StemType type);

    mutable std::mutex mutex_;
    std::array<std::shared_ptr<AudioTrack>, static_cast<size_t>(StemType::Count)> stems_;
    std::array<StemState, static_cast<size_t>(StemType::Count)> states_;
    std::vector<StemStateCallback> callbacks_;
};

} // namespace BeatMate::Core
