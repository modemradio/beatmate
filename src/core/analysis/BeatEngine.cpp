#include "BeatEngine.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kOdfRate = 100.0;
constexpr double kPreferredBPM = 120.0;
constexpr double kOctaveSigma = 0.9;
constexpr double kTightness = 100.0;

struct OnsetData {
    std::vector<double> broadband;
    std::vector<double> lowBand;
    double odfRate = kOdfRate;
};

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

void normalizeUnitMax(std::vector<double>& v) {
    double mx = 0.0;
    for (double x : v) mx = std::max(mx, x);
    if (mx > 1e-12) for (double& x : v) x /= mx;
}

OnsetData computeOnsets(const float* mono, std::int64_t numSamples, double sampleRate) {
    OnsetData out;
    if (!mono || numSamples <= 0 || sampleRate <= 0.0) return out;

    const int fftSize = 2048;
    const int hopSize = std::max(1, static_cast<int>(std::round(sampleRate / kOdfRate)));
    out.odfRate = sampleRate / static_cast<double>(hopSize);

    if (numSamples < static_cast<std::int64_t>(fftSize)) return out;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    const int numBins = fftSize / 2 + 1;
    const int lowBinMax = std::min(numBins - 1,
        std::max(1, static_cast<int>(250.0 * fftSize / sampleRate)));

    const std::int64_t numFrames = (numSamples - fftSize) / hopSize + 1;
    out.broadband.reserve(static_cast<size_t>(numFrames));
    out.lowBand.reserve(static_cast<size_t>(numFrames));

    std::vector<float> logMag(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> prevLog(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> maxFilt(static_cast<size_t>(numBins), 0.0f);
    const double gamma = 20.0;

    std::vector<std::complex<float>> spectrum;
    for (std::int64_t frame = 0; frame < numFrames; ++frame) {
        const std::int64_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        fft.forward(mono + offset, spectrum);
        const int bins = std::min(numBins, static_cast<int>(spectrum.size()));
        for (int b = 0; b < bins; ++b) {
            const float m = std::abs(spectrum[static_cast<size_t>(b)]);
            logMag[static_cast<size_t>(b)] = std::log(1.0f + static_cast<float>(gamma) * m);
        }

        for (int b = 0; b < bins; ++b) {
            const int lo = std::max(0, b - 1);
            const int hi = std::min(bins - 1, b + 1);
            float mx = prevLog[static_cast<size_t>(lo)];
            for (int k = lo + 1; k <= hi; ++k)
                mx = std::max(mx, prevLog[static_cast<size_t>(k)]);
            maxFilt[static_cast<size_t>(b)] = mx;
        }

        double sfBroad = 0.0;
        double sfLow = 0.0;
        for (int b = 1; b < bins; ++b) {
            const double diff = logMag[static_cast<size_t>(b)] - maxFilt[static_cast<size_t>(b)];
            if (diff > 0.0) {
                sfBroad += diff;
                if (b <= lowBinMax) sfLow += diff;
            }
        }
        out.broadband.push_back(sfBroad);
        out.lowBand.push_back(sfLow);
        prevLog.swap(logMag);
    }

    normalizeUnitMax(out.broadband);
    normalizeUnitMax(out.lowBand);
    return out;
}

std::vector<double> autocorrelation(const std::vector<double>& odf, int minLag, int maxLag) {
    const int n = static_cast<int>(odf.size());
    std::vector<double> ac(static_cast<size_t>(maxLag + 1), 0.0);
    if (n <= 0) return ac;

    double mean = std::accumulate(odf.begin(), odf.end(), 0.0) / n;
    std::vector<double> c(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) c[static_cast<size_t>(i)] = odf[static_cast<size_t>(i)] - mean;

    for (int lag = minLag; lag <= maxLag && lag < n; ++lag) {
        double s = 0.0;
        int cnt = 0;
        for (int i = 0; i + lag < n; ++i) {
            s += c[static_cast<size_t>(i)] * c[static_cast<size_t>(i + lag)];
            ++cnt;
        }
        ac[static_cast<size_t>(lag)] = cnt > 0 ? s / cnt : 0.0;
    }
    return ac;
}

double fourierTempoStrength(const std::vector<double>& odf, double odfRate, double bpm) {
    const double f = bpm / 60.0;
    const int n = static_cast<int>(odf.size());
    if (n <= 0) return 0.0;
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPi * f / odfRate;
    for (int i = 0; i < n; ++i) {
        re += odf[static_cast<size_t>(i)] * std::cos(w * i);
        im += odf[static_cast<size_t>(i)] * std::sin(w * i);
    }
    return std::sqrt(re * re + im * im) / n;
}

double octavePrior(double bpm) {
    const double tau = 60.0 / bpm;
    const double tau0 = 60.0 / kPreferredBPM;
    const double l = std::log2(tau / tau0) / kOctaveSigma;
    return std::exp(-0.5 * l * l);
}

double lowBandCombScore(const std::vector<double>& low, double odfRate, double bpm) {
    if (low.empty() || bpm <= 0.0) return 0.0;
    const double step = (60.0 / bpm) * odfRate;
    if (step < 1.0) return 0.0;
    double acc = 0.0;
    int cnt = 0;
    for (double pos = 0.0; pos < static_cast<double>(low.size()); pos += step) {
        const int idx = static_cast<int>(pos + 0.5);
        if (idx < 0 || idx >= static_cast<int>(low.size())) break;
        acc += low[static_cast<size_t>(idx)];
        ++cnt;
    }
    return cnt > 0 ? acc / cnt : 0.0;
}

double estimateTempo(const OnsetData& onsets, double minBPM, double maxBPM,
                     double& confidenceOut) {
    confidenceOut = 0.0;
    const std::vector<double>& odf = onsets.broadband;
    const int n = static_cast<int>(odf.size());
    if (n < 16) return 0.0;

    const double odfRate = onsets.odfRate;
    const int minLag = std::max(1, static_cast<int>(std::floor(60.0 / maxBPM * odfRate)));
    const int maxLag = std::min(n / 2, static_cast<int>(std::ceil(60.0 / minBPM * odfRate)));
    if (maxLag <= minLag) return 0.0;

    const auto ac = autocorrelation(odf, minLag, maxLag);

    double acMax = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) acMax = std::max(acMax, ac[static_cast<size_t>(lag)]);
    if (acMax <= 1e-12) return 0.0;

    double bestScore = -1.0;
    double bestBPM = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 / (lag / odfRate);
        const double acN = ac[static_cast<size_t>(lag)] / acMax;
        if (acN <= 0.0) continue;
        const double ft = fourierTempoStrength(odf, odfRate, bpm);
        const double combined = (acN + ft) * octavePrior(bpm);
        if (combined > bestScore) {
            bestScore = combined;
            bestBPM = bpm;
        }
    }
    if (bestBPM <= 0.0) return 0.0;

    const std::array<double, 5> ratios { 1.0, 0.5, 1.0 / 3.0, 2.0, 3.0 };
    double winnerBPM = bestBPM;
    double winnerScore = -1.0;
    for (double r : ratios) {
        const double cand = bestBPM * r;
        if (cand < minBPM - 1e-6 || cand > maxBPM + 1e-6) continue;
        const int lag = static_cast<int>(std::round(60.0 / cand * odfRate));
        if (lag < minLag || lag > maxLag) continue;
        const double acN = ac[static_cast<size_t>(lag)] / acMax;
        const double low = lowBandCombScore(onsets.lowBand, odfRate, cand);
        const double score = (acN + 0.6 * low + 0.4 * fourierTempoStrength(odf, odfRate, cand))
                           * octavePrior(cand);
        if (score > winnerScore) {
            winnerScore = score;
            winnerBPM = cand;
        }
    }

    double refined = winnerBPM;
    double refScore = -1.0;
    for (double bpm = winnerBPM - 2.0; bpm <= winnerBPM + 2.0 + 1e-9; bpm += 0.02) {
        if (bpm <= 0.0) continue;
        const double s = lowBandCombScore(onsets.lowBand, odfRate, bpm)
                       + lowBandCombScore(odf, odfRate, bpm);
        if (s > refScore) { refScore = s; refined = bpm; }
    }

    double second = -1.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 / (lag / odfRate);
        if (std::abs(bpm - winnerBPM) < winnerBPM * 0.06) continue;
        const double acN = ac[static_cast<size_t>(lag)] / acMax * octavePrior(bpm);
        second = std::max(second, acN);
    }
    const double peak = std::clamp(winnerScore, 0.0, 1.0);
    const double sep = second > 0.0 ? std::clamp(1.0 - second / (peak + 1e-9), 0.0, 1.0) : 0.5;
    confidenceOut = std::clamp(0.4 * peak + 0.6 * sep, 0.0, 1.0);

    return refined;
}

