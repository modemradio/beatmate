#include "OnsetDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

OnsetDetector::OnsetDetector() = default;
OnsetDetector::~OnsetDetector() = default;

std::vector<double> OnsetDetector::detect(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 1024;
    int hopSize = 512;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    double hopDuration = static_cast<double>(hopSize) / sr;

    std::vector<float> flux;
    std::vector<float> prevMag(fftSize / 2 + 1, 0.0f);

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float sf = 0.0f;
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            float diff = mag[bin] - prevMag[bin];
            if (diff > 0) sf += diff;
        }
        flux.push_back(sf);
        prevMag = mag;
    }

    int windowSize = static_cast<int>(0.1 / hopDuration);
    windowSize = std::max(3, windowSize);

    std::vector<double> onsets;
    int minGap = static_cast<int>(minInterval_ / hopDuration);
    int lastOnset = -minGap;

    for (int i = 1; i < static_cast<int>(flux.size()) - 1; ++i) {
        int start = std::max(0, i - windowSize);
        int end = std::min(static_cast<int>(flux.size()), i + windowSize);
        std::vector<float> localWindow(flux.begin() + start, flux.begin() + end);
        std::sort(localWindow.begin(), localWindow.end());
        float median = localWindow[localWindow.size() / 2];
        float adaptiveThreshold = median * (1.0f + threshold_);

        if (flux[i] > adaptiveThreshold &&
            flux[i] > flux[i - 1] && flux[i] >= flux[i + 1] &&
            (i - lastOnset) >= minGap) {
            onsets.push_back(i * hopDuration);
            lastOnset = i;
        }
    }

    spdlog::info("OnsetDetector: found {} onsets", onsets.size());
    return onsets;
}

}
