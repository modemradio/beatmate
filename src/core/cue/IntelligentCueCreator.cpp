#include "IntelligentCueCreator.h"
#include "../audio/AudioTrack.h"
#include "../analysis/BPMDetector.h"
#include "../analysis/StructureDetector.h"
#include "../analysis/EnergyAnalyzer.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

IntelligentCueCreator::IntelligentCueCreator() = default;
IntelligentCueCreator::~IntelligentCueCreator() = default;

uint32_t IntelligentCueCreator::colorForSection(const std::string& type) {
    if (type == "Intro") return 0xFF00BFFF;     // Deep sky blue
    if (type == "Verse") return 0xFF32CD32;     // Lime green
    if (type == "Chorus") return 0xFFFF4500;    // Orange red
    if (type == "Drop") return 0xFFFF0000;      // Red
    if (type == "Breakdown") return 0xFF9370DB; // Medium purple
    if (type == "Buildup") return 0xFFFFD700;   // Gold
    if (type == "Bridge") return 0xFF20B2AA;    // Light sea green
    if (type == "Outro") return 0xFF4169E1;     // Royal blue
    return 0xFFFFFFFF;                          // White default
}

double IntelligentCueCreator::snapToNearestBeat(double position, const std::vector<double>& beats,
                                                  double bpm) {
    if (beats.empty()) return position;

    double bestDist = 1e10;
    double bestBeat = position;

    for (double beat : beats) {
        double dist = std::fabs(beat - position);
        if (dist < bestDist) {
            bestDist = dist;
            bestBeat = beat;
        }
    }

    if (snapToDownbeat_) {
        double barDuration = 240.0 / bpm;
        double barBeat = std::round(bestBeat / barDuration) * barDuration;

        double beatDuration = 60.0 / bpm;
        if (std::fabs(barBeat - position) < beatDuration * 2.0) {
            for (double beat : beats) {
                if (std::fabs(beat - barBeat) < beatDuration * 0.5) {
                    return beat;
                }
            }
        }
    }

    return bestBeat;
}

float IntelligentCueCreator::scoreEnergyTransition(const AudioTrack& track, double position) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t samplePos = static_cast<size_t>(position * sr);
    int windowSize = static_cast<int>(0.5 * sr);

    if (samplePos + windowSize >= numSamples || samplePos < static_cast<size_t>(windowSize)) return 0.0f;

    float energyBefore = 0.0f, energyAfter = 0.0f;
    for (int i = 0; i < windowSize; ++i) {
        energyBefore += data[samplePos - windowSize + i] * data[samplePos - windowSize + i];
        energyAfter += data[samplePos + i] * data[samplePos + i];
    }
    energyBefore = std::sqrt(energyBefore / windowSize);
    energyAfter = std::sqrt(energyAfter / windowSize);

    float diff = std::fabs(energyAfter - energyBefore);
    return std::clamp(diff * 5.0f, 0.0f, 1.0f);
}

