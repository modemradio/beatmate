#include "SamplerEngine.h"
#include "../audio/AudioFileReader.h"
#include <cstring>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

SamplerEngine::SamplerEngine() { mixBuffer_.resize(8192, 0.0f); }
SamplerEngine::~SamplerEngine() = default;

bool SamplerEngine::loadSample(int padIndex, const std::string& path) {
    if (padIndex < 0 || padIndex >= kNumPads) return false;
    AudioFileReader reader;
    auto track = reader.readFile(path);
    if (!track) return false;
    pads_[padIndex].loadSample(track);
    pads_[padIndex].setName(std::filesystem::path(path).stem().string());
    spdlog::info("SamplerEngine: loaded pad {} from {}", padIndex, path);
    return true;
}

void SamplerEngine::triggerPad(int index) {
    if (index >= 0 && index < kNumPads) pads_[index].trigger();
}

void SamplerEngine::stopPad(int index) {
    if (index >= 0 && index < kNumPads) pads_[index].stop();
}

void SamplerEngine::stopAll() {
    for (auto& pad : pads_) pad.stop();
}

void SamplerEngine::processBlock(float* output, int numFrames, int channels) {
    std::memset(output, 0, numFrames * channels * sizeof(float));
    size_t needed = numFrames * channels;
    if (mixBuffer_.size() < needed) mixBuffer_.resize(needed);

    for (auto& pad : pads_) {
        if (!pad.isPlaying()) continue;
        pad.processBlock(mixBuffer_.data(), numFrames, channels);
        for (size_t i = 0; i < needed; ++i) {
            output[i] += mixBuffer_[i];
        }
    }
}

SamplePad* SamplerEngine::getPad(int index) {
    if (index < 0 || index >= kNumPads) return nullptr;
    return &pads_[index];
}

void SamplerEngine::setPadMode(int index, PadMode mode) {
    if (index >= 0 && index < kNumPads) pads_[index].setMode(mode);
}

void SamplerEngine::setPadVolume(int index, float vol) {
    if (index >= 0 && index < kNumPads) pads_[index].setVolume(vol);
}

void SamplerEngine::setPadPitch(int index, float semitones) {
    if (index >= 0 && index < kNumPads) pads_[index].setPitch(semitones);
}

}
