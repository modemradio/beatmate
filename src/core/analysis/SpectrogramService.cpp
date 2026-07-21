#include "SpectrogramService.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

SpectrogramService::SpectrogramService() = default;
SpectrogramService::~SpectrogramService() = default;

SpectrogramData SpectrogramService::compute(const AudioTrack& track, int fftSize, int hopSize) {
    spdlog::info("SpectrogramService: computing spectrogram (fft={}, hop={})", fftSize, hopSize);

    SpectrogramData result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (numSamples < static_cast<size_t>(fftSize)) return result;

    result.fftSize = fftSize;
    result.hopSize = hopSize;
    result.sampleRate = sr;
    result.numBins = fftSize / 2 + 1;
    result.duration = track.getDuration();

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    result.numFrames = numFrames;
    result.magnitude.resize(numFrames);
    result.phase.resize(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);

        auto mag = fft.getMagnitudes(spectrum);
        auto phs = fft.getPhases(spectrum);

        result.magnitude[frame].resize(mag.size());
        for (size_t bin = 0; bin < mag.size(); ++bin) {
            result.magnitude[frame][bin] = 20.0f * std::log10(std::max(mag[bin], 1e-10f));
        }

        result.phase[frame] = phs;
    }

    spdlog::info("SpectrogramService: {} frames x {} bins", result.numFrames, result.numBins);
    return result;
}

std::vector<std::vector<float>> SpectrogramService::createMelFilterbank(
    int numBands, int fftSize, int sampleRate) {

    float nyquist = sampleRate / 2.0f;
    float melLow = hzToMel(20.0f);
    float melHigh = hzToMel(nyquist);

    std::vector<float> melPoints(numBands + 2);
    for (int i = 0; i < numBands + 2; ++i) {
        melPoints[i] = melLow + (melHigh - melLow) * i / (numBands + 1);
    }

    std::vector<int> binPoints(numBands + 2);
    for (int i = 0; i < numBands + 2; ++i) {
        float hz = melToHz(melPoints[i]);
        binPoints[i] = static_cast<int>(hz * fftSize / sampleRate);
    }

    int numBins = fftSize / 2 + 1;
    std::vector<std::vector<float>> filterbank(numBands, std::vector<float>(numBins, 0.0f));

    for (int band = 0; band < numBands; ++band) {
        int start = binPoints[band];
        int center = binPoints[band + 1];
        int end = binPoints[band + 2];

        for (int bin = start; bin < center && bin < numBins; ++bin) {
            if (center > start) {
                filterbank[band][bin] = static_cast<float>(bin - start) / (center - start);
            }
        }

        for (int bin = center; bin < end && bin < numBins; ++bin) {
            if (end > center) {
                filterbank[band][bin] = static_cast<float>(end - bin) / (end - center);
            }
        }
    }

    return filterbank;
}

MelSpectrogramData SpectrogramService::computeMel(const AudioTrack& track, int numBands,
                                                    int fftSize, int hopSize) {
    spdlog::info("SpectrogramService: computing mel spectrogram ({} bands)", numBands);

    MelSpectrogramData result;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    if (numSamples < static_cast<size_t>(fftSize)) return result;

    result.sampleRate = sr;
    result.numBands = numBands;
    result.duration = track.getDuration();

    auto filterbank = createMelFilterbank(numBands, fftSize, sr);

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize) + 1;
    result.numFrames = numFrames;
    result.bands.resize(numFrames);

    int numBins = fftSize / 2 + 1;

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        std::vector<float> power(mag.size());
        for (size_t i = 0; i < mag.size(); ++i) power[i] = mag[i] * mag[i];

        result.bands[frame].resize(numBands);
        for (int band = 0; band < numBands; ++band) {
            float sum = 0.0f;
            for (int bin = 0; bin < numBins && bin < static_cast<int>(power.size()); ++bin) {
                sum += power[bin] * filterbank[band][bin];
            }
            result.bands[frame][band] = 10.0f * std::log10(std::max(sum, 1e-10f));
        }
    }

    spdlog::info("SpectrogramService: mel {} frames x {} bands", result.numFrames, result.numBands);
    return result;
}

std::vector<float> SpectrogramService::getSlice(const SpectrogramData& spec, double timeSeconds) {
    int frame = static_cast<int>(timeSeconds * spec.sampleRate / spec.hopSize);
    if (frame >= 0 && frame < spec.numFrames) {
        return spec.magnitude[frame];
    }
    return {};
}

std::vector<float> SpectrogramService::getBandEnergy(const SpectrogramData& spec,
                                                       float freqLow, float freqHigh) {
    int binLow = static_cast<int>(freqLow * spec.fftSize / spec.sampleRate);
    int binHigh = static_cast<int>(freqHigh * spec.fftSize / spec.sampleRate);
    binLow = std::max(0, binLow);
    binHigh = std::min(spec.numBins - 1, binHigh);

    std::vector<float> energy;
    energy.reserve(spec.numFrames);

    for (int frame = 0; frame < spec.numFrames; ++frame) {
        float sum = 0.0f;
        for (int bin = binLow; bin <= binHigh && bin < static_cast<int>(spec.magnitude[frame].size()); ++bin) {
            // Convert from dB back to linear for summing
            float lin = std::pow(10.0f, spec.magnitude[frame][bin] / 20.0f);
            sum += lin * lin;
        }
        energy.push_back(10.0f * std::log10(std::max(sum, 1e-10f)));
    }

    return energy;
}

} // namespace BeatMate::Core
