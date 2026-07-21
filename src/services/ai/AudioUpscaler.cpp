#include "AudioUpscaler.h"
#include "ONNXInference.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <vector>

namespace BeatMate::Services::AI {

AudioUpscaler::AudioUpscaler(std::shared_ptr<ONNXInference> inference)
    : inference_(std::move(inference)) {}

Core::AudioTrack AudioUpscaler::upscale(const Core::AudioTrack& track, int targetSampleRate) {
    if (track.getSampleRate() >= targetSampleRate) {
        spdlog::info("AudioUpscaler: Track already at {}Hz, no upscaling needed", track.getSampleRate());
        return track.resample(track.getSampleRate());
    }

    spdlog::info("AudioUpscaler: Upscaling from {}Hz to {}Hz", track.getSampleRate(), targetSampleRate);

    Core::AudioTrack upsampled = track.resample(targetSampleRate);
    if (!inference_ || !inference_->isLoaded())
        return upsampled;

    const int channels = upsampled.getChannels();
    const size_t totalSamples = upsampled.getTotalSamples();
    if (channels <= 0 || totalSamples == 0)
        return upsampled;

    const float* raw = upsampled.getRawData();
    if (raw == nullptr)
        return upsampled;

    std::vector<float> interleaved(raw, raw + totalSamples);
    const size_t totalFrames = totalSamples / static_cast<size_t>(channels);
    const size_t window = 8192;

    bool anyApplied = false;
    std::vector<float> chanBuf;
    chanBuf.reserve(window);
    for (int ch = 0; ch < channels; ++ch) {
        for (size_t start = 0; start < totalFrames; start += window) {
            const size_t n = std::min(window, totalFrames - start);
            chanBuf.assign(n, 0.0f);
            for (size_t i = 0; i < n; ++i)
                chanBuf[i] = interleaved[(start + i) * static_cast<size_t>(channels) + static_cast<size_t>(ch)];

            auto out = inference_->run(chanBuf);
            if (out.size() == n) {
                for (size_t i = 0; i < n; ++i)
                    interleaved[(start + i) * static_cast<size_t>(channels) + static_cast<size_t>(ch)] =
                        std::clamp(out[i], -1.0f, 1.0f);
                anyApplied = true;
            }
        }
    }

    if (!anyApplied) {
        spdlog::warn("AudioUpscaler: model output shape mismatch, returning resampled audio");
        return upsampled;
    }

    Core::AudioTrack result;
    result.loadData(std::move(interleaved), targetSampleRate, channels);
    result.setFilePath(track.getFilePath());
    result.setTitle(track.getTitle());
    result.setArtist(track.getArtist());
    return result;
}

bool AudioUpscaler::loadModel(const std::string& path) {
    return inference_->loadModel(path);
}

} // namespace BeatMate::Services::AI
