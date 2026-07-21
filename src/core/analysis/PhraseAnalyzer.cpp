#include "PhraseAnalyzer.h"

#include "../audio/AudioTrack.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace BeatMate::Core::Analysis {

namespace {

inline float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0.0f;
    p = clamp01(p);
    std::sort(v.begin(), v.end());
    const size_t idx = static_cast<size_t>(p * static_cast<float>(v.size() - 1));
    return v[idx];
}

float mean(const std::vector<float>& v, size_t i0, size_t i1) {
    if (v.empty() || i1 <= i0) return 0.0f;
    i1 = std::min(i1, v.size());
    i0 = std::min(i0, v.size());
    if (i1 <= i0) return 0.0f;
    double s = 0.0;
    for (size_t i = i0; i < i1; ++i) s += v[i];
    return static_cast<float>(s / static_cast<double>(i1 - i0));
}

float slope(const std::vector<float>& v, size_t i0, size_t i1) {
    if (v.empty()) return 0.0f;
    i0 = std::min(i0, v.size());
    i1 = std::min(i1, v.size());
    if (i1 <= i0 + 1) return 0.0f;
    const size_t n = i1 - i0;
    const double xMean = (n - 1) * 0.5;
    double yMean = 0.0;
    for (size_t i = i0; i < i1; ++i) yMean += v[i];
    yMean /= static_cast<double>(n);
    double num = 0.0, den = 0.0;
    for (size_t i = i0; i < i1; ++i) {
        const double x = static_cast<double>(i - i0) - xMean;
        const double y = static_cast<double>(v[i]) - yMean;
        num += x * y;
        den += x * x;
    }
    if (den <= 0.0) return 0.0f;
    return static_cast<float>(num / den);
}

size_t secToIndex(double sec, double hopSec, size_t size) {
    if (hopSec <= 0.0 || size == 0) return 0;
    long long idx = static_cast<long long>(sec / hopSec);
    if (idx < 0) idx = 0;
    if (static_cast<size_t>(idx) >= size) idx = static_cast<long long>(size) - 1;
    return static_cast<size_t>(idx);
}

} // namespace

const char* PhraseAnalyzer::phraseLabel(PhraseType t) {
    switch (t) {
        case PhraseType::Intro:   return "Intro";
        case PhraseType::Up:      return "Up";
        case PhraseType::Down:    return "Down";
        case PhraseType::Chorus:  return "Chorus";
        case PhraseType::Bridge:  return "Bridge";
        case PhraseType::Verse:   return "Verse";
        case PhraseType::Outro:   return "Outro";
        case PhraseType::Unknown:
        default:                  return "Unknown";
    }
}

std::vector<float> PhraseAnalyzer::computeEnergyEnvelope(const float* audio,
                                                        int numSamples,
                                                        double sampleRate,
                                                        double& outHopSec) const {
    std::vector<float> env;
    if (audio == nullptr || numSamples <= 0 || sampleRate <= 0.0) {
        outHopSec = 0.0;
        return env;
    }
    const int winLen = std::max(1, static_cast<int>(envWindowSec_ * sampleRate));
    const int hopLen = std::max(1, winLen / 2);
    outHopSec = static_cast<double>(hopLen) / sampleRate;

    env.reserve(static_cast<size_t>(numSamples / hopLen + 1));
    for (int start = 0; start + winLen <= numSamples; start += hopLen) {
        double sum = 0.0;
        const float* p = audio + start;
        for (int i = 0; i < winLen; ++i) {
            const double s = static_cast<double>(p[i]);
            sum += s * s;
        }
        const float rms = static_cast<float>(std::sqrt(sum / static_cast<double>(winLen)));
        env.push_back(rms);
    }

    // 99e percentile pour résister aux outliers.
    if (!env.empty()) {
        const float p99 = percentile(env, 0.99f);
        const float norm = (p99 > 1e-9f) ? p99 : 1.0f;
        for (float& v : env) v = clamp01(v / norm);
    }
    return env;
}