std::vector<IntelligentCueCreator::CueCandidate> IntelligentCueCreator::generateCandidates(
    const AudioTrack& track, const std::vector<Section>& /*sections*/,
    const std::vector<double>& beats, double bpm) {

    std::vector<CueCandidate> candidates;
    double duration = track.getDuration();

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();
    if (sr <= 0 || numSamples == 0 || bpm <= 0) return candidates;

    double beatDur = 60.0 / bpm;
    double barDur = beatDur * 4.0;
    double phraseDur = barDur * 16.0;
    int numPhrases = std::max(1, static_cast<int>(duration / phraseDur));


    int fftSize = 1024;
    int hopSize = fftSize / 2;
    int numFrames = static_cast<int>((numSamples - fftSize) / hopSize);
    if (numFrames < 2) return candidates;

    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    int numBins = fftSize / 2;
    std::vector<float> prevMag(numBins, 0.0f);
    std::vector<float> spectralFlux(numFrames, 0.0f);
    std::vector<float> energy(numFrames, 0.0f);
    std::vector<float> bassEnergy(numFrames, 0.0f);

    int bassBinLimit = std::min(static_cast<int>(250.0 * fftSize / sr), numBins);

    for (int f = 0; f < numFrames; ++f) {
        size_t offset = static_cast<size_t>(f) * hopSize;
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        float flux = 0.0f;
        float totalE = 0.0f;
        float bassE = 0.0f;

        for (int b = 0; b < numBins && b < static_cast<int>(mag.size()); ++b) {
            float diff = mag[b] - prevMag[b];
            if (diff > 0) flux += diff * diff;
            totalE += mag[b] * mag[b];
            if (b < bassBinLimit) bassE += mag[b] * mag[b];
            prevMag[b] = mag[b];
        }

        spectralFlux[f] = std::sqrt(flux);
        energy[f] = std::sqrt(totalE);
        bassEnergy[f] = std::sqrt(bassE);
    }

    double frameDur = static_cast<double>(hopSize) / sr;
    int framesPerPhrase = std::max(1, static_cast<int>(phraseDur / frameDur));

    struct PhraseInfo {
        double startTime;
        float avgEnergy;
        float avgBass;
        float peakFlux;     // max spectral flux at phrase boundary
        float avgFlux;
    };
    std::vector<PhraseInfo> phrases;

    for (int p = 0; p < numPhrases; ++p) {
        int f0 = p * framesPerPhrase;
        int f1 = std::min(f0 + framesPerPhrase, numFrames);
        if (f0 >= numFrames) break;

        float sumE = 0, sumB = 0, sumF = 0, peakF = 0;
        for (int f = f0; f < f1; ++f) {
            sumE += energy[f];
            sumB += bassEnergy[f];
            sumF += spectralFlux[f];
            peakF = std::max(peakF, spectralFlux[f]);
        }
        int count = f1 - f0;
        phrases.push_back({
            p * phraseDur,
            sumE / count,
            sumB / count,
            peakF,
            sumF / count
        });
    }

    if (phrases.empty()) return candidates;

    float maxE = 0, maxB = 0, maxF = 0;
    for (auto& p : phrases) {
        maxE = std::max(maxE, p.avgEnergy);
        maxB = std::max(maxB, p.avgBass);
        maxF = std::max(maxF, p.peakFlux);
    }
    if (maxE > 0) for (auto& p : phrases) p.avgEnergy /= maxE;
    if (maxB > 0) for (auto& p : phrases) p.avgBass /= maxB;
    if (maxF > 0) for (auto& p : phrases) { p.peakFlux /= maxF; p.avgFlux /= maxF; }


    {
        CueCandidate c;
        c.position = beats.empty() ? 0.0 : beats[0];
        c.score = 0.6f;
        c.reason = "Debut";
        c.sectionType = "Intro";
        c.color = 0xFF00BFFF;
        candidates.push_back(c);
    }

    for (size_t i = 1; i < phrases.size(); ++i) {
        auto& prev = phrases[i - 1];
        auto& curr = phrases[i];

        float energyDiff = curr.avgEnergy - prev.avgEnergy;
        float bassDiff = curr.avgBass - prev.avgBass;
        float fluxScore = curr.peakFlux;

        float transitionScore = fluxScore * 0.4f + std::abs(energyDiff) * 0.3f + std::abs(bassDiff) * 0.3f;

        double pos = snapToNearestBeat(curr.startTime, beats, bpm);
        CueCandidate c;
        c.position = pos;

        if (energyDiff > 0.15f && bassDiff > 0.15f && fluxScore > 0.4f) {
            c.score = prioritizeDrops_ ? 1.0f : 0.95f;
            c.reason = "Drop";
            c.sectionType = "Drop";
            c.color = 0xFFFF0000;
            candidates.push_back(c);
        }
        else if (energyDiff > 0.10f && fluxScore > 0.25f) {
            c.score = 0.85f;
            c.reason = "Chorus";
            c.sectionType = "Chorus";
            c.color = 0xFFFF4500;
            candidates.push_back(c);
        }
        else if (energyDiff < -0.20f && bassDiff < -0.15f) {
            c.score = 0.75f;
            c.reason = "Breakdown";
            c.sectionType = "Breakdown";
            c.color = 0xFF9370DB;
            candidates.push_back(c);
        }
        else if (energyDiff > 0.05f && curr.avgFlux > prev.avgFlux * 1.3f && curr.avgEnergy < 0.7f) {
            c.score = 0.70f;
            c.reason = "Buildup";
            c.sectionType = "Buildup";
            c.color = 0xFFFFD700;
            candidates.push_back(c);
        }
        else if (fluxScore > 0.5f && transitionScore > 0.35f) {
            c.score = 0.65f;
            c.reason = "Nouvelle section";
            c.sectionType = "Section";
            c.color = 0xFF20B2AA;
            candidates.push_back(c);
        }
    }

    for (size_t i = 0; i < phrases.size(); ++i) {
        if (phrases[i].avgEnergy > 0.3f && phrases[i].avgBass > 0.25f) {
            double pos = snapToNearestBeat(phrases[i].startTime, beats, bpm);
            bool exists = false;
            for (auto& c : candidates)
                if (std::fabs(c.position - pos) < barDur * 2) { exists = true; break; }
            if (!exists && pos > barDur * 4) {
                CueCandidate c;
                c.position = pos;
                c.score = 0.75f;
                c.reason = "Mix-in";
                c.sectionType = "MixIn";
                c.color = 0xFF32CD32;
                candidates.push_back(c);
            }
            break;
        }
    }

    for (int i = static_cast<int>(phrases.size()) - 1; i >= 1; --i) {
        if (phrases[i].avgEnergy < 0.25f && phrases[i - 1].avgEnergy > 0.5f) {
            double pos = snapToNearestBeat(phrases[i].startTime, beats, bpm);
            bool exists = false;
            for (auto& c : candidates)
                if (std::fabs(c.position - pos) < barDur * 2) { exists = true; break; }
            if (!exists) {
                CueCandidate c;
                c.position = pos;
                c.score = 0.70f;
                c.reason = "Mix-out";
                c.sectionType = "Outro";
                c.color = 0xFF4169E1;
                candidates.push_back(c);
            }
            break;
        }
    }

    return candidates;
}

