
#include "CollectionRadar.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>

#include <juce_dsp/juce_dsp.h>

namespace BeatMate::Core::Analysis {


float CollectionRadar::hzToMel(float hz)
{
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float CollectionRadar::melToHz(float mel)
{
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}


CollectionRadar::MelFilterBank
CollectionRadar::buildMelFilterBank(int numBands,
                                    int fftSize,
                                    double sampleRate,
                                    float minHz,
                                    float maxHz)
{
    MelFilterBank bank;
    bank.numBands = numBands;
    bank.fftSize  = fftSize;

    const int numBins = fftSize / 2 + 1;
    bank.filters.assign(numBands, std::vector<float>(numBins, 0.0f));

    const float melMin = hzToMel(minHz);
    const float melMax = hzToMel(std::min<float>(maxHz, static_cast<float>(sampleRate * 0.5)));

    std::vector<float> melPoints(numBands + 2);
    for (int i = 0; i < numBands + 2; ++i)
        melPoints[i] = melMin + (melMax - melMin) * static_cast<float>(i) / (numBands + 1);

    std::vector<int> binPoints(numBands + 2);
    for (int i = 0; i < numBands + 2; ++i) {
        const float hz = melToHz(melPoints[i]);
        const int   b  = static_cast<int>(std::floor((fftSize + 1) * hz / sampleRate));
        binPoints[i]   = std::clamp(b, 0, numBins - 1);
    }

    for (int m = 1; m <= numBands; ++m) {
        const int left   = binPoints[m - 1];
        const int center = binPoints[m];
        const int right  = binPoints[m + 1];

        for (int k = left; k < center; ++k) {
            if (center != left)
                bank.filters[m - 1][k] = static_cast<float>(k - left) / (center - left);
        }
        for (int k = center; k < right; ++k) {
            if (right != center)
                bank.filters[m - 1][k] = static_cast<float>(right - k) / (right - center);
        }
    }

    return bank;
}


std::vector<std::vector<float>>
CollectionRadar::buildDctMatrix(int numIn, int numOut)
{
    std::vector<std::vector<float>> mat(numOut, std::vector<float>(numIn, 0.0f));

    const float pi      = 3.14159265358979323846f;
    const float scale0  = std::sqrt(1.0f / numIn);
    const float scaleN  = std::sqrt(2.0f / numIn);

    for (int k = 0; k < numOut; ++k) {
        const float scale = (k == 0) ? scale0 : scaleN;
        for (int n = 0; n < numIn; ++n) {
            mat[k][n] = scale * std::cos(pi * (n + 0.5f) * k / numIn);
        }
    }
    return mat;
}


void CollectionRadar::applyHannWindow(float* frame, int n)
{
    const float pi = 3.14159265358979323846f;
    for (int i = 0; i < n; ++i) {
        const float w = 0.5f * (1.0f - std::cos(2.0f * pi * i / (n - 1)));
        frame[i] *= w;
    }
}


float CollectionRadar::cosineSimilarity(const std::vector<float>& a,
                                        const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty()) return 0.0f;

    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na  += static_cast<double>(a[i]) * a[i];
        nb  += static_cast<double>(b[i]) * b[i];
    }
    if (na <= 1e-12 || nb <= 1e-12) return 0.0f;

    const double cos = dot / (std::sqrt(na) * std::sqrt(nb));
    const double scaled = 0.5 * (cos + 1.0);
    return static_cast<float>(std::clamp(scaled, 0.0, 1.0));
}


static float estimateTempoFromOnsets(const std::vector<float>& onsetEnv,
                                     double frameRate)
{
    if (onsetEnv.size() < 8) return 0.0f;

    const int minLag = static_cast<int>(std::floor(frameRate * 60.0 / 200.0));
    const int maxLag = static_cast<int>(std::ceil (frameRate * 60.0 / 60.0));
    if (maxLag >= static_cast<int>(onsetEnv.size())) return 0.0f;

    double mean = std::accumulate(onsetEnv.begin(), onsetEnv.end(), 0.0) / onsetEnv.size();
    std::vector<double> x(onsetEnv.size());
    for (size_t i = 0; i < onsetEnv.size(); ++i) x[i] = onsetEnv[i] - mean;

    double bestVal = -std::numeric_limits<double>::infinity();
    int    bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double s = 0.0;
        for (size_t i = 0; i + lag < x.size(); ++i) s += x[i] * x[i + lag];
        if (s > bestVal) { bestVal = s; bestLag = lag; }
    }
    if (bestLag <= 0) return 0.0f;
    return static_cast<float>(60.0 * frameRate / bestLag);
}


