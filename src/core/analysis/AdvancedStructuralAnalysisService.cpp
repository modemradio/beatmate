#include "AdvancedStructuralAnalysisService.h"
#include "AudioAnalysisPipeline.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace {
constexpr int kMaxSSMFrames = 2048;
}

namespace BeatMate::Core {

AdvancedStructuralAnalysisService::AdvancedStructuralAnalysisService() = default;
AdvancedStructuralAnalysisService::~AdvancedStructuralAnalysisService() = default;

std::vector<std::vector<float>> AdvancedStructuralAnalysisService::extractFeatures(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 2048;
    int hopSize = static_cast<int>(hopSeconds_ * sr);
    if (hopSize < 512) hopSize = 512;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numFrames = static_cast<int>(safeFrameCount(numSamples,
                                                    static_cast<size_t>(fftSize),
                                                    static_cast<size_t>(hopSize)));
    std::vector<std::vector<float>> features;
    features.reserve(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        std::vector<float> featureVec;

        if (featureType_ == 1 || featureType_ == 2) {
            std::vector<float> chroma(12, 0.0f);
            for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
                float freq = static_cast<float>(bin) * sr / fftSize;
                if (freq < 50.0f || freq > 5000.0f) continue;

                float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
                int pitchClass = static_cast<int>(std::round(midi)) % 12;
                if (pitchClass < 0) pitchClass += 12;
                chroma[pitchClass] += mag[bin] * mag[bin];
            }
            float chromaMax = *std::max_element(chroma.begin(), chroma.end());
            if (chromaMax > 0) {
                for (auto& c : chroma) c /= chromaMax;
            }
            featureVec.insert(featureVec.end(), chroma.begin(), chroma.end());
        }

        if (featureType_ == 0 || featureType_ == 2) {
            int numBands = 13;
            std::vector<float> melBands(numBands, 0.0f);

            for (int band = 0; band < numBands; ++band) {
                float melLow = 2595.0f * std::log10(1.0f + 20.0f / 700.0f);
                float melHigh = 2595.0f * std::log10(1.0f + (sr / 2.0f) / 700.0f);
                float melCenter = melLow + (melHigh - melLow) * (band + 1) / (numBands + 1);
                float freqCenter = 700.0f * (std::pow(10.0f, melCenter / 2595.0f) - 1.0f);

                int centerBin = static_cast<int>(freqCenter * fftSize / sr);
                int bandWidth = std::max(1, centerBin / 4);

                float bandEnergy = 0.0f;
                for (int b = std::max(1, centerBin - bandWidth); b <= std::min(static_cast<int>(mag.size()) - 1, centerBin + bandWidth); ++b) {
                    bandEnergy += mag[b] * mag[b];
                }
                melBands[band] = std::log(bandEnergy + 1e-10f);
            }
            featureVec.insert(featureVec.end(), melBands.begin(), melBands.end());
        }

        features.push_back(featureVec);
    }

    return features;
}

std::vector<std::vector<float>> AdvancedStructuralAnalysisService::computeSSM(
    const std::vector<std::vector<float>>& featuresIn) {

    std::vector<std::vector<float>> featuresLocal;
    const std::vector<std::vector<float>>* features = &featuresIn;

    if (static_cast<int>(featuresIn.size()) > kMaxSSMFrames && !featuresIn.empty()) {
        int srcN = static_cast<int>(featuresIn.size());
        int factor = (srcN + kMaxSSMFrames - 1) / kMaxSSMFrames;
        int dim = static_cast<int>(featuresIn[0].size());
        int dstN = (srcN + factor - 1) / factor;
        featuresLocal.assign(dstN, std::vector<float>(dim, 0.0f));
        for (int i = 0; i < srcN; ++i) {
            int dstIdx = i / factor;
            if (dstIdx >= dstN) break;
            for (int k = 0; k < dim && k < static_cast<int>(featuresIn[i].size()); ++k) {
                featuresLocal[dstIdx][k] += featuresIn[i][k];
            }
        }
        for (int i = 0; i < dstN; ++i) {
            int bucketSize = std::min(factor, srcN - i * factor);
            if (bucketSize <= 0) continue;
            for (auto& v : featuresLocal[i]) v /= static_cast<float>(bucketSize);
        }
        spdlog::info("AdvancedStructuralAnalysisService: SSM downsampled {} -> {} frames (factor={})",
                     srcN, dstN, factor);
        features = &featuresLocal;
    }

    int n = static_cast<int>(features->size());
    std::vector<std::vector<float>> ssm(n, std::vector<float>(n, 0.0f));

    for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) {
            float dotProduct = 0.0f, normA = 0.0f, normB = 0.0f;
            for (size_t k = 0; k < (*features)[i].size(); ++k) {
                dotProduct += (*features)[i][k] * (*features)[j][k];
                normA += (*features)[i][k] * (*features)[i][k];
                normB += (*features)[j][k] * (*features)[j][k];
            }
            float sim = 0.0f;
            if (normA > 0 && normB > 0) {
                sim = dotProduct / (std::sqrt(normA) * std::sqrt(normB));
            }
            ssm[i][j] = sim;
            ssm[j][i] = sim;
        }
    }

    return ssm;
}