std::vector<IntelligentCueCreator::CueCandidate> IntelligentCueCreator::selectOptimalCues(
    const std::vector<CueCandidate>& candidates, int maxCues, double trackDuration) {

    if (static_cast<int>(candidates.size()) <= maxCues) return candidates;

    auto sorted = candidates;
    std::sort(sorted.begin(), sorted.end(),
              [](const CueCandidate& a, const CueCandidate& b) { return a.score > b.score; });

    double minDistance = std::max(8.0, trackDuration / (maxCues * 1.5));
    std::vector<CueCandidate> selected;

    for (auto& candidate : sorted) {
        if (static_cast<int>(selected.size()) >= maxCues) break;

        bool tooClose = false;
        for (auto& sel : selected) {
            if (std::fabs(sel.position - candidate.position) < minDistance) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            selected.push_back(candidate);
        }
    }

    std::sort(selected.begin(), selected.end(),
              [](const CueCandidate& a, const CueCandidate& b) { return a.position < b.position; });

    return selected;
}

IntelligentCueResult IntelligentCueCreator::generateCues(
    const AudioTrack& track, const std::vector<Section>& sections,
    const std::vector<double>& beats, double bpm, int maxCues) {

    spdlog::info("IntelligentCueCreator: generating {} intelligent cues", maxCues);

    IntelligentCueResult result;
    result.requestedCues = maxCues;

    auto candidates = generateCandidates(track, sections, beats, bpm);
    auto selected = selectOptimalCues(candidates, maxCues, track.getDuration());

    for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
        IntelligentCue ic;
        ic.cue.number = i + 1;
        ic.cue.position = selected[i].position;
        ic.cue.color = selected[i].color;
        ic.cue.name = selected[i].reason;
        ic.reason = selected[i].reason;
        ic.importance = selected[i].score;
        ic.sectionType = selected[i].sectionType;
        ic.isDownbeat = true;

        result.cues.push_back(ic);
    }

    float confSum = 0.0f;
    for (auto& c : result.cues) confSum += c.importance;
    result.overallConfidence = result.cues.empty() ? 0.0f : confSum / result.cues.size();

    spdlog::info("IntelligentCueCreator: generated {} cues, confidence {:.0f}%",
                 result.cues.size(), result.overallConfidence * 100);
    return result;
}

IntelligentCueResult IntelligentCueCreator::generateCues(const AudioTrack& track, int maxCues) {
    BPMDetector bpmDet;
    auto bpmResult = bpmDet.detect(track);

    StructureDetector structDet;
    auto sections = structDet.detect(track);

    return generateCues(track, sections, bpmResult.beats, bpmResult.bpm, maxCues);
}

} // namespace BeatMate::Core
