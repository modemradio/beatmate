#include "KeyDetector.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FFTProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

KeyDetector::KeyDetector() = default;
KeyDetector::~KeyDetector() = default;

KeyDetector::KeyProfile KeyDetector::getKrumhanslProfile() {
    KeyProfile p;
    p.major[0]  = 6.35; // C
    p.major[1]  = 2.23; // C#
    p.major[2]  = 3.48; // D
    p.major[3]  = 2.33; // D#
    p.major[4]  = 4.38; // E
    p.major[5]  = 4.09; // F
    p.major[6]  = 2.52; // F#
    p.major[7]  = 5.19; // G
    p.major[8]  = 2.39; // G#
    p.major[9]  = 3.66; // A
    p.major[10] = 2.29; // A#
    p.major[11] = 2.88; // B

    p.minor[0]  = 6.33; // C
    p.minor[1]  = 2.68; // C#
    p.minor[2]  = 3.52; // D
    p.minor[3]  = 5.38; // D#
    p.minor[4]  = 2.60; // E
    p.minor[5]  = 3.53; // F
    p.minor[6]  = 2.54; // F#
    p.minor[7]  = 4.75; // G
    p.minor[8]  = 3.98; // G#
    p.minor[9]  = 2.69; // A
    p.minor[10] = 3.34; // A#
    p.minor[11] = 3.17; // B

    return p;
}

std::vector<double> KeyDetector::extractChromagram(const AudioTrack& track) {
    auto monoTrack = track.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    int fftSize = 4096;
    int hopSize = 2048;
    FFTProcessor fft(fftSize);
    fft.setWindow(WindowType::Hann);

    std::vector<double> chromagram(12, 0.0);
    int numFrames = 0;

    for (size_t offset = 0; offset + fftSize <= numSamples; offset += hopSize) {
        std::vector<std::complex<float>> spectrum;
        fft.forward(data + offset, spectrum);
        auto mag = fft.getMagnitudes(spectrum);

        for (int bin = 1; bin < static_cast<int>(mag.size()); ++bin) {
            double freq = static_cast<double>(bin) * sr / fftSize;
            if (freq < 60.0 || freq > 5000.0) continue;

            double midiNote = 12.0 * std::log2(freq / 440.0) + 69.0;
            int chromaBin = static_cast<int>(std::round(midiNote)) % 12;
            if (chromaBin < 0) chromaBin += 12;

            chromagram[chromaBin] += mag[bin] * mag[bin]; // Use power
        }
        numFrames++;
    }

    if (numFrames > 0) {
        double maxVal = *std::max_element(chromagram.begin(), chromagram.end());
        if (maxVal > 0) {
            for (auto& v : chromagram) v /= maxVal;
        }
    }

    return chromagram;
}

KeyResult KeyDetector::matchKey(const std::vector<double>& chromagram) {
    auto profile = getKrumhanslProfile();

    double bestCorrelation = -2.0;
    int bestKey = 0;
    bool bestIsMinor = false;

    for (int key = 0; key < 12; ++key) {
        double corrMajor = 0.0, corrMinor = 0.0;
        double sumCh = 0, sumMaj = 0, sumMin = 0;
        double sumChSq = 0, sumMajSq = 0, sumMinSq = 0;
        double sumChMaj = 0, sumChMin = 0;

        for (int i = 0; i < 12; ++i) {
            int rotated = (i + key) % 12;
            double ch = chromagram[rotated];
            double maj = profile.major[i];
            double min = profile.minor[i];

            sumCh += ch;
            sumMaj += maj;
            sumMin += min;
            sumChSq += ch * ch;
            sumMajSq += maj * maj;
            sumMinSq += min * min;
            sumChMaj += ch * maj;
            sumChMin += ch * min;
        }

        double n = 12.0;
        double denomMaj = std::sqrt((n * sumChSq - sumCh * sumCh) * (n * sumMajSq - sumMaj * sumMaj));
        double denomMin = std::sqrt((n * sumChSq - sumCh * sumCh) * (n * sumMinSq - sumMin * sumMin));

        if (denomMaj > 0) corrMajor = (n * sumChMaj - sumCh * sumMaj) / denomMaj;
        if (denomMin > 0) corrMinor = (n * sumChMin - sumCh * sumMin) / denomMin;

        if (corrMajor > bestCorrelation) {
            bestCorrelation = corrMajor;
            bestKey = key;
            bestIsMinor = false;
        }
        if (corrMinor > bestCorrelation) {
            bestCorrelation = corrMinor;
            bestKey = key;
            bestIsMinor = true;
        }
    }

    KeyResult result;
    result.pitchClass = bestKey;
    result.isMinor = bestIsMinor;
    result.key = toKeyName(bestKey, bestIsMinor);
    result.camelotKey = toCamelotKey(bestKey, bestIsMinor);
    result.confidence = std::clamp((bestCorrelation + 1.0) / 2.0, 0.0, 1.0);

    return result;
}

std::string KeyDetector::toKeyName(int pitchClass, bool isMinor) {
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    std::string name = noteNames[pitchClass % 12];
    if (isMinor) name += "m";
    return name;
}

std::string KeyDetector::toCamelotKey(int pitchClass, bool isMinor) {

    static const int majorCamelot[] = {8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1}; // C,C#,...,B
    static const int minorCamelot[] = {5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10};

    int num;
    char letter;
    if (isMinor) {
        num = minorCamelot[pitchClass];
        letter = 'A';
    } else {
        num = majorCamelot[pitchClass];
        letter = 'B';
    }

    return std::to_string(num) + letter;
}

KeyResult KeyDetector::detect(const AudioTrack& track) {
    spdlog::info("KeyDetector: analyzing {}", track.getFilePath());

    auto chromagram = extractChromagram(track);
    auto result = matchKey(chromagram);

    spdlog::info("KeyDetector: detected {} (Camelot: {}, confidence: {:.0f}%)",
                 result.key, result.camelotKey, result.confidence * 100);
    return result;
}

} // namespace BeatMate::Core
