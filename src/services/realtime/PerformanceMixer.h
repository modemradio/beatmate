#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace BeatMate::Core {
class AudioPlayer;
class CrossfadeEngine;
}

namespace BeatMate::Services::Realtime {

class PerformanceMixer {
public:
    PerformanceMixer();
    ~PerformanceMixer();

    PerformanceMixer(const PerformanceMixer&) = delete;
    PerformanceMixer& operator=(const PerformanceMixer&) = delete;

    void setDeckA(Core::AudioPlayer* player) { deckA_ = player; }
    void setDeckB(Core::AudioPlayer* player) { deckB_ = player; }

    // Must be called with the max block size before processBlock runs on the audio thread
    void setBufferSize(int maxFrames, int channels);

    void setSampleRate(double sr) noexcept;

    // UI-thread setters, lock-free, picked up on the next audio block
    void setChannelGain(char channel, float linear);
    void setChannelVolume(char channel, float linear);
    void setCrossfader(float position);
    void setMasterVolume(float linear);

    void setChannelEQ(char channel, float lowDb, float midDb, float highDb) noexcept;
    void setChannelFilter(char channel, float pos) noexcept;

    void setPfl(char channel, bool enabled);
    void setCueVolume(float linear);

    void processBlock(float* output, int numFrames, int channels);

private:
    Core::AudioPlayer* deckA_ = nullptr;
    Core::AudioPlayer* deckB_ = nullptr;

    std::atomic<float> gainA_{1.0f}, gainB_{1.0f};
    std::atomic<float> volA_{1.0f},  volB_{1.0f};
    std::atomic<float> crossfader_{0.0f};
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<bool>  pflA_{false}, pflB_{false};
    std::atomic<float> cueVolume_{0.8f};

    std::atomic<float> eqLowA_{0.0f}, eqMidA_{0.0f}, eqHighA_{0.0f};
    std::atomic<float> eqLowB_{0.0f}, eqMidB_{0.0f}, eqHighB_{0.0f};
    std::atomic<float> filterA_{0.0f}, filterB_{0.0f};

    std::vector<float> scratchA_;
    std::vector<float> scratchB_;
    int maxFrames_  = 0;
    int channels_   = 2;
    double sampleRate_ = 44100.0;

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1L = 0, z2L = 0, z1R = 0, z2R = 0;
        inline float process(float x, float& z1, float& z2) noexcept {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct OnePole {
        float a = 1.0f;
        float zL = 0, zR = 0;
    };

    struct ChannelDSP {
        Biquad lowShelf;
        Biquad midPeak;
        Biquad highShelf;
        OnePole lp;
        OnePole hp;
        float lastLowDb  = 1e9f;
        float lastMidDb  = 1e9f;
        float lastHighDb = 1e9f;
        float lastFilterPos = 1e9f;
        double lastSr = 0.0;
    };
    ChannelDSP dspA_;
    ChannelDSP dspB_;

    // Per-block line-gain smoothing (anti zipper) ; audio thread only
    float prevLineA_ = 0.0f;
    float prevLineB_ = 0.0f;

    void updateChannelCoeffs(ChannelDSP& dsp, float lowDb, float midDb,
                             float highDb, float filterPos) noexcept;
    void processChannelDSP(ChannelDSP& dsp, float* buf, int numFrames,
                           int channels, float filterPos) noexcept;
};

}
