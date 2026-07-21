#pragma once

#include <complex>
#include <memory>
#include <string>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace BeatMate::Core {

enum class WindowType {
    Rectangular, Hann, Hamming, Blackman, Kaiser
};

class FFTProcessor {
public:
    explicit FFTProcessor(int fftSize = 2048);
    ~FFTProcessor();

    // Change FFT size (power of 2: 256, 512, 1024, 2048, 4096, 8192)
    void setFFTSize(int size);
    int getFFTSize() const { return fftSize_; }

    void setWindow(WindowType type);
    WindowType getWindow() const { return windowType_; }

    void forward(const float* input, std::vector<std::complex<float>>& output);

    void inverse(const std::vector<std::complex<float>>& input, float* output);

    std::vector<float> getMagnitudes(const std::vector<std::complex<float>>& spectrum) const;

    std::vector<float> getPhases(const std::vector<std::complex<float>>& spectrum) const;

    std::vector<float> getMagnitudesDB(const std::vector<std::complex<float>>& spectrum) const;

    void applyWindow(float* buffer, int size) const;

    std::vector<float> getPowerSpectrum(const float* input);

    static std::vector<float> generateWindow(WindowType type, int size);

private:
    void initFFT();
    void computeWindow();

    int fftSize_ = 2048;
    int fftOrder_ = 11;
    WindowType windowType_ = WindowType::Hann;
    std::vector<float> window_;

    std::unique_ptr<juce::dsp::FFT> juceFFT_;
    std::vector<float> fftData_; // interleaved real/imag, size = fftSize_ * 2
};

}
