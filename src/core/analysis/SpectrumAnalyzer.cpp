#include "SpectrumAnalyzer.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>

namespace BeatMate::Core {

SpectrumAnalyzer::SpectrumAnalyzer(int fftSize) : fftSize_(fftSize) {
    fft_ = std::make_unique<FFTProcessor>(fftSize);
    inputBuffer_.resize(fftSize, 0.0f);
    int bins = fftSize / 2 + 1;
    smoothedMagnitudes_.resize(bins, 0.0f);
    peakHold_.resize(bins, 0.0f);
    peakHoldCounter_.resize(bins, 0);
}

SpectrumAnalyzer::~SpectrumAnalyzer() = default;

void SpectrumAnalyzer::setFFTSize(int size) {
    fftSize_ = size;
    fft_ = std::make_unique<FFTProcessor>(size);
    inputBuffer_.resize(size, 0.0f);
    inputWritePos_ = 0;
    int bins = size / 2 + 1;
    smoothedMagnitudes_.assign(bins, 0.0f);
    peakHold_.assign(bins, 0.0f);
    peakHoldCounter_.assign(bins, 0);
}

void SpectrumAnalyzer::processBlock(const float* input, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        inputBuffer_[inputWritePos_] = input[i];
        inputWritePos_ = (inputWritePos_ + 1) % fftSize_;
    }

    std::vector<float> ordered(fftSize_);
    for (int i = 0; i < fftSize_; ++i) {
        ordered[i] = inputBuffer_[(inputWritePos_ + i) % fftSize_];
    }

    std::vector<std::complex<float>> spectrum;
    fft_->forward(ordered.data(), spectrum);
    auto mag = fft_->getMagnitudes(spectrum);

    int bins = fftSize_ / 2 + 1;
    int peakHoldSamples = static_cast<int>(peakHoldTime_ * sampleRate_ / numSamples);

    for (int i = 0; i < bins && i < static_cast<int>(mag.size()); ++i) {
        if (mag[i] > smoothedMagnitudes_[i]) {
            smoothedMagnitudes_[i] = mag[i]; // instant attack
        } else {
            smoothedMagnitudes_[i] = smoothedMagnitudes_[i] * decayRate_ +
                                     mag[i] * (1.0f - decayRate_);
        }

        if (mag[i] > peakHold_[i]) {
            peakHold_[i] = mag[i];
            peakHoldCounter_[i] = peakHoldSamples;
        } else {
            peakHoldCounter_[i]--;
            if (peakHoldCounter_[i] <= 0) {
                peakHold_[i] *= 0.99f;
            }
        }
    }
}

std::vector<float> SpectrumAnalyzer::getMagnitudes() const {
    return smoothedMagnitudes_;
}

std::vector<float> SpectrumAnalyzer::getMagnitudesDB() const {
    std::vector<float> db(smoothedMagnitudes_.size());
    for (size_t i = 0; i < db.size(); ++i) {
        db[i] = (smoothedMagnitudes_[i] > 1e-10f) ?
                20.0f * std::log10(smoothedMagnitudes_[i]) : -200.0f;
    }
    return db;
}

std::vector<float> SpectrumAnalyzer::getPeakHold() const {
    return peakHold_;
}

float SpectrumAnalyzer::getBinFrequency(int bin, int sampleRate) const {
    return static_cast<float>(bin) * sampleRate / fftSize_;
}

void SpectrumAnalyzer::reset() {
    std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
    std::fill(smoothedMagnitudes_.begin(), smoothedMagnitudes_.end(), 0.0f);
    std::fill(peakHold_.begin(), peakHold_.end(), 0.0f);
    std::fill(peakHoldCounter_.begin(), peakHoldCounter_.end(), 0);
    inputWritePos_ = 0;
}

} // namespace BeatMate::Core
