#include "WaveformGenerator.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

WaveformGenerator::WaveformGenerator() = default;
WaveformGenerator::~WaveformGenerator() = default;

WaveformData WaveformGenerator::computeWaveform(const AudioTrack& track, int numPoints) {
    WaveformData data;
    data.resolution = static_cast<int>(track.getNumFrames() / numPoints);
    data.peaks.resize(numPoints, 0.0f);
    data.rms.resize(numPoints, 0.0f);

    auto monoTrack = track.toMono();
    const float* samples = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    size_t samplesPerPoint = totalSamples / numPoints;

    for (int i = 0; i < numPoints; ++i) {
        size_t start = i * samplesPerPoint;
        size_t end = std::min(start + samplesPerPoint, totalSamples);

        float peak = 0.0f;
        double rmsSum = 0.0;

        for (size_t j = start; j < end; ++j) {
            float abs_s = std::fabs(samples[j]);
            if (abs_s > peak) peak = abs_s;
            rmsSum += samples[j] * samples[j];
        }

        data.peaks[i] = peak;
        data.rms[i] = static_cast<float>(std::sqrt(rmsSum / (end - start)));
    }

    return data;
}

static void applyLowPass(const float* in, float* out, size_t count, float cutoff, float sampleRate)
{
    float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
    float dt = 1.0f / sampleRate;
    float alpha = dt / (rc + dt);
    float prev = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        prev += alpha * (in[i] - prev);
        out[i] = prev;
    }
}

static void applyHighPass(const float* in, float* out, size_t count, float cutoff, float sampleRate)
{
    float rc = 1.0f / (2.0f * 3.14159265f * cutoff);
    float dt = 1.0f / sampleRate;
    float alpha = rc / (rc + dt);
    float prevIn = 0.0f, prevOut = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        prevOut = alpha * (prevOut + in[i] - prevIn);
        prevIn = in[i];
        out[i] = prevOut;
    }
}

WaveformData WaveformGenerator::generate(const AudioTrack& track, int resolution) {
    spdlog::info("WaveformGenerator: overview ({} points)", resolution);
    return computeWaveform(track, resolution);
}

WaveformData WaveformGenerator::generateDetailed(const AudioTrack& track, int resolution) {
    spdlog::info("WaveformGenerator: detailed RGB ({} points)", resolution);

    WaveformData data;
    data.resolution = static_cast<int>(track.getNumFrames() / resolution);
    data.peaks.resize(resolution, 0.0f);
    data.rms.resize(resolution, 0.0f);

    auto monoTrack = track.toMono();
    const float* samples = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    float sr = static_cast<float>(track.getSampleRate());
    if (sr <= 0.0f) sr = 44100.0f;

    std::vector<float> lowBuf(totalSamples);
    std::vector<float> midBuf(totalSamples);
    std::vector<float> highBuf(totalSamples);

    applyLowPass(samples, lowBuf.data(), totalSamples, 250.0f, sr);

    applyHighPass(samples, highBuf.data(), totalSamples, 4000.0f, sr);

    for (size_t i = 0; i < totalSamples; ++i)
        midBuf[i] = samples[i] - lowBuf[i] - highBuf[i];

    size_t samplesPerPoint = totalSamples / resolution;

    std::vector<float> bassPeaks(resolution, 0.0f);
    std::vector<float> midPeaks(resolution, 0.0f);
    std::vector<float> treblePeaks(resolution, 0.0f);

    for (int i = 0; i < resolution; ++i) {
        size_t start = i * samplesPerPoint;
        size_t end = std::min(start + samplesPerPoint, totalSamples);

        float peak = 0.0f, bPeak = 0.0f, mPeak = 0.0f, tPeak = 0.0f;
        double rmsSum = 0.0;

        for (size_t j = start; j < end; ++j) {
            float abs_s = std::fabs(samples[j]);
            if (abs_s > peak) peak = abs_s;
            rmsSum += samples[j] * samples[j];

            float bAbs = std::fabs(lowBuf[j]);
            float mAbs = std::fabs(midBuf[j]);
            float tAbs = std::fabs(highBuf[j]);
            if (bAbs > bPeak) bPeak = bAbs;
            if (mAbs > mPeak) mPeak = mAbs;
            if (tAbs > tPeak) tPeak = tAbs;
        }

        data.peaks[i] = peak;
        data.rms[i] = static_cast<float>(std::sqrt(rmsSum / (end - start)));
        bassPeaks[i] = bPeak;
        midPeaks[i] = mPeak;
        treblePeaks[i] = tPeak;
    }

    // bass/mid/treble packed into rms, interleaved by band.
    data.rms.resize(resolution * 4);
    std::copy(bassPeaks.begin(), bassPeaks.end(), data.rms.begin() + resolution);
    std::copy(midPeaks.begin(), midPeaks.end(), data.rms.begin() + resolution * 2);
    std::copy(treblePeaks.begin(), treblePeaks.end(), data.rms.begin() + resolution * 3);

    return data;
}

bool WaveformGenerator::saveToCache(const std::string& trackId, const WaveformData& data,
                                     const std::string& cacheDir) {
    namespace fs = std::filesystem;
    fs::create_directories(cacheDir);

    auto path = fs::path(cacheDir) / (trackId + ".wfm");
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    int numPoints = static_cast<int>(data.peaks.size());
    file.write(reinterpret_cast<const char*>(&numPoints), sizeof(int));
    file.write(reinterpret_cast<const char*>(&data.resolution), sizeof(int));
    file.write(reinterpret_cast<const char*>(data.peaks.data()), numPoints * sizeof(float));
    file.write(reinterpret_cast<const char*>(data.rms.data()), numPoints * sizeof(float));

    spdlog::debug("WaveformGenerator: cached to {}", path.string());
    return true;
}

bool WaveformGenerator::loadFromCache(const std::string& trackId, WaveformData& data,
                                       const std::string& cacheDir) {
    auto path = std::filesystem::path(cacheDir) / (trackId + ".wfm");
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    int numPoints;
    file.read(reinterpret_cast<char*>(&numPoints), sizeof(int));
    file.read(reinterpret_cast<char*>(&data.resolution), sizeof(int));
    data.peaks.resize(numPoints);
    data.rms.resize(numPoints);
    file.read(reinterpret_cast<char*>(data.peaks.data()), numPoints * sizeof(float));
    file.read(reinterpret_cast<char*>(data.rms.data()), numPoints * sizeof(float));

    return true;
}

} // namespace BeatMate::Core
