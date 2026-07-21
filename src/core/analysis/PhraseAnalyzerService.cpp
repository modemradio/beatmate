#include "PhraseAnalyzerService.h"
#include "../audio/AudioTrack.h"
#include "BPMDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

PhraseAnalyzerService::PhraseAnalyzerService() = default;
PhraseAnalyzerService::~PhraseAnalyzerService() = default;

std::vector<float> PhraseAnalyzerService::computeBarEnergies(const AudioTrack& track, double barDuration) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t totalSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t barSamples = static_cast<size_t>(barDuration * sr);
    if (barSamples == 0) return {};

    size_t numBars = totalSamples / barSamples;
    std::vector<float> energies;
    energies.reserve(numBars);

    for (size_t bar = 0; bar < numBars; ++bar) {
        const float* barData = data + bar * barSamples;
        size_t len = std::min(barSamples, totalSamples - bar * barSamples);

        double sum = 0.0;
        for (size_t i = 0; i < len; ++i) sum += barData[i] * barData[i];
        energies.push_back(static_cast<float>(std::sqrt(sum / len)));
    }

    float maxVal = 0.0f;
    for (auto& v : energies) maxVal = std::max(maxVal, v);
    if (maxVal > 0) {
        for (auto& v : energies) v /= maxVal;
    }

    return energies;
}

std::vector<PhraseInfo> PhraseAnalyzerService::detectPhrases(
    const std::vector<float>& barEnergies, double barDuration, int phraseBars) {

    std::vector<PhraseInfo> phrases;
    int totalBars = static_cast<int>(barEnergies.size());
    int phraseNum = 1;

    std::vector<float> boundaryStrengths;
    for (int bar = 0; bar < totalBars; bar += phraseBars) {
        float strength = 0.0f;
        if (bar > 0 && bar < totalBars) {
            float prevEnergy = 0.0f, nextEnergy = 0.0f;
            int window = std::min(2, phraseBars / 2);

            for (int i = std::max(0, bar - window); i < bar; ++i) prevEnergy += barEnergies[i];
            for (int i = bar; i < std::min(totalBars, bar + window); ++i) nextEnergy += barEnergies[i];

            prevEnergy /= window;
            nextEnergy /= window;
            strength = std::fabs(nextEnergy - prevEnergy);
        }
        boundaryStrengths.push_back(strength);
    }

    for (int bar = 0; bar + phraseBars <= totalBars; bar += phraseBars) {
        PhraseInfo phrase;
        phrase.startTime = bar * barDuration;
        phrase.endTime = (bar + phraseBars) * barDuration;
        phrase.bars = phraseBars;
        phrase.phraseNumber = phraseNum++;
        phrase.label = "Phrase " + std::to_string(phrase.phraseNumber) +
                       " (" + std::to_string(phraseBars) + " bars)";

        float sum = 0.0f;
        for (int i = bar; i < bar + phraseBars && i < totalBars; ++i) {
            sum += barEnergies[i];
        }
        phrase.energy = sum / phraseBars;

        int bIdx = bar / phraseBars;
        phrase.confidence = (bIdx < static_cast<int>(boundaryStrengths.size()))
                            ? std::clamp(boundaryStrengths[bIdx] * 3.0f + 0.3f, 0.0f, 1.0f)
                            : 0.5f;

        if (bar < totalBars && barEnergies[bar] > phrase.energy * 0.8f) {
            phrase.isDownbeat = true;
        }

        phrases.push_back(phrase);
    }

    return phrases;
}

int PhraseAnalyzerService::findDominantPhrase(const std::vector<float>& barEnergies, double barDuration) {
    int bestPhrase = 8;
    float bestScore = 0.0f;

    for (int phraseBars : {4, 8, 16, 32}) {
        int totalBars = static_cast<int>(barEnergies.size());
        if (phraseBars > totalBars / 2) continue;

        // Score: how much energy changes at phrase boundaries vs. within phrases
        float boundaryChange = 0.0f;
        float withinChange = 0.0f;
        int boundaryCount = 0, withinCount = 0;

        for (int bar = 1; bar < totalBars; ++bar) {
            float change = std::fabs(barEnergies[bar] - barEnergies[bar - 1]);
            if (bar % phraseBars == 0) {
                boundaryChange += change;
                boundaryCount++;
            } else {
                withinChange += change;
                withinCount++;
            }
        }

        float avgBoundary = (boundaryCount > 0) ? boundaryChange / boundaryCount : 0.0f;
        float avgWithin = (withinCount > 0) ? withinChange / withinCount : 1.0f;

        float score = (avgWithin > 0) ? avgBoundary / avgWithin : 0.0f;

        if (score > bestScore) {
            bestScore = score;
            bestPhrase = phraseBars;
        }
    }

    return bestPhrase;
}

PhraseAnalysisResult PhraseAnalyzerService::analyze(const AudioTrack& track, double bpm) {
    spdlog::info("PhraseAnalyzerService: analyzing {}", track.getFilePath());

    PhraseAnalysisResult result;

    if (bpm <= 0) {
        BPMDetector detector;
        auto bpmResult = detector.detect(track);
        bpm = bpmResult.bpm;
    }
    result.bpm = bpm;

    double beatDuration = 60.0 / bpm;
    double barDuration = beatDuration * 4.0;

    auto barEnergies = computeBarEnergies(track, barDuration);
    if (barEnergies.empty()) return result;

    result.phrases4 = detectPhrases(barEnergies, barDuration, 4);
    result.phrases8 = detectPhrases(barEnergies, barDuration, 8);
    result.phrases16 = detectPhrases(barEnergies, barDuration, 16);
    result.phrases32 = detectPhrases(barEnergies, barDuration, 32);

    result.dominantPhraseLength = findDominantPhrase(barEnergies, barDuration);

    spdlog::info("PhraseAnalyzerService: dominant={}bars, phrases: 4={} 8={} 16={} 32={}",
                 result.dominantPhraseLength,
                 result.phrases4.size(), result.phrases8.size(),
                 result.phrases16.size(), result.phrases32.size());
    return result;
}

} // namespace BeatMate::Core
