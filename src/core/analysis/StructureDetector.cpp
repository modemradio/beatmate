#include "StructureDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

StructureDetector::StructureDetector() = default;
StructureDetector::~StructureDetector() = default;

std::string StructureDetector::sectionTypeToString(SectionType type) {
    switch (type) {
        case SectionType::Intro: return "Intro";
        case SectionType::Verse: return "Verse";
        case SectionType::Chorus: return "Chorus";
        case SectionType::Bridge: return "Bridge";
        case SectionType::Drop: return "Drop";
        case SectionType::Breakdown: return "Breakdown";
        case SectionType::Buildup: return "Buildup";
        case SectionType::Outro: return "Outro";
        default: return "Unknown";
    }
}

std::vector<std::vector<float>> StructureDetector::computeSelfSimilarity(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 4096;
    int hopSize = 2048;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<std::vector<float>> chromaFrames;
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
        chromaFrames.push_back(chroma);
    }

    int n = static_cast<int>(chromaFrames.size());
    std::vector<std::vector<float>> ssm(n, std::vector<float>(n, 0.0f));

    for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) {
            float dot = 0, normA = 0, normB = 0;
            for (int k = 0; k < 12; ++k) {
                dot += chromaFrames[i][k] * chromaFrames[j][k];
                normA += chromaFrames[i][k] * chromaFrames[i][k];
                normB += chromaFrames[j][k] * chromaFrames[j][k];
            }
            float sim = (normA > 0 && normB > 0) ?
                        dot / (std::sqrt(normA) * std::sqrt(normB)) : 0.0f;
            ssm[i][j] = ssm[j][i] = sim;
        }
    }

    return ssm;
}

std::vector<float> StructureDetector::computeNoveltyCurve(const std::vector<std::vector<float>>& ssm) {
    int n = static_cast<int>(ssm.size());
    if (n < 2) return {};

    std::vector<float> novelty(n, 0.0f);
    int kernelSize = std::min(64, n / 4);

    for (int i = kernelSize; i < n - kernelSize; ++i) {
        // Checkerboard kernel novelty detection
        float before = 0, after = 0, cross = 0;
        int count = 0;
        for (int di = 1; di <= kernelSize; ++di) {
            for (int dj = 1; dj <= kernelSize; ++dj) {
                if (i - di >= 0 && i - dj >= 0) before += ssm[i - di][i - dj];
                if (i + di < n && i + dj < n) after += ssm[i + di][i + dj];
                if (i - di >= 0 && i + dj < n) cross += ssm[i - di][i + dj];
                count++;
            }
        }
        if (count > 0) {
            novelty[i] = (before + after - 2.0f * cross) / count;
        }
    }

    return novelty;
}

std::vector<double> StructureDetector::findBoundaries(const std::vector<float>& novelty,
                                                       double hopDuration) {
    std::vector<double> boundaries;
    boundaries.push_back(0.0);

    float mean = 0;
    for (auto& v : novelty) mean += v;
    mean /= novelty.size();

    float stddev = 0;
    for (auto& v : novelty) stddev += (v - mean) * (v - mean);
    stddev = std::sqrt(stddev / novelty.size());

    float threshold = mean + stddev * 1.0f;
    int minDistance = static_cast<int>(6.0 / hopDuration); // 6 s minimum between boundaries

    int lastPeak = -minDistance;
    for (int i = 1; i < static_cast<int>(novelty.size()) - 1; ++i) {
        if (novelty[i] > threshold &&
            novelty[i] > novelty[i - 1] && novelty[i] > novelty[i + 1] &&
            (i - lastPeak) >= minDistance) {
            boundaries.push_back(i * hopDuration);
            lastPeak = i;
        }
    }

    return boundaries;
}

