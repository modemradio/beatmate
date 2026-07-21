#include "HarmonicAnalysisService.h"
#include "AudioAnalysisPipeline.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "KeyDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

HarmonicAnalysisService::HarmonicAnalysisService() = default;
HarmonicAnalysisService::~HarmonicAnalysisService() = default;

std::vector<HarmonicAnalysisService::ChordTemplate> HarmonicAnalysisService::getChordTemplates() {
    std::vector<ChordTemplate> templates;

    templates.push_back({"", "major", {1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0}});
    templates.push_back({"m", "minor", {1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}});
    templates.push_back({"7", "dominant7", {1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0}});
    templates.push_back({"maj7", "major7", {1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1}});
    templates.push_back({"m7", "minor7", {1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0}});
    templates.push_back({"dim", "diminished", {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0}});
    templates.push_back({"aug", "augmented", {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}});
    templates.push_back({"sus4", "sus4", {1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0}});
    templates.push_back({"sus2", "sus2", {1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0}});

    return templates;
}

std::vector<std::vector<float>> HarmonicAnalysisService::extractChromagram(
    const AudioTrack& track, int hopSize) {

    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 4096; // Higher resolution for harmonic analysis
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    // Guard against size_t underflow when numSamples < fftSize.
    int numFrames = static_cast<int>(safeFrameCount(numSamples,
                                                    static_cast<size_t>(fftSize),
                                                    static_cast<size_t>(hopSize)));
    std::vector<std::vector<float>> chromagram;
    chromagram.reserve(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        size_t offset = frame * hopSize;
        if (offset + fftSize > numSamples) break;

        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        std::vector<float> chroma(12, 0.0f);
        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            float freq = static_cast<float>(bin) * sr / fftSize;
            if (freq < 50.0f || freq > 5000.0f) continue;

            float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
            int pitchClass = static_cast<int>(std::round(midi)) % 12;
            if (pitchClass < 0) pitchClass += 12;
            chroma[pitchClass] += mag[bin] * mag[bin];
        }

        float maxVal = *std::max_element(chroma.begin(), chroma.end());
        if (maxVal > 0) {
            for (auto& c : chroma) c /= maxVal;
        }

        chromagram.push_back(chroma);
    }

    return chromagram;
}

ChordInfo HarmonicAnalysisService::recognizeChord(const std::vector<float>& chroma) {
    auto templates = getChordTemplates();
    static const char* noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    float bestScore = -1.0f;
    ChordInfo bestChord;

    for (int root = 0; root < 12; ++root) {
        for (auto& tmpl : templates) {
            float score = 0.0f;
            float norm1 = 0.0f, norm2 = 0.0f;

            for (int i = 0; i < 12; ++i) {
                int rotated = (i + root) % 12;
                score += chroma[rotated] * tmpl.pattern[i];
                norm1 += chroma[rotated] * chroma[rotated];
                norm2 += tmpl.pattern[i] * tmpl.pattern[i];
            }

            if (norm1 > 0 && norm2 > 0) {
                score /= (std::sqrt(norm1) * std::sqrt(norm2));
            }

            if (score > bestScore) {
                bestScore = score;
                bestChord.rootNote = root;
                bestChord.quality = tmpl.quality;
                bestChord.name = std::string(noteNames[root]) + tmpl.name;
                bestChord.confidence = score;
            }
        }
    }

    return bestChord;
}

HarmonicProfile HarmonicAnalysisService::analyze(const AudioTrack& track) {
    spdlog::info("HarmonicAnalysisService: analyzing {}", track.getFilePath());

    HarmonicProfile result;
    int sr = track.getSampleRate();
    int hopSize = static_cast<int>(chordResolution_ * sr);

    result.chromaOverTime = extractChromagram(track, hopSize);
    result.chromaFrameDuration = static_cast<double>(hopSize) / sr;

    if (result.chromaOverTime.empty()) return result;

    result.chromagram.resize(12, 0.0f);
    for (auto& frame : result.chromaOverTime) {
        for (int i = 0; i < 12; ++i) {
            result.chromagram[i] += frame[i];
        }
    }
    for (auto& c : result.chromagram) c /= result.chromaOverTime.size();

    KeyDetector keyDet;
    auto keyResult = keyDet.detect(track);
    result.key = keyResult.key;
    result.mode = keyResult.isMinor ? "minor" : "major";

    float maxChroma = *std::max_element(result.chromagram.begin(), result.chromagram.end());
    float avgChroma = 0.0f;
    for (auto& c : result.chromagram) avgChroma += c;
    avgChroma /= 12.0f;
    result.tonal = (avgChroma > 0) ? std::clamp((maxChroma / avgChroma - 1.0f) / 3.0f, 0.0f, 1.0f) : 0.0f;

    for (size_t f = 0; f < result.chromaOverTime.size(); ++f) {
        auto chord = recognizeChord(result.chromaOverTime[f]);
        chord.startTime = f * result.chromaFrameDuration;
        chord.endTime = (f + 1) * result.chromaFrameDuration;
        result.chords.push_back(chord);
    }

    std::vector<ChordInfo> merged;
    if (!result.chords.empty()) {
        ChordInfo current = result.chords[0];
        for (size_t i = 1; i < result.chords.size(); ++i) {
            if (result.chords[i].name == current.name) {
                current.endTime = result.chords[i].endTime;
                current.confidence = std::max(current.confidence, result.chords[i].confidence);
            } else {
                merged.push_back(current);
                current = result.chords[i];
            }
        }
        merged.push_back(current);
    }
    result.chords = merged;

    spdlog::info("HarmonicAnalysisService: key={} {}, {} chords, tonality={:.2f}",
                 result.key, result.mode, result.chords.size(), result.tonal);
    return result;
}

} // namespace BeatMate::Core