std::vector<int> trackBeatsDP(const std::vector<double>& odf, double odfRate, double bpm) {
    std::vector<int> beats;
    const int n = static_cast<int>(odf.size());
    if (n < 4 || bpm <= 0.0) return beats;

    const double period = 60.0 / bpm * odfRate;
    if (period < 1.0) return beats;

    std::vector<double> localScore(static_cast<size_t>(n), 0.0);
    double odfMean = std::accumulate(odf.begin(), odf.end(), 0.0) / n;
    double odfStd = 0.0;
    for (double v : odf) odfStd += (v - odfMean) * (v - odfMean);
    odfStd = std::sqrt(odfStd / n) + 1e-9;
    for (int i = 0; i < n; ++i)
        localScore[static_cast<size_t>(i)] = (odf[static_cast<size_t>(i)] - odfMean) / odfStd;

    std::vector<double> cum(static_cast<size_t>(n), -1e30);
    std::vector<int> back(static_cast<size_t>(n), -1);

    const int searchLo = std::max(1, static_cast<int>(std::round(0.5 * period)));
    const int searchHi = std::max(searchLo + 1, static_cast<int>(std::round(2.0 * period)));

    for (int i = 0; i < n; ++i) {
        double bestTrans = -1e30;
        int bestPrev = -1;
        for (int delta = searchLo; delta <= searchHi; ++delta) {
            const int j = i - delta;
            if (j < 0) break;
            if (cum[static_cast<size_t>(j)] <= -1e29) continue;
            const double logr = std::log2(static_cast<double>(delta) / period);
            const double trans = cum[static_cast<size_t>(j)] - kTightness * logr * logr;
            if (trans > bestTrans) { bestTrans = trans; bestPrev = j; }
        }
        if (bestPrev < 0) {
            cum[static_cast<size_t>(i)] = localScore[static_cast<size_t>(i)];
            back[static_cast<size_t>(i)] = -1;
        } else {
            cum[static_cast<size_t>(i)] = localScore[static_cast<size_t>(i)] + bestTrans;
            back[static_cast<size_t>(i)] = bestPrev;
        }
    }

    int endIdx = -1;
    double endScore = -1e30;
    const int tail = std::max(0, n - static_cast<int>(std::round(period)) - 1);
    for (int i = tail; i < n; ++i) {
        if (cum[static_cast<size_t>(i)] > endScore) { endScore = cum[static_cast<size_t>(i)]; endIdx = i; }
    }
    if (endIdx < 0) {
        for (int i = 0; i < n; ++i)
            if (cum[static_cast<size_t>(i)] > endScore) { endScore = cum[static_cast<size_t>(i)]; endIdx = i; }
    }
    if (endIdx < 0) return beats;

    for (int i = endIdx; i >= 0; i = back[static_cast<size_t>(i)]) {
        beats.push_back(i);
        if (back[static_cast<size_t>(i)] < 0) break;
    }
    std::reverse(beats.begin(), beats.end());
    return beats;
}

