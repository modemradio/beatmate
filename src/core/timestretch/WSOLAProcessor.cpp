#include "WSOLAProcessor.h"
#include "../audio/AudioTrack.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

WSOLAProcessor::WSOLAProcessor() = default;
WSOLAProcessor::~WSOLAProcessor() = default;

int WSOLAProcessor::findBestOverlap(const float* src, const float* target, int len, int maxShift) {
    float bestCorr = -1e30f;
    int bestShift = 0;
    for (int shift = -maxShift; shift <= maxShift; ++shift) {
        float corr = 0;
        int count = 0;
        for (int i = 0; i < len; ++i) {
            int srcIdx = i + shift;
            if (srcIdx >= 0 && srcIdx < len) {
                corr += src[srcIdx] * target[i];
                count++;
            }
        }
        if (count > 0) corr /= count;
        if (corr > bestCorr) { bestCorr = corr; bestShift = shift; }
    }
    return bestShift;
}

std::shared_ptr<AudioTrack> WSOLAProcessor::process(const AudioTrack& input, double tempoRatio) {
    auto monoTrack = input.toMono();
    const float* data = monoTrack.getRawData();
    size_t numSamples = monoTrack.getTotalSamples();
    int sr = monoTrack.getSampleRate();

    size_t outputLen = static_cast<size_t>(numSamples * tempoRatio);
    std::vector<float> output(outputLen, 0.0f);

    int winSize = windowSize_;
    int halfWin = winSize / 2;
    int hopAnalysis = winSize / 2;
    int hopSynthesis = static_cast<int>(hopAnalysis * tempoRatio);
    int maxShift = winSize / 4;

    std::vector<float> window(winSize);
    for (int i = 0; i < winSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159f * i / (winSize - 1)));
    }

    size_t inPos = 0;
    size_t outPos = 0;

    while (inPos + winSize < numSamples && outPos + winSize < outputLen) {
        int shift = 0;
        if (outPos > 0) {
            int searchLen = std::min(halfWin, static_cast<int>(numSamples - inPos));
            shift = findBestOverlap(data + inPos, output.data() + outPos, searchLen, maxShift);
        }

        int srcPos = static_cast<int>(inPos) + shift;
        srcPos = std::clamp(srcPos, 0, static_cast<int>(numSamples) - winSize);

        for (int i = 0; i < winSize && outPos + i < outputLen; ++i) {
            output[outPos + i] += data[srcPos + i] * window[i];
        }

        inPos += hopAnalysis;
        outPos += hopSynthesis;
    }

    auto result = std::make_shared<AudioTrack>();
    result->loadData(std::move(output), sr, 1);
    spdlog::info("WSOLA: {:.2f}x stretch, {} -> {} samples", tempoRatio, numSamples, outputLen);
    return result;
}

} // namespace BeatMate::Core