std::vector<float> PhraseAnalyzer::computeOnsetDensity(const float* audio,
                                                      int numSamples,
                                                      double sampleRate,
                                                      double segmentSec) const {
    std::vector<float> density;
    if (audio == nullptr || numSamples <= 0 || sampleRate <= 0.0 || segmentSec <= 0.0) {
        return density;
    }

    const int winLen = std::max(1, static_cast<int>(0.023 * sampleRate));
    const int hopLen = std::max(1, winLen / 2);

    std::vector<float> shortRms;
    shortRms.reserve(static_cast<size_t>(numSamples / hopLen + 1));
    for (int start = 0; start + winLen <= numSamples; start += hopLen) {
        double sum = 0.0;
        const float* p = audio + start;
        for (int i = 0; i < winLen; ++i) {
            const double s = static_cast<double>(p[i]);
            sum += s * s;
        }
        shortRms.push_back(static_cast<float>(std::sqrt(sum / static_cast<double>(winLen))));
    }
    if (shortRms.size() < 3) return density;

    std::vector<float> novelty(shortRms.size(), 0.0f);
    for (size_t i = 1; i < shortRms.size(); ++i) {
        const float d = shortRms[i] - shortRms[i - 1];
        novelty[i] = d > 0.0f ? d : 0.0f;
    }

    double mN = 0.0;
    for (float v : novelty) mN += v;
    mN /= static_cast<double>(novelty.size());
    double varN = 0.0;
    for (float v : novelty) {
        const double d = v - mN;
        varN += d * d;
    }
    varN /= static_cast<double>(novelty.size());
    const float thresh = static_cast<float>(mN + 1.5 * std::sqrt(varN));

    const double frameSec = static_cast<double>(hopLen) / sampleRate;
    const int minGap = std::max(1, static_cast<int>(0.05 / std::max(1e-9, frameSec)));
    std::vector<int> onsetFrames;
    int lastOnset = -minGap * 2;
    for (size_t i = 1; i + 1 < novelty.size(); ++i) {
        if (novelty[i] > thresh &&
            novelty[i] >= novelty[i - 1] &&
            novelty[i] >= novelty[i + 1] &&
            static_cast<int>(i) - lastOnset >= minGap) {
            onsetFrames.push_back(static_cast<int>(i));
            lastOnset = static_cast<int>(i);
        }
    }

    const double totalSec = static_cast<double>(numSamples) / sampleRate;
    const size_t numBins  = static_cast<size_t>(std::ceil(totalSec / segmentSec));
    density.assign(numBins, 0.0f);
    for (int f : onsetFrames) {
        const double sec = f * frameSec;
        size_t bin = static_cast<size_t>(sec / segmentSec);
        if (bin >= numBins) bin = numBins - 1;
        density[bin] += 1.0f;
    }
    for (float& v : density) v = static_cast<float>(v / segmentSec); // per second

    return density;
}

