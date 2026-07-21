#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace BeatMate::Core {

class FFTProcessor;

class SpectrumAnalyzer {
public:
    explicit SpectrumAnalyzer(int fftSize = 2048);
    ~SpectrumAnalyzer();

    void processBlock(const float* input, int numSamples);

    std::vector<float> getMagnitudes() const;
    std::vector<float> getMagnitudesDB() const;
    std::vector<float> getPeakHold() const;

    void setFFTSize(int size);
    void setDecayRate(float rate) { decayRate_ = rate; }
    void setPeakHoldTime(float seconds) { peakHoldTime_ = seconds; }

    int getFFTSize() const { return fftSize_; }
    int getBinCount() const { return fftSize_ / 2 + 1; }
    float getBinFrequency(int bin, int sampleRate) const;

    void reset();

private:
    int fftSize_;
    std::unique_ptr<FFTProcessor> fft_;

    std::vector<float> inputBuffer_;
    int inputWritePos_ = 0;

    std::vector<float> smoothedMagnitudes_;
    std::vector<float> peakHold_;
    std::vector<int> peakHoldCounter_;

    float decayRate_ = 0.9f;
    float peakHoldTime_ = 1.0f;
    int sampleRate_ = 44100;
};

}
