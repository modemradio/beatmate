#include "FFTProcessor.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

static int computeOrder(int fftSize) {
    int order = 0;
    int s = fftSize;
    while (s > 1) { s >>= 1; order++; }
    return order;
}

FFTProcessor::FFTProcessor(int fftSize) : fftSize_(fftSize), fftOrder_(computeOrder(fftSize)) {
    computeWindow();
    initFFT();
}

FFTProcessor::~FFTProcessor() = default;

void FFTProcessor::initFFT() {
    juceFFT_ = std::make_unique<juce::dsp::FFT>(fftOrder_);
    // JUCE performRealOnlyForwardTransform needs 2*fftSize floats
    fftData_.resize(static_cast<size_t>(fftSize_) * 2, 0.0f);
}

void FFTProcessor::setFFTSize(int size) {
    if (size < 64 || (size & (size - 1)) != 0) {
        spdlog::error("FFT size must be power of 2, got {}", size);
        return;
    }
    fftSize_ = size;
    fftOrder_ = computeOrder(size);
    computeWindow();
    initFFT();
}

void FFTProcessor::setWindow(WindowType type) {
    windowType_ = type;
    computeWindow();
}

void FFTProcessor::computeWindow() {
    window_ = generateWindow(windowType_, fftSize_);
}

std::vector<float> FFTProcessor::generateWindow(WindowType type, int size) {
    std::vector<float> w(size);
    for (int i = 0; i < size; ++i) {
        double t = static_cast<double>(i) / (size - 1);
        switch (type) {
            case WindowType::Rectangular:
                w[i] = 1.0f;
                break;
            case WindowType::Hann:
                w[i] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * t)));
                break;
            case WindowType::Hamming:
                w[i] = static_cast<float>(0.54 - 0.46 * std::cos(2.0 * kPi * t));
                break;
            case WindowType::Blackman:
                w[i] = static_cast<float>(0.42 - 0.5 * std::cos(2.0 * kPi * t) +
                                          0.08 * std::cos(4.0 * kPi * t));
                break;
            case WindowType::Kaiser: {
                double beta = 8.6;
                double a = 2.0 * i / (size - 1) - 1.0;
                double arg = beta * std::sqrt(1.0 - a * a);
                double i0arg = 1.0, i0beta = 1.0;
                for (int k = 1; k <= 20; ++k) {
                    double term_arg = std::pow(arg / 2.0, k) / std::tgamma(k + 1);
                    i0arg += term_arg * term_arg;
                    double term_beta = std::pow(beta / 2.0, k) / std::tgamma(k + 1);
                    i0beta += term_beta * term_beta;
                }
                w[i] = static_cast<float>(i0arg / i0beta);
                break;
            }
        }
    }
    return w;
}

void FFTProcessor::applyWindow(float* buffer, int size) const {
    int n = std::min(size, static_cast<int>(window_.size()));
    for (int i = 0; i < n; ++i) {
        buffer[i] *= window_[i];
    }
}

void FFTProcessor::forward(const float* input, std::vector<std::complex<float>>& output) {
    if (!juceFFT_ || !input) {
        output.clear();
        return;
    }

    for (int i = 0; i < fftSize_; ++i) {
        fftData_[static_cast<size_t>(i)] = input[i] * window_[static_cast<size_t>(i)];
    }
    std::memset(fftData_.data() + fftSize_, 0, static_cast<size_t>(fftSize_) * sizeof(float));

    // JUCE in-place FFT: input is fftSize real values, output is fftSize/2+1 complex pairs
    juceFFT_->performRealOnlyForwardTransform(fftData_.data());

    int halfSize = fftSize_ / 2 + 1;
    output.resize(halfSize);
    for (int i = 0; i < halfSize; ++i) {
        output[i] = std::complex<float>(fftData_[static_cast<size_t>(i * 2)],
                                         fftData_[static_cast<size_t>(i * 2 + 1)]);
    }
}

void FFTProcessor::inverse(const std::vector<std::complex<float>>& input, float* output) {
    if (!juceFFT_ || !output) {
        if (output) std::memset(output, 0, static_cast<size_t>(fftSize_) * sizeof(float));
        return;
    }

    std::memset(fftData_.data(), 0, fftData_.size() * sizeof(float));

    int halfSize = fftSize_ / 2 + 1;
    for (int i = 0; i < halfSize && i < static_cast<int>(input.size()); ++i) {
        fftData_[static_cast<size_t>(i * 2)] = input[i].real();
        fftData_[static_cast<size_t>(i * 2 + 1)] = input[i].imag();
    }

    juceFFT_->performRealOnlyInverseTransform(fftData_.data());

    for (int i = 0; i < fftSize_; ++i) {
        output[i] = fftData_[static_cast<size_t>(i)];
    }
}

std::vector<float> FFTProcessor::getMagnitudes(const std::vector<std::complex<float>>& spectrum) const {
    std::vector<float> mag(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); ++i) {
        mag[i] = std::abs(spectrum[i]);
    }
    return mag;
}

std::vector<float> FFTProcessor::getPhases(const std::vector<std::complex<float>>& spectrum) const {
    std::vector<float> phase(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); ++i) {
        phase[i] = std::arg(spectrum[i]);
    }
    return phase;
}

std::vector<float> FFTProcessor::getMagnitudesDB(const std::vector<std::complex<float>>& spectrum) const {
    std::vector<float> db(spectrum.size());
    for (size_t i = 0; i < spectrum.size(); ++i) {
        float mag = std::abs(spectrum[i]);
        db[i] = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -200.0f;
    }
    return db;
}

std::vector<float> FFTProcessor::getPowerSpectrum(const float* input) {
    std::vector<std::complex<float>> spectrum;
    forward(input, spectrum);
    auto mags = getMagnitudes(spectrum);
    for (auto& m : mags) m = m * m;
    return mags;
}

} // namespace BeatMate::Core
