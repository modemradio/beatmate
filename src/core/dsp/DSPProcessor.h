#pragma once

#include <atomic>
#include <string>

namespace BeatMate::Core {

class DSPProcessor {
public:
    DSPProcessor() = default;
    virtual ~DSPProcessor() = default;

    virtual void process(float* buffer, int numSamples, int channels) = 0;

    virtual void reset() {}

    virtual std::string getName() const = 0;

    void setBypass(bool bypass) { bypassed_.store(bypass); }
    bool isBypassed() const { return bypassed_.load(); }

    // 0.0 = fully dry, 1.0 = fully wet.
    void setWetDry(float mix) { wetDry_.store(mix); }
    float getWetDry() const { return wetDry_.load(); }

    // Call before processing.
    void setSampleRate(double sr) { sampleRate_ = sr; onSampleRateChanged(); }
    double getSampleRate() const { return sampleRate_; }

    void processWithMix(float* buffer, int numSamples, int channels);

protected:
    virtual void onSampleRateChanged() {}

    double sampleRate_ = 44100.0;

private:
    std::atomic<bool> bypassed_{false};
    std::atomic<float> wetDry_{1.0f};
};

} // namespace BeatMate::Core
