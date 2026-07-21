#pragma once

#include "DSPProcessor.h"
#include <array>
#include <atomic>

namespace BeatMate::Core {

class PhaserProcessor : public DSPProcessor {
public:
    PhaserProcessor();
    ~PhaserProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Phaser"; }

    void setRate(float hz);
    void setDepth(float depth);
    void setFeedback(float feedback);
    void setStages(int stages); // 2, 4, 6, 8, 10, 12

    float getRate() const { return rate_.load(); }
    float getDepth() const { return depth_.load(); }
    float getFeedback() const { return feedback_.load(); }
    int getStages() const { return stages_; }

private:
    std::atomic<float> rate_{0.5f};
    std::atomic<float> depth_{0.5f};
    std::atomic<float> feedback_{0.5f};
    int stages_ = 6;

    static constexpr int kMaxStages = 12;

    struct AllPassState {
        float y1 = 0.0f;
    };
    std::array<AllPassState, kMaxStages> statesL_{};
    std::array<AllPassState, kMaxStages> statesR_{};

    double lfoPhase_ = 0.0;
    float lastOutputL_ = 0.0f;
    float lastOutputR_ = 0.0f;
};

} // namespace BeatMate::Core