int detectDownbeatPhase(const std::vector<double>& lowBand, double odfRate,
                        const std::vector<int>& beatFrames, int beatsPerBar) {
    if (beatFrames.size() < static_cast<size_t>(beatsPerBar * 2) || lowBand.empty())
        return 0;

    std::vector<double> beatLow(beatFrames.size(), 0.0);
    const int half = std::max(1, static_cast<int>(0.03 * odfRate));
    for (size_t i = 0; i < beatFrames.size(); ++i) {
        const int f = beatFrames[i];
        double acc = 0.0;
        for (int d = -half; d <= half; ++d) {
            const int idx = f + d;
            if (idx >= 0 && idx < static_cast<int>(lowBand.size()))
                acc = std::max(acc, lowBand[static_cast<size_t>(idx)]);
        }
        beatLow[i] = acc;
    }

    int bestPhase = 0;
    double bestScore = -1.0;
    for (int phase = 0; phase < beatsPerBar; ++phase) {
        double acc = 0.0;
        int cnt = 0;
        for (size_t i = phase; i < beatLow.size(); i += static_cast<size_t>(beatsPerBar)) {
            acc += beatLow[i];
            ++cnt;
        }
        const double score = cnt > 0 ? acc / cnt : 0.0;
        if (score > bestScore) { bestScore = score; bestPhase = phase; }
    }
    return bestPhase;
}

} // namespace

