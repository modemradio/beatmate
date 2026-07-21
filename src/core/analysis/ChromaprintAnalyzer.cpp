#include "ChromaprintAnalyzer.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

ChromaprintAnalyzer::ChromaprintAnalyzer() = default;
ChromaprintAnalyzer::~ChromaprintAnalyzer() = default;

uint32_t ChromaprintAnalyzer::hashChroma(const std::vector<float>& chroma) {
    uint32_t hash = 0;
    for (int i = 0; i < 12 && i < static_cast<int>(chroma.size()); ++i) {
        int q = static_cast<int>(chroma[i] * 7.0f);
        q = std::clamp(q, 0, 7);
        hash |= (q << (i * 2));
    }
    return hash;
}

std::vector<uint32_t> ChromaprintAnalyzer::computeChromaHash(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 4096;
    int hopSize = 2048;
    FFTProcessor fft(fftSize);

    std::vector<uint32_t> hashes;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        std::vector<float> chroma(12, 0.0f);
        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            double freq = static_cast<double>(bin) * sr / fftSize;
            if (freq < 60 || freq > 4000) continue;
            double midi = 12.0 * std::log2(freq / 440.0) + 69.0;
            int c = static_cast<int>(std::round(midi)) % 12;
            if (c < 0) c += 12;
            chroma[c] += mag[bin];
        }

        float maxC = *std::max_element(chroma.begin(), chroma.end());
        if (maxC > 0) for (auto& v : chroma) v /= maxC;

        hashes.push_back(hashChroma(chroma));
    }

    return hashes;
}

std::string ChromaprintAnalyzer::fingerprint(const AudioTrack& track) {
    auto hashes = computeChromaHash(track);

    std::ostringstream oss;
    for (auto h : hashes) {
        oss << std::hex << std::setw(8) << std::setfill('0') << h;
    }

    spdlog::info("ChromaprintAnalyzer: fingerprint generated ({} hashes)", hashes.size());
    return oss.str();
}

double ChromaprintAnalyzer::compareFingerprints(const std::string& fp1, const std::string& fp2) {
    size_t minLen = std::min(fp1.size(), fp2.size());
    if (minLen == 0) return 0.0;

    int matches = 0;
    int total = 0;

    for (size_t i = 0; i + 8 <= minLen; i += 8) {
        if (fp1.substr(i, 8) == fp2.substr(i, 8)) matches++;
        total++;
    }

    return (total > 0) ? static_cast<double>(matches) / total : 0.0;
}

bool ChromaprintAnalyzer::isDuplicate(const AudioTrack& a, const AudioTrack& b, double threshold) {
    auto fpA = fingerprint(a);
    auto fpB = fingerprint(b);
    double similarity = compareFingerprints(fpA, fpB);
    spdlog::info("ChromaprintAnalyzer: similarity={:.2f} (threshold={:.2f})", similarity, threshold);
    return similarity >= threshold;
}

} // namespace BeatMate::Core