SectionType StructureDetector::classifySection(const AudioTrack& track, double start, double end, bool isAlreadyMono) {
    double duration = track.getDuration();
    if (duration <= 0.0) return SectionType::Unknown;
    double relativeStart = start / duration;
    double relativeEnd = end / duration;
    double sectionDuration = end - start;
    if (sectionDuration <= 0.0) return SectionType::Unknown;

    bool isEarly = (relativeStart < 0.08);
    bool isLate = (relativeEnd > 0.92);

    int sr;
    const float* data;
    size_t totalFrames;
    AudioTrack monoTemp;
    if (isAlreadyMono) {
        sr = track.getSampleRate();
        data = track.getRawData();
        totalFrames = track.getTotalSamples();
    } else {
        monoTemp = track.toMono();
        sr = monoTemp.getSampleRate();
        data = monoTemp.getRawData();
        totalFrames = monoTemp.getTotalSamples();
    }
    if (sr <= 0 || !data || totalFrames == 0) return SectionType::Unknown;

    size_t startFrame = static_cast<size_t>(std::max(0.0, start * sr));
    if (startFrame >= totalFrames) return SectionType::Unknown;
    size_t maxFrames = totalFrames - startFrame;
    size_t numFrames = static_cast<size_t>(sectionDuration * sr);
    if (numFrames > maxFrames) numFrames = maxFrames;
    if (numFrames == 0) return SectionType::Unknown;

    float rms = 0.0f;
    for (size_t i = startFrame; i < startFrame + numFrames; ++i) {
        rms += data[i] * data[i];
    }
    rms = std::sqrt(rms / static_cast<float>(numFrames));

    float globalRms = 0.0f;
    size_t sampleStep = std::max<size_t>(1, totalFrames / 10000);
    size_t globalCount = 0;
    for (size_t i = 0; i < totalFrames; i += sampleStep) {
        globalRms += data[i] * data[i];
        globalCount++;
    }
    globalRms = globalCount > 0 ? std::sqrt(globalRms / static_cast<float>(globalCount)) : 1e-6f;

    float relativeEnergy = rms / std::max(globalRms, 1e-6f);

    int fftSize = 2048;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    float bassEnergy = 0.0f, totalEnergy = 0.0f;
    int fftCount = 0;
    size_t maxOffset = std::min(startFrame + numFrames, totalFrames);
    for (size_t offset = startFrame; offset + fftSize <= maxOffset; offset += fftSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        int bassBin = std::min(static_cast<int>(200.0 * fftSize / sr), static_cast<int>(mag.size()));
        for (int b = 1; b < static_cast<int>(mag.size()); ++b) {
            float e = mag[b] * mag[b];
            totalEnergy += e;
            if (b < bassBin) bassEnergy += e;
        }
        fftCount++;
        if (fftCount >= 20) break;
    }

    float bassRatio = totalEnergy > 0 ? bassEnergy / totalEnergy : 0.0f;

    float energyAtStart = 0.0f;
    size_t windowSz = std::min<size_t>(static_cast<size_t>(0.3 * sr), numFrames / 2);
    for (size_t i = startFrame; i < startFrame + windowSz; ++i)
        energyAtStart += data[i] * data[i];
    energyAtStart = windowSz > 0 ? std::sqrt(energyAtStart / static_cast<float>(windowSz)) : 0.0f;

    if (isEarly && sectionDuration < duration * 0.15)
        return SectionType::Intro;

    if (isLate && sectionDuration < duration * 0.15)
        return SectionType::Outro;

    // Drop: high energy + strong bass + high relative energy
    if (relativeEnergy > 1.1f && bassRatio > 0.35f)
        return SectionType::Drop;

    // Chorus: high energy but less bass-heavy than drop
    if (relativeEnergy > 1.0f && bassRatio <= 0.35f)
        return SectionType::Chorus;

    // Breakdown: significantly quieter than average
    if (relativeEnergy < 0.5f)
        return SectionType::Breakdown;

    // Buildup: energy is rising (start quieter, builds up)
    if (relativeEnergy > 0.7f && relativeEnergy < 1.1f && energyAtStart < rms * 0.7f)
        return SectionType::Buildup;

    // Verse: moderate energy
    if (relativeEnergy >= 0.5f && relativeEnergy < 1.0f)
        return SectionType::Verse;

    return SectionType::Bridge;
}

std::vector<Section> StructureDetector::detect(const AudioTrack& track) {
    spdlog::info("StructureDetector: analyzing {}", track.getFilePath());

    auto ssm = computeSelfSimilarity(track);
    auto novelty = computeNoveltyCurve(ssm);

    // Free SSM memory early (can be very large)
    ssm.clear();
    ssm.shrink_to_fit();

    int fftSize = 4096;
    int hopSize = 2048;
    double hopDuration = static_cast<double>(hopSize) / track.getSampleRate();

    auto boundaries = findBoundaries(novelty, hopDuration);

    boundaries.push_back(track.getDuration());

    // Mono conversion done once for all section classifications
    auto monoTrack = track.toMono();

    std::vector<Section> sections;
    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        Section sec;
        sec.startTime = boundaries[i];
        sec.endTime = boundaries[i + 1];
        sec.type = classifySection(monoTrack, sec.startTime, sec.endTime, true);
        sec.label = sectionTypeToString(sec.type);
        sec.confidence = 0.7f;
        sections.push_back(sec);
    }

    spdlog::info("StructureDetector: found {} sections", sections.size());
    return sections;
}

} // namespace BeatMate::Core