BeatEngine::BeatEngine() = default;
BeatEngine::~BeatEngine() = default;

BeatGridCore BeatEngine::analyze(const float* mono, std::int64_t numSamples, double sampleRate,
                                 const BeatEngineOptions& options) const {
    BeatGridCore result;
    if (!mono || numSamples <= 0 || sampleRate <= 0.0) {
        spdlog::warn("BeatEngine: invalid input (samples={}, sr={})", numSamples, sampleRate);
        return result;
    }

    std::int64_t analyzeSamples = numSamples;
    if (options.maxSecondsToAnalyze > 0.0) {
        const std::int64_t cap = static_cast<std::int64_t>(options.maxSecondsToAnalyze * sampleRate);
        analyzeSamples = std::min(numSamples, cap);
    } else if (!options.fastMode) {
        const std::int64_t cap = static_cast<std::int64_t>(180.0 * sampleRate);
        analyzeSamples = std::min(numSamples, cap);
    } else {
        const std::int64_t cap = static_cast<std::int64_t>(60.0 * sampleRate);
        analyzeSamples = std::min(numSamples, cap);
    }

    const OnsetData onsets = computeOnsets(mono, analyzeSamples, sampleRate);
    if (onsets.broadband.size() < 16) {
        spdlog::warn("BeatEngine: onset function too short ({} frames)", onsets.broadband.size());
        return result;
    }

    double tempoConfidence = 0.0;
    const double bpm = estimateTempo(onsets, options.minBPM, options.maxBPM, tempoConfidence);
    if (bpm <= 0.0) {
        spdlog::warn("BeatEngine: tempo undetermined");
        return result;
    }

    const std::vector<int> beatFrames = trackBeatsDP(onsets.broadband, onsets.odfRate, bpm);
    if (beatFrames.size() < 4) {
        spdlog::warn("BeatEngine: beat tracking produced {} beats", beatFrames.size());
        return result;
    }

    result.bpm = bpm; // pleine precision (l'arrondi 0.01 derivait de ~0.25 beat sur 5 min)
    result.ok = true;
    result.beatsPerBarNum = 4;
    result.beatsPerBarDen = 4;

    // Compensation du centre de la fenetre FFT (Hann 2048) : le frame f reference le DEBUT
    const double winCenterSec = 2048.0 / (2.0 * sampleRate);
    result.beats.reserve(beatFrames.size());
    for (int f : beatFrames) result.beats.push_back(static_cast<double>(f) / onsets.odfRate + winCenterSec);

    const int phase = detectDownbeatPhase(onsets.lowBand, onsets.odfRate, beatFrames,
                                          result.beatsPerBarNum);
    for (size_t i = phase; i < result.beats.size(); i += static_cast<size_t>(result.beatsPerBarNum))
        result.bars.push_back(result.beats[i]);
    result.firstDownbeatSec = result.bars.empty() ? result.beats.front() : result.bars.front();

    std::vector<double> ibis;
    for (size_t i = 1; i < result.beats.size(); ++i)
        ibis.push_back(result.beats[i] - result.beats[i - 1]);
    const double medIbi = median(ibis);
    double drift = 0.0;
    int driftCnt = 0;
    for (double d : ibis) {
        if (medIbi > 0.0 && std::abs(d - medIbi) / medIbi > 0.06) ++driftCnt;
        drift += std::abs(d - medIbi);
    }
    result.variableTempo = options.allowVariableTempo
                        && ibis.size() > 8
                        && static_cast<double>(driftCnt) / ibis.size() > 0.18;

    if (result.variableTempo) {
        const int win = std::max(4, static_cast<int>(result.beatsPerBarNum * 2));
        for (size_t i = 0; i + 1 < result.beats.size(); i += static_cast<size_t>(result.beatsPerBarNum)) {
            const size_t hi = std::min(result.beats.size() - 1, i + static_cast<size_t>(win));
            if (hi <= i) break;
            const double span = result.beats[hi] - result.beats[i];
            const double localBpm = span > 0.0 ? 60.0 * static_cast<double>(hi - i) / span : result.bpm;
            result.tempoMap.push_back({ result.beats[i], localBpm });
        }
        if (result.tempoMap.empty())
            result.tempoMap.push_back({ result.beats.front(), result.bpm });
    } else {
        result.tempoMap.push_back({ result.beats.front(), result.bpm });
    }

    double odfMean = std::accumulate(onsets.broadband.begin(), onsets.broadband.end(), 0.0)
                   / static_cast<double>(onsets.broadband.size());
    int aligned = 0;
    for (int f : beatFrames) {
        if (f >= 0 && f < static_cast<int>(onsets.broadband.size())
            && onsets.broadband[static_cast<size_t>(f)] >= odfMean)
            ++aligned;
    }
    const double alignFraction = beatFrames.empty()
        ? 0.0 : static_cast<double>(aligned) / static_cast<double>(beatFrames.size());
    result.confidence = std::clamp(0.5 * tempoConfidence + 0.5 * alignFraction, 0.0, 1.0);

    spdlog::info("BeatEngine: {:.2f} BPM, {} beats, downbeatPhase={}, conf={:.0f}%, variable={}",
                 result.bpm, result.beats.size(), phase, result.confidence * 100.0,
                 result.variableTempo ? "yes" : "no");
    return result;
}

BeatGridCore BeatEngine::analyzeMultiChannel(const float* const* channels, int numChannels,
                                             std::int64_t numFrames, double sampleRate,
                                             const BeatEngineOptions& options) const {
    if (!channels || numChannels <= 0 || numFrames <= 0)
        return {};
    if (numChannels == 1)
        return analyze(channels[0], numFrames, sampleRate, options);

    std::vector<float> mono(static_cast<size_t>(numFrames));
    const float inv = 1.0f / static_cast<float>(numChannels);
    for (std::int64_t f = 0; f < numFrames; ++f) {
        float s = 0.0f;
        for (int c = 0; c < numChannels; ++c) s += channels[c][f];
        mono[static_cast<size_t>(f)] = s * inv;
    }
    return analyze(mono.data(), numFrames, sampleRate, options);
}

BeatGridCore BeatEngine::analyze(const AudioTrack& track, const BeatEngineOptions& options) const {
    AudioTrack monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    const std::int64_t numSamples = static_cast<std::int64_t>(monoTrack.getTotalSamples());
    const double sr = static_cast<double>(monoTrack.getSampleRate());
    return analyze(data, numSamples, sr, options);
}

} // namespace BeatMate::Core
