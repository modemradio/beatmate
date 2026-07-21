#include "PaulstretchProcessor.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <cmath>
#include <random>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

PaulstretchProcessor::PaulstretchProcessor() = default;
PaulstretchProcessor::~PaulstretchProcessor() = default;

std::shared_ptr<AudioTrack> PaulstretchProcessor::process(const AudioTrack& input,
                                                            double stretchFactor) {
    spdlog::info("Paulstretch: {:.1f}x stretch", stretchFactor);

    auto monoTrack = input.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = windowSize_;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    size_t outputLen = static_cast<size_t>(numSamples * stretchFactor);
    std::vector<float> output(outputLen, 0.0f);

    int hopAnalysis = fftSize / 4;
    int hopSynthesis = static_cast<int>(hopAnalysis * stretchFactor);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * 3.14159265f);

    auto window = FFTProcessor::generateWindow(WindowType::Hann, fftSize);

    size_t inPos = 0;
    size_t outPos = 0;

    while (inPos + fftSize < numSamples && outPos + fftSize < outputLen) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + inPos, spectrum);

        auto mag = fft.getMagnitudes(spectrum);

        std::vector<std::complex<float>> randomized(spectrum.size());
        for (size_t i = 0; i < spectrum.size(); ++i) {
            float phase = phaseDist(rng);
            randomized[i] = std::polar(mag[i], phase);
        }

        std::vector<float> block(fftSize);
        fft.inverse(randomized, block.data());

        for (int i = 0; i < fftSize && outPos + i < outputLen; ++i) {
            output[outPos + i] += block[i] * window[i];
        }

        inPos += hopAnalysis;
        outPos += hopSynthesis;
    }

    float maxAbs = 0;
    for (auto& s : output) { float a = std::fabs(s); if (a > maxAbs) maxAbs = a; }
    if (maxAbs > 0) for (auto& s : output) s /= maxAbs;

    auto result = std::make_shared<AudioTrack>();
    result->loadData(std::move(output), sr, 1);
    spdlog::info("Paulstretch: output {:.1f}s", result->getDuration());
    return result;
}

} // namespace BeatMate::Core