CollectionRadarFeatures
CollectionRadar::extract(int64_t trackId,
                         const float* audio,
                         int numSamples,
                         double sampleRate)
{
    CollectionRadarFeatures out;
    out.trackId = trackId;
    out.mfccMean.assign(config_.mfccCoeffs, 0.0f);
    out.mfccStd .assign(config_.mfccCoeffs, 0.0f);
    out.tempo   = 0.0f;
    out.energy  = 0.0f;

    if (audio == nullptr || numSamples <= config_.fftSize || sampleRate <= 0.0)
        return out;

    const int fftSize    = config_.fftSize;
    const int hopSize    = config_.hopSize;
    const int numBins    = fftSize / 2 + 1;
    const int numBands   = config_.melBands;
    const int numCoeffs  = config_.mfccCoeffs;

    int fftOrder = 0;
    {
        int s = fftSize;
        while (s > 1) { s >>= 1; ++fftOrder; }
        if ((1 << fftOrder) != fftSize) return out;
    }

    juce::dsp::FFT fft(fftOrder);

    const auto bank = buildMelFilterBank(numBands, fftSize, sampleRate,
                                          config_.melMinHz, config_.melMaxHz);
    const auto dct  = buildDctMatrix(numBands, numCoeffs);

    std::vector<double> sum (numCoeffs, 0.0);
    std::vector<double> sum2(numCoeffs, 0.0);
    int frameCount = 0;

    std::vector<float> onsetEnv;
    onsetEnv.reserve(static_cast<size_t>(numSamples / hopSize + 1));
    std::vector<float> prevMag(numBins, 0.0f);

    double rmsAcc = 0.0;
    int    rmsN   = 0;

    std::vector<float> frame (fftSize, 0.0f);
    std::vector<float> fftBuf(static_cast<size_t>(fftSize) * 2, 0.0f);
    std::vector<float> magSpec(numBins, 0.0f);
    std::vector<float> melEnergy(numBands, 0.0f);

    for (int start = 0; start + fftSize <= numSamples; start += hopSize) {
        for (int i = 0; i < fftSize; ++i) frame[i] = audio[start + i];

        double localRms = 0.0;
        for (int i = 0; i < fftSize; ++i) localRms += frame[i] * frame[i];
        rmsAcc += localRms;
        rmsN   += fftSize;

        applyHannWindow(frame.data(), fftSize);

        std::fill(fftBuf.begin(), fftBuf.end(), 0.0f);
        std::copy(frame.begin(), frame.end(), fftBuf.begin());
        fft.performFrequencyOnlyForwardTransform(fftBuf.data());

        for (int k = 0; k < numBins; ++k) magSpec[k] = fftBuf[k];

        double flux = 0.0;
        for (int k = 0; k < numBins; ++k) {
            const float d = magSpec[k] - prevMag[k];
            if (d > 0.0f) flux += d;
        }
        onsetEnv.push_back(static_cast<float>(flux));
        prevMag = magSpec;

        for (int m = 0; m < numBands; ++m) {
            const auto& filt = bank.filters[m];
            double e = 0.0;
            for (int k = 0; k < numBins; ++k) e += filt[k] * magSpec[k];
            melEnergy[m] = static_cast<float>(std::log(1.0 + e));
        }

        for (int c = 0; c < numCoeffs; ++c) {
            const auto& row = dct[c];
            double v = 0.0;
            for (int m = 0; m < numBands; ++m) v += row[m] * melEnergy[m];
            sum [c] += v;
            sum2[c] += v * v;
        }
        ++frameCount;
    }

    if (frameCount == 0) return out;

    for (int c = 0; c < numCoeffs; ++c) {
        const double mean = sum[c] / frameCount;
        const double var  = std::max(0.0, sum2[c] / frameCount - mean * mean);
        out.mfccMean[c] = static_cast<float>(mean);
        out.mfccStd [c] = static_cast<float>(std::sqrt(var));
    }

    const double frameRate = sampleRate / static_cast<double>(hopSize);
    out.tempo = estimateTempoFromOnsets(onsetEnv, frameRate);

    if (rmsN > 0) {
        const double rms = std::sqrt(rmsAcc / rmsN);
        const double db  = 20.0 * std::log10(std::max(rms, 1e-7));
        const double norm = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
        out.energy = static_cast<float>(norm);
    }

    return out;
}