void AdvancedStructuralAnalysisService::enhanceSSM(std::vector<std::vector<float>>& ssm) {
    int n = static_cast<int>(ssm.size());
    if (n < 3) return;

    int filterSize = 3;
    int half = filterSize / 2;
    auto copy = ssm;

    for (int i = half; i < n - half; ++i) {
        for (int j = half; j < n - half; ++j) {
            std::vector<float> neighborhood;
            neighborhood.reserve(static_cast<size_t>(filterSize) * filterSize);
            for (int di = -half; di <= half; ++di) {
                for (int dj = -half; dj <= half; ++dj) {
                    neighborhood.push_back(copy[i + di][j + dj]);
                }
            }
            std::sort(neighborhood.begin(), neighborhood.end());
            ssm[i][j] = neighborhood[neighborhood.size() / 2];
        }
    }

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (ssm[i][j] < 0.5f) ssm[i][j] = 0.0f;
        }
    }
}

std::vector<float> AdvancedStructuralAnalysisService::computeNoveltyCurve(
    const std::vector<std::vector<float>>& ssm) {

    int n = static_cast<int>(ssm.size());
    std::vector<float> novelty(n, 0.0f);

    int k = kernelSize_;
    if (k > n / 2) k = n / 2;

    for (int i = k; i < n - k; ++i) {
        float sum = 0.0f;

        for (int di = -k; di < k; ++di) {
            for (int dj = -k; dj < k; ++dj) {
                int ii = i + di;
                int jj = i + dj;
                if (ii < 0 || ii >= n || jj < 0 || jj >= n) continue;

                float sign = ((di < 0) == (dj < 0)) ? -1.0f : 1.0f;
                sum += sign * ssm[ii][jj];
            }
        }

        novelty[i] = std::max(0.0f, sum);
    }

    if (!novelty.empty()) {
        float maxVal = *std::max_element(novelty.begin(), novelty.end());
        if (maxVal > 0) {
            for (auto& v : novelty) v /= maxVal;
        }
    }

    return novelty;
}

std::vector<double> AdvancedStructuralAnalysisService::findBoundaries(
    const std::vector<float>& novelty, double hopDuration) {

    std::vector<double> boundaries;
    boundaries.push_back(0.0);

    float threshold = 0.3f;
    int minDistance = static_cast<int>(4.0 / hopDuration); // At least 4 seconds between boundaries

    int lastBoundary = -minDistance;

    for (int i = 1; i < static_cast<int>(novelty.size()) - 1; ++i) {
        if (novelty[i] > threshold &&
            novelty[i] > novelty[i - 1] &&
            novelty[i] > novelty[i + 1] &&
            (i - lastBoundary) >= minDistance) {

            boundaries.push_back(i * hopDuration);
            lastBoundary = i;
        }
    }

    return boundaries;
}

