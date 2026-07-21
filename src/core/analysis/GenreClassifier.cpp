#include "GenreClassifier.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include "BPMDetector.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

GenreClassifier::GenreClassifier() = default;
GenreClassifier::~GenreClassifier() = default;

bool GenreClassifier::loadModel(const std::string& modelPath) {
    spdlog::info("GenreClassifier: model path set to {}", modelPath);
    modelLoaded_ = false; // Will use feature-based heuristics
    return false;
}

GenreClassifier::Features GenreClassifier::extractFeatures(const AudioTrack& track) {
    Features f;
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    BPMDetector bpmDet;
    auto bpmResult = bpmDet.detect(track);
    f.bpm = static_cast<float>(bpmResult.bpm);

    double sumSq = 0;
    for (size_t i = 0; i < numSamples; ++i) sumSq += data[i] * data[i];
    f.rmsEnergy = static_cast<float>(std::sqrt(sumSq / numSamples));

    int zcr = 0;
    for (size_t i = 1; i < numSamples; ++i) {
        if ((data[i] > 0 && data[i-1] < 0) || (data[i] < 0 && data[i-1] > 0)) zcr++;
    }
    f.zeroCrossingRate = static_cast<float>(zcr) / numSamples;

    int fftSize = 4096;
    FFTProcessor fft(fftSize);
    size_t analysisFrames = std::min(numSamples, static_cast<size_t>(10 * sr));
    if (analysisFrames >= static_cast<size_t>(fftSize)) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + numSamples / 4, spectrum); // Middle of track
        auto mag = fft.getMagnitudes(spectrum);

        double weightedSum = 0, magSum = 0;
        for (int i = 1; i < static_cast<int>(mag.size()); ++i) {
            double freq = static_cast<double>(i) * sr / fftSize;
            weightedSum += freq * mag[i];
            magSum += mag[i];
        }
        f.spectralCentroid = (magSum > 0) ? static_cast<float>(weightedSum / magSum) : 0;

        // Rolloff (85% of spectral energy)
        double target = magSum * 0.85;
        double cumSum = 0;
        for (int i = 1; i < static_cast<int>(mag.size()); ++i) {
            cumSum += mag[i];
            if (cumSum >= target) {
                f.spectralRolloff = static_cast<float>(static_cast<double>(i) * sr / fftSize);
                break;
            }
        }
    }

    return f;
}

std::vector<GenreScore> GenreClassifier::classifyFromFeatures(const Features& f) {
    std::vector<GenreScore> scores;


    auto addGenre = [&](const std::string& name, float score) {
        scores.push_back({name, std::clamp(score, 0.0f, 1.0f)});
    };

    // House: 120-130 BPM, moderate energy
    float houseLikelihood = 0.0f;
    if (f.bpm >= 118 && f.bpm <= 132) houseLikelihood += 0.5f;
    if (f.rmsEnergy > 0.05f && f.rmsEnergy < 0.2f) houseLikelihood += 0.3f;
    addGenre("House", houseLikelihood);

    // Techno: 125-145 BPM, high energy
    float technoLikelihood = 0.0f;
    if (f.bpm >= 125 && f.bpm <= 145) technoLikelihood += 0.4f;
    if (f.rmsEnergy > 0.1f) technoLikelihood += 0.3f;
    if (f.spectralCentroid > 3000) technoLikelihood += 0.2f;
    addGenre("Techno", technoLikelihood);

    // DnB: 170-180 BPM
    float dnbLikelihood = 0.0f;
    if (f.bpm >= 165 && f.bpm <= 185) dnbLikelihood += 0.6f;
    if (f.rmsEnergy > 0.08f) dnbLikelihood += 0.2f;
    addGenre("Drum & Bass", dnbLikelihood);

    // Hip-Hop: 80-100 BPM
    float hipHopLikelihood = 0.0f;
    if (f.bpm >= 75 && f.bpm <= 105) hipHopLikelihood += 0.5f;
    if (f.spectralCentroid < 2000) hipHopLikelihood += 0.2f;
    addGenre("Hip-Hop", hipHopLikelihood);

    // Trance: 128-142 BPM
    float tranceLikelihood = 0.0f;
    if (f.bpm >= 128 && f.bpm <= 142) tranceLikelihood += 0.4f;
    if (f.spectralCentroid > 2000 && f.spectralCentroid < 5000) tranceLikelihood += 0.2f;
    addGenre("Trance", tranceLikelihood);

    // Dubstep: 140 BPM (half-time 70)
    float dubstepLikelihood = 0.0f;
    if ((f.bpm >= 138 && f.bpm <= 142) || (f.bpm >= 68 && f.bpm <= 72)) dubstepLikelihood += 0.5f;
    if (f.spectralCentroid < 1500) dubstepLikelihood += 0.2f;
    addGenre("Dubstep", dubstepLikelihood);

    // Pop: 100-130 BPM
    float popLikelihood = 0.0f;
    if (f.bpm >= 100 && f.bpm <= 130) popLikelihood += 0.3f;
    if (f.zeroCrossingRate < 0.05f) popLikelihood += 0.2f;
    addGenre("Pop", popLikelihood);

    std::sort(scores.begin(), scores.end(),
              [](const auto& a, const auto& b) { return a.confidence > b.confidence; });

    return scores;
}

std::vector<GenreScore> GenreClassifier::classify(const AudioTrack& track) {
    spdlog::info("GenreClassifier: analyzing {}", track.getFilePath());
    auto features = extractFeatures(track);
    auto scores = classifyFromFeatures(features);

    if (!scores.empty()) {
        spdlog::info("GenreClassifier: top genre = {} ({:.0f}%)",
                     scores[0].genre, scores[0].confidence * 100);
    }
    return scores;
}

} // namespace BeatMate::Core
