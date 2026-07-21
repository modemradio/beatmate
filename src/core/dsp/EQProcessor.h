#pragma once

#include "DSPProcessor.h"
#include <atomic>
#include <memory>

namespace BeatMate::Core {

class FilterProcessor;

class EQProcessor : public DSPProcessor {
public:
    EQProcessor();
    ~EQProcessor() override;

    void process(float* buffer, int numSamples, int channels) override;
    void reset() override;
    std::string getName() const override { return "3-Band EQ"; }

    // Gain per band in dB (-12 to +12)
    void setLow(float gainDb);
    void setMid(float gainDb);
    void setHigh(float gainDb);

    float getLow() const { return lowGain_.load(); }
    float getMid() const { return midGain_.load(); }
    float getHigh() const { return highGain_.load(); }

    void setLowFreq(float freq);
    void setMidFreq(float freq);
    void setHighFreq(float freq);

    float getLowFreq() const { return lowFreq_.load(); }
    float getMidFreq() const { return midFreq_.load(); }
    float getHighFreq() const { return highFreq_.load(); }

    // Kill switches (completely cut a band)
    void setLowKill(bool kill) { lowKill_.store(kill); }
    void setMidKill(bool kill) { midKill_.store(kill); }
    void setHighKill(bool kill) { highKill_.store(kill); }

    bool isLowKilled() const { return lowKill_.load(); }
    bool isMidKilled() const { return midKill_.load(); }
    bool isHighKilled() const { return highKill_.load(); }

protected:
    void onSampleRateChanged() override;

private:
    void updateFilters();

    std::unique_ptr<FilterProcessor> lowFilter_;
    std::unique_ptr<FilterProcessor> midFilter_;
    std::unique_ptr<FilterProcessor> highFilter_;

    std::atomic<float> lowGain_{0.0f};
    std::atomic<float> midGain_{0.0f};
    std::atomic<float> highGain_{0.0f};

    std::atomic<float> lowFreq_{100.0f};
    std::atomic<float> midFreq_{1000.0f};
    std::atomic<float> highFreq_{10000.0f};

    std::atomic<bool> lowKill_{false};
    std::atomic<bool> midKill_{false};
    std::atomic<bool> highKill_{false};
};

} // namespace BeatMate::Core