std::vector<Section> AdvancedStructuralAnalysisService::classifySections(
    const std::vector<std::vector<float>>& ssm, const std::vector<double>& boundaries,
    const AudioTrack& track, const std::vector<float>& novelty, double frameHop) {

    std::vector<Section> sections;
    double duration = track.getDuration();
    if (duration <= 0.0) return sections;
    if (frameHop <= 0.0) frameHop = hopSeconds_;

    float noveltyMax = 1e-6f;
    for (float v : novelty) noveltyMax = std::max(noveltyMax, v);

    for (size_t i = 0; i < boundaries.size(); ++i) {
        Section sec;
        sec.startTime = boundaries[i];
        sec.endTime = (i + 1 < boundaries.size()) ? boundaries[i + 1] : duration;

        double relPos = sec.startTime / duration;

        if (relPos < 0.05) {
            sec.type = SectionType::Intro;
        } else if (relPos > 0.9) {
            sec.type = SectionType::Outro;
        } else {
            int startFrame = static_cast<int>(sec.startTime / frameHop);
            int endFrame = static_cast<int>(sec.endTime / frameHop);
            startFrame = std::min(startFrame, static_cast<int>(ssm.size()) - 1);
            endFrame = std::min(endFrame, static_cast<int>(ssm.size()) - 1);

            float avgSelfSim = 0.0f;
            int count = 0;
            for (int f = startFrame; f <= endFrame; ++f) {
                for (int g = 0; g < static_cast<int>(ssm.size()); ++g) {
                    if (g < startFrame || g > endFrame) {
                        avgSelfSim += ssm[f][g];
                        count++;
                    }
                }
            }
            if (count > 0) avgSelfSim /= count;

            if (avgSelfSim > 0.6f) {
                sec.type = SectionType::Chorus;
            } else if (avgSelfSim > 0.4f) {
                sec.type = SectionType::Verse;
            } else if (avgSelfSim > 0.2f) {
                sec.type = SectionType::Bridge;
            } else {
                sec.type = SectionType::Breakdown;
            }
        }

        switch (sec.type) {
            case SectionType::Intro: sec.label = "Intro"; break;
            case SectionType::Verse: sec.label = "Verse"; break;
            case SectionType::Chorus: sec.label = "Chorus"; break;
            case SectionType::Bridge: sec.label = "Bridge"; break;
            case SectionType::Drop: sec.label = "Drop"; break;
            case SectionType::Breakdown: sec.label = "Breakdown"; break;
            case SectionType::Buildup: sec.label = "Buildup"; break;
            case SectionType::Outro: sec.label = "Outro"; break;
            default: sec.label = "Unknown"; break;
        }

        int boundaryFrame = static_cast<int>(sec.startTime / frameHop);
        boundaryFrame = std::clamp(boundaryFrame, 0, static_cast<int>(novelty.size()) - 1);
        float peakValue = !novelty.empty() ? novelty[boundaryFrame] : 0.0f;
        sec.confidence = std::clamp(peakValue / noveltyMax, 0.0f, 1.0f);

        sections.push_back(sec);
    }

    return sections;
}

SelfSimilarityResult AdvancedStructuralAnalysisService::analyze(const AudioTrack& track) {
    spdlog::info("AdvancedStructuralAnalysisService: analyzing {}", track.getFilePath());

    SelfSimilarityResult result;

    auto features = extractFeatures(track);
    if (features.empty()) return result;

    int rawFrames = static_cast<int>(features.size());

    result.matrix = computeSSM(features);
    result.resolution = static_cast<int>(result.matrix.size());

    double effectiveHop = (result.resolution > 0)
        ? hopSeconds_ * static_cast<double>(rawFrames) / static_cast<double>(result.resolution)
        : hopSeconds_;
    result.hopDuration = effectiveHop;

    enhanceSSM(result.matrix);

    result.noveltyCurve = computeNoveltyCurve(result.matrix);

    result.boundaries = findBoundaries(result.noveltyCurve, effectiveHop);

    result.sections = classifySections(result.matrix, result.boundaries, track,
                                       result.noveltyCurve, effectiveHop);

    spdlog::info("AdvancedStructuralAnalysisService: {} boundaries, {} sections (resolution={}, hop={:.3f}s)",
                 result.boundaries.size(), result.sections.size(),
                 result.resolution, effectiveHop);
    return result;
}

} // namespace BeatMate::Core
