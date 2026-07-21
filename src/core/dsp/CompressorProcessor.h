#pragma once

#include "DSPProcessor.h"
#include <atomic>

namespace BeatMate::Core {

class CompressorProcessor : public DSPProcessor {
public:
    CompressorProcessor();
    ~CompressorProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Compressor"; }

    void setThreshold(float dB);     // -60 to 0 dB
    void setRatio(float ratio);      // 1:1 to 20:1
    void setAttack(float ms);        // 0.1 to 200 ms
    void setRelease(float ms);       // 5 to 2000 ms
    void setMakeupGain(float dB);    // 0 to 40 dB
    void setKnee(float dB);          // 0 to 20 dB (soft knee width)

    // Pilote tous les paramètres ensemble depuis une seule valeur 0..1.
    void setMixAmount(float amount0to1) noexcept;

    // Bypass thread-safe (pour bypass complet sans relancer process).
    void setBypassed(bool b) noexcept { bypassed_.store(b); }
    bool isBypassed() const noexcept { return bypassed_.load(); }

    float getThreshold() const { return threshold_.load(); }
    float getRatio() const { return ratio_.load(); }
    float getAttack() const { return attack_.load(); }
    float getRelease() const { return release_.load(); }
    float getMakeupGain() const { return makeupGain_.load(); }
    float getKnee() const { return knee_.load(); }

    // Current gain reduction in dB (for metering)
    float getGainReduction() const { return gainReduction_.load(); }

private:
    float computeGain(float inputLevel_dB) const;

    std::atomic<float> threshold_{-20.0f};
    std::atomic<float> ratio_{4.0f};
    std::atomic<float> attack_{10.0f};
    std::atomic<float> release_{100.0f};
    std::atomic<float> makeupGain_{0.0f};
    std::atomic<float> knee_{6.0f};

    std::atomic<float> gainReduction_{0.0f};

    std::atomic<bool> bypassed_{false};

    float envelope_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
};

} // namespace BeatMate::Core
