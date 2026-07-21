#pragma once

#include "DSPProcessor.h"
#include <array>
#include <atomic>

namespace BeatMate::Core {

enum class FilterType {
    LowPass, HighPass, BandPass, Notch,
    LowShelf, HighShelf, Peak
};

class FilterProcessor : public DSPProcessor {
public:
    FilterProcessor();
    ~FilterProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "Filter"; }

    void setType(FilterType type);
    FilterType getType() const { return type_; }

    void setFrequency(float freq);
    float getFrequency() const { return frequency_.load(); }

    void setQ(float q);
    float getQ() const { return q_.load(); }

    void setGain(float gainDb);
    float getGain() const { return gainDb_.load(); }

    struct Coefficients {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a0 = 1.0, a1 = 0.0, a2 = 0.0;
    };

    Coefficients getCoefficients() const { return coeffs_; }

    void setOrder(int order);
    int getOrder() const { return order_; }

protected:
    void onSampleRateChanged() override { recalculate(); }

private:
    void recalculate();
    void processBiquad(float* buffer, int numSamples, int channels, int stage);

    FilterType type_ = FilterType::LowPass;
    std::atomic<float> frequency_{1000.0f};
    std::atomic<float> q_{0.707f};
    std::atomic<float> gainDb_{0.0f};
    int order_ = 1;

    Coefficients coeffs_;

    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxStages = 4;
    struct BiquadState {
        double x1 = 0, x2 = 0;
        double y1 = 0, y2 = 0;
    };
    std::array<std::array<BiquadState, kMaxStages>, kMaxChannels> state_{};
};

}