void CollectionRadar::store(const CollectionRadarFeatures& f)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[f.trackId] = f;
}

const CollectionRadarFeatures* CollectionRadar::get(int64_t trackId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(trackId);
    if (it == cache_.end()) return nullptr;
    return &it->second;
}

void CollectionRadar::remove(int64_t trackId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(trackId);
}

void CollectionRadar::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

size_t CollectionRadar::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

std::vector<int64_t> CollectionRadar::cachedTrackIds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int64_t> ids;
    ids.reserve(cache_.size());
    for (const auto& kv : cache_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}


float CollectionRadar::similarity(const CollectionRadarFeatures& a,
                                  const CollectionRadarFeatures& b) const
{
    const float sMean = cosineSimilarity(a.mfccMean, b.mfccMean);

    const float sStd = cosineSimilarity(a.mfccStd, b.mfccStd);

    float sTempo = 1.0f;
    if (a.tempo > 0.0f && b.tempo > 0.0f && config_.tempoHalfLife > 0.0f) {
        const float d1 = std::abs(a.tempo - b.tempo);
        const float d2 = std::abs(a.tempo - b.tempo * 2.0f);
        const float d3 = std::abs(a.tempo * 2.0f - b.tempo);
        const float d  = std::min({ d1, d2, d3 });
        sTempo = std::exp(-0.6931472f * d / config_.tempoHalfLife);
    }

    const float sEnergy = 1.0f - std::min(1.0f, std::abs(a.energy - b.energy));

    const auto& c = config_;
    float total = c.mfccMeanWeight * sMean
                + c.mfccStdWeight  * sStd
                + c.tempoWeight    * sTempo
                + c.energyWeight   * sEnergy;

    const float wSum = c.mfccMeanWeight + c.mfccStdWeight
                     + c.tempoWeight    + c.energyWeight;
    if (wSum > 1e-6f) total /= wSum;

    return std::clamp(total, 0.0f, 1.0f);
}

float CollectionRadar::timbreSimilarity(int64_t a, int64_t b) const
{
    if (a == b) return 1.0f;
    std::lock_guard<std::mutex> lock(mutex_);
    auto ia = cache_.find(a);
    auto ib = cache_.find(b);
    if (ia == cache_.end() || ib == cache_.end()) return -1.0f;
    const float c = cosineSimilarity(ia->second.mfccMean, ib->second.mfccMean);
    return std::clamp(c, 0.0f, 1.0f);
}


std::vector<SimilarTrack>
CollectionRadar::findSimilar(int64_t referenceTrackId, int topK) const
{
    std::vector<SimilarTrack> results;
    if (topK <= 0) return results;

    // Copy reference out of the map under the lock, then release it so that
    CollectionRadarFeatures ref;
    std::vector<CollectionRadarFeatures> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(referenceTrackId);
        if (it == cache_.end()) return results;
        ref = it->second;

        snapshot.reserve(cache_.size());
        for (const auto& kv : cache_) {
            if (kv.first == referenceTrackId) continue;
            snapshot.push_back(kv.second);
        }
    }

    results.reserve(snapshot.size());
    for (const auto& f : snapshot) {
        results.push_back({ f.trackId, similarity(ref, f) });
    }

    const int k = std::min<int>(topK, static_cast<int>(results.size()));
    std::partial_sort(results.begin(), results.begin() + k, results.end(),
        [](const SimilarTrack& a, const SimilarTrack& b) {
            return a.similarity > b.similarity;
        });
    results.resize(k);
    return results;
}


std::vector<std::vector<float>>
CollectionRadar::computeSimilarityMatrix() const
{
    std::vector<CollectionRadarFeatures> snap;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snap.reserve(cache_.size());
        for (const auto& kv : cache_) snap.push_back(kv.second);
    }

    std::sort(snap.begin(), snap.end(),
        [](const CollectionRadarFeatures& a, const CollectionRadarFeatures& b) {
            return a.trackId < b.trackId;
        });

    const size_t n = snap.size();
    std::vector<std::vector<float>> mat(n, std::vector<float>(n, 0.0f));
    for (size_t i = 0; i < n; ++i) {
        mat[i][i] = 1.0f;
        for (size_t j = i + 1; j < n; ++j) {
            const float s = similarity(snap[i], snap[j]);
            mat[i][j] = s;
            mat[j][i] = s;
        }
    }
    return mat;
}

}