// 4/4 supposé.
std::vector<double> PhraseAnalyzer::segmentBoundaries(int numSamples,
                                                     double sampleRate,
                                                     double bpm,
                                                     const std::vector<float>& envelope,
                                                     double envHopSec) const {
    std::vector<double> bounds;
    if (numSamples <= 0 || sampleRate <= 0.0) return bounds;

    const double totalSec = static_cast<double>(numSamples) / sampleRate;

    double phraseSec = 16.0;
    if (bpm > 20.0 && bpm < 300.0) {
        const double beatsPerBar = 4.0;
        const double secPerBeat  = 60.0 / bpm;
        phraseSec = phraseBars_ * beatsPerBar * secPerBeat;
    }
    if (phraseSec < 4.0) phraseSec = 4.0;

    bounds.push_back(0.0);
    for (double t = phraseSec; t < totalSec - 0.5 * phraseSec; t += phraseSec) {
        bounds.push_back(t);
    }
    bounds.push_back(totalSec);

    if (!envelope.empty() && envHopSec > 0.0) {
        const int snapRadius = static_cast<int>(1.0 / envHopSec);
        for (size_t bi = 1; bi + 1 < bounds.size(); ++bi) {
            const size_t centerIdx = secToIndex(bounds[bi], envHopSec, envelope.size());
            const int lo = std::max(0, static_cast<int>(centerIdx) - snapRadius);
            const int hi = std::min(static_cast<int>(envelope.size()) - 1,
                                    static_cast<int>(centerIdx) + snapRadius);
            int bestIdx   = static_cast<int>(centerIdx);
            float bestJump = 0.0f;
            for (int i = lo + 1; i <= hi; ++i) {
                const float jump = std::fabs(envelope[i] - envelope[i - 1]);
                if (jump > bestJump) { bestJump = jump; bestIdx = i; }
            }
            if (bestJump > 0.15f) {
                bounds[bi] = static_cast<double>(bestIdx) * envHopSec;
            }
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    }

    return bounds;
}

PhraseType PhraseAnalyzer::classifyPhrase(double startSec,
                                          double endSec,
                                          double trackDurationSec,
                                          const std::vector<float>& envelope,
                                          double envHopSec,
                                          const std::vector<float>& onsetDensity,
                                          double densityHopSec,
                                          float envMedian,
                                          float envMax,
                                          float densityMedian,
                                          float densityMax,
                                          int phraseIndex,
                                          int numPhrases,
                                          float& outConfidence) const {
    const size_t i0 = secToIndex(startSec, envHopSec, envelope.size());
    const size_t i1 = secToIndex(endSec,   envHopSec, envelope.size());
    const float e      = mean(envelope, i0, std::max(i0 + 1, i1));
    const float eSlope = slope(envelope, i0, std::max(i0 + 1, i1));

    const size_t d0 = secToIndex(startSec, densityHopSec, onsetDensity.size());
    const size_t d1 = secToIndex(endSec,   densityHopSec, onsetDensity.size());
    const float dens = mean(onsetDensity, d0, std::max(d0 + 1, d1));

    const float eNorm    = (envMax     > 1e-9f) ? clamp01(e    / envMax)     : 0.0f;
    const float densNorm = (densityMax > 1e-9f) ? clamp01(dens / densityMax) : 0.0f;
    const float slopeNorm = clamp01(std::fabs(eSlope) * 50.0f);
    const float slopeSign = (eSlope > 0.0f) ? 1.0f : -1.0f;

    const bool isStart = (startSec < introOutroSec_);
    const bool isEnd   = (endSec > trackDurationSec - introOutroSec_);

    // Prototypes (énergie, densité, pente signée) en [0..1], pente [-1..1].
    struct Proto { PhraseType t; float e; float d; float slope; float bias; };
    const Proto protos[] = {
        { PhraseType::Intro,  0.20f, 0.20f,  0.10f, isStart          ? -0.30f : 0.10f },
        { PhraseType::Outro,  0.20f, 0.15f, -0.10f, isEnd            ? -0.30f : 0.10f },
        { PhraseType::Chorus, 0.90f, 0.85f,  0.00f, 0.00f },
        { PhraseType::Up,     0.60f, 0.60f,  0.60f, 0.00f },
        { PhraseType::Down,   0.55f, 0.45f, -0.60f, 0.00f },
        { PhraseType::Verse,  0.50f, 0.50f,  0.00f, 0.00f },
        { PhraseType::Bridge, 0.45f, 0.35f,  0.00f, 0.10f },
    };

    const float slopeSigned = slopeNorm * slopeSign;

    PhraseType best = PhraseType::Unknown;
    float bestDist  = 1e9f;
    float secondDist = 1e9f;

    for (const Proto& pr : protos) {
        const float de = eNorm       - pr.e;
        const float dd = densNorm    - pr.d;
        const float ds = slopeSigned - pr.slope;
        const float dist = de * de + dd * dd + 0.5f * ds * ds + pr.bias;
        if (dist < bestDist) {
            secondDist = bestDist;
            bestDist = dist;
            best = pr.t;
        } else if (dist < secondDist) {
            secondDist = dist;
        }
    }

    if (secondDist >= 1e8f) {
        outConfidence = 0.5f;
    } else {
        const float denom = bestDist + secondDist + 1e-6f;
        float conf = (secondDist - bestDist) / denom;
        conf = 0.5f + 0.5f * clamp01(conf * 2.0f);
        outConfidence = clamp01(conf);
    }

    if (phraseIndex == 0 && best != PhraseType::Chorus && e < envMedian * 1.1f) {
        best = PhraseType::Intro;
        outConfidence = std::max(outConfidence, 0.75f);
    }
    if (phraseIndex == numPhrases - 1 && best != PhraseType::Chorus && e < envMedian * 1.1f) {
        best = PhraseType::Outro;
        outConfidence = std::max(outConfidence, 0.75f);
    }

    if (best == PhraseType::Chorus && e < envMedian) {
        best = PhraseType::Verse;
        outConfidence *= 0.8f;
    }

    (void)densityMedian;
    return best;
}

std::vector<Phrase> PhraseAnalyzer::analyze(const float* audio,
                                            int numSamples,
                                            double sampleRate,
                                            double bpm) {
    std::vector<Phrase> out;
    if (audio == nullptr || numSamples <= 0 || sampleRate <= 0.0) return out;

    const double totalSec = static_cast<double>(numSamples) / sampleRate;
    if (totalSec < 4.0) {
        Phrase p;
        p.startSec = 0.0;
        p.endSec = totalSec;
        p.type = PhraseType::Unknown;
        p.confidence = 0.0f;
        out.push_back(p);
        return out;
    }

    double envHopSec = 0.0;
    auto envelope = computeEnergyEnvelope(audio, numSamples, sampleRate, envHopSec);
    auto density  = computeOnsetDensity(audio, numSamples, sampleRate, densitySegSec_);

    if (envelope.empty() || envHopSec <= 0.0) return out;

    const float envMedian = percentile(envelope, 0.5f);
    const float envMax    = percentile(envelope, 0.95f);
    const float densMedian = density.empty() ? 0.0f : percentile(density, 0.5f);
    const float densMax    = density.empty() ? 1.0f : std::max(percentile(density, 0.95f), 1e-6f);

    if (bpm <= 20.0 || bpm >= 300.0) {
        Phrase p;
        p.startSec = 0.0;
        p.endSec = totalSec;
        p.type = PhraseType::Unknown;
        out.push_back(p);
        return out;
    }
    const double effBpm = bpm;
    auto bounds = segmentBoundaries(numSamples, sampleRate, effBpm, envelope, envHopSec);
    if (bounds.size() < 2) {
        Phrase p;
        p.startSec = 0.0;
        p.endSec = totalSec;
        p.type = PhraseType::Unknown;
        out.push_back(p);
        return out;
    }

    const int numPhrases = static_cast<int>(bounds.size()) - 1;
    out.reserve(static_cast<size_t>(numPhrases));

    for (int i = 0; i < numPhrases; ++i) {
        Phrase ph;
        ph.startSec = bounds[i];
        ph.endSec   = bounds[i + 1];
        float conf = 0.0f;
        ph.type = classifyPhrase(ph.startSec, ph.endSec, totalSec,
                                 envelope, envHopSec,
                                 density, densitySegSec_,
                                 envMedian, envMax,
                                 densMedian, densMax,
                                 i, numPhrases,
                                 conf);
        ph.confidence = conf;
        out.push_back(ph);
    }

    // Au plus un Chorus sur deux phrases adjacentes.
    for (size_t i = 1; i < out.size(); ++i) {
        if (out[i].type == PhraseType::Chorus && out[i - 1].type == PhraseType::Chorus) {
            const size_t a0 = secToIndex(out[i - 1].startSec, envHopSec, envelope.size());
            const size_t a1 = secToIndex(out[i - 1].endSec,   envHopSec, envelope.size());
            const size_t b0 = secToIndex(out[i].startSec,     envHopSec, envelope.size());
            const size_t b1 = secToIndex(out[i].endSec,       envHopSec, envelope.size());
            const float ea = mean(envelope, a0, std::max(a0 + 1, a1));
            const float eb = mean(envelope, b0, std::max(b0 + 1, b1));
            if (ea >= eb) {
                out[i].type = PhraseType::Verse;
                out[i].confidence *= 0.8f;
            } else {
                out[i - 1].type = PhraseType::Verse;
                out[i - 1].confidence *= 0.8f;
            }
        }
    }

    return out;
}

std::vector<Phrase> PhraseAnalyzer::analyze(const Core::AudioTrack& track) {
    if (!track.isLoaded()) return {};

    const int sr       = track.getSampleRate();
    const int channels = std::max(1, track.getChannels());
    const size_t frames = track.getNumFrames();
    if (sr <= 0 || frames == 0) return {};

    std::vector<float> mono(frames, 0.0f);
    const float* raw = track.getRawData();
    if (raw != nullptr) {
        for (size_t f = 0; f < frames; ++f) {
            mono[f] = raw[f * static_cast<size_t>(channels)];
        }
    } else {
        for (size_t f = 0; f < frames; ++f) {
            mono[f] = track.getSample(f, 0);
        }
    }

    return analyze(mono.data(),
                   static_cast<int>(mono.size()),
                   static_cast<double>(sr),
                   0.0);
}

} // namespace BeatMate::Core::Analysis
