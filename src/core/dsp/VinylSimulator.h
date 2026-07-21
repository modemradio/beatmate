#pragma once

#include "DSPProcessor.h"
#include <atomic>
#include <random>
#include <vector>

namespace BeatMate::Core {

class VinylSimulator : public DSPProcessor {
public:
    VinylSimulator();
    ~VinylSimulator() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Vinyl Simulator"; }

    void setWow(float amount);        // 0-1 slow pitch variation
    void setFlutter(float amount);    // 0-1 fast pitch variation
    void setCrackle(float amount);    // 0-1 random noise pops
    void setSurfaceNoise(float amount); // 0-1 hiss
    void setRPM(int rpm);            // 33 or 45

    float getWow() const { return wow_.load(); }
    float getFlutter() const { return flutter_.load(); }
    float getCrackle() const { return crackle_.load(); }
    float getSurfaceNoise() const { return surfaceNoise_.load(); }

private:
    std::atomic<float> wow_{0.3f};
    std::atomic<float> flutter_{0.2f};
    std::atomic<float> crackle_{0.3f};
    std::atomic<float> surfaceNoise_{0.2f};
    int rpm_ = 33;

    double wowPhase_ = 0.0;
    double flutterPhase_ = 0.0;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_{0.0f, 1.0f};

    // Simple LP filter for surface noise shaping
    float noiseFilterState_ = 0.0f;
};

} // namespace BeatMate::Core
