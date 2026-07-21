#include "AudioMixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioMixer::AudioMixer(int maxChannels, int bufferSize, int sampleRate)
    : bufferSize_(bufferSize), sampleRate_(sampleRate) {
    tempBuffer_.resize(bufferSize * 2);
    mixBuffer_.resize(bufferSize * 2);
    channels_.reserve(maxChannels);
    spdlog::info("AudioMixer created: maxCh={}, bufSize={}", maxChannels, bufferSize);
}

AudioMixer::~AudioMixer() = default;

int AudioMixer::addChannel(const std::string& name) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    auto ch = std::make_unique<MixerChannel>();
    ch->id = nextChannelId_++;
    ch->name = name.empty() ? ("Channel " + std::to_string(ch->id)) : name;
    int id = ch->id;
    channels_.push_back(std::move(ch));
    spdlog::info("Mixer: added channel {} ({})", id, channels_.back()->name);
    return id;
}

bool AudioMixer::removeChannel(int channelId) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    auto it = std::find_if(channels_.begin(), channels_.end(),
        [channelId](const auto& ch) { return ch->id == channelId; });
    if (it == channels_.end()) return false;
    channels_.erase(it);
    spdlog::info("Mixer: removed channel {}", channelId);
    return true;
}

MixerChannel* AudioMixer::findChannel(int id) {
    for (auto& ch : channels_) {
        if (ch->id == id) return ch.get();
    }
    return nullptr;
}

const MixerChannel* AudioMixer::findChannel(int id) const {
    for (auto& ch : channels_) {
        if (ch->id == id) return ch.get();
    }
    return nullptr;
}

void AudioMixer::setChannelVolume(int id, float volume) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) ch->volume.store(std::clamp(volume, 0.0f, 2.0f));
}

float AudioMixer::getChannelVolume(int id) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) return ch->volume.load();
    return 0.0f;
}

void AudioMixer::setChannelPan(int id, float pan) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) ch->pan.store(std::clamp(pan, -1.0f, 1.0f));
}

float AudioMixer::getChannelPan(int id) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) return ch->pan.load();
    return 0.0f;
}

void AudioMixer::muteChannel(int id, bool mute) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) ch->muted.store(mute);
}

bool AudioMixer::isChannelMuted(int id) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) return ch->muted.load();
    return false;
}

void AudioMixer::soloChannel(int id, bool solo) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) ch->solo.store(solo);
}

bool AudioMixer::isChannelSoloed(int id) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) return ch->solo.load();
    return false;
}

void AudioMixer::setChannelSource(int id, std::function<void(float*, int, int)> callback) {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) ch->sourceCallback = std::move(callback);
}

bool AudioMixer::hasSoloedChannels() const {
    for (auto& ch : channels_) {
        if (ch->solo.load()) return true;
    }
    return false;
}

void AudioMixer::updateVU(MixerChannel& ch, const float* buffer, int frames, int channels) {
    float sumL = 0.0f, sumR = 0.0f;
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < frames; ++i) {
        float l = std::fabs(buffer[i * channels]);
        float r = (channels > 1) ? std::fabs(buffer[i * channels + 1]) : l;
        sumL += l * l;
        sumR += r * r;
        peakL = std::max(peakL, l);
        peakR = std::max(peakR, r);
    }

    float rmsL = std::sqrt(sumL / frames);
    float rmsR = std::sqrt(sumR / frames);

    ch.vuLeft.store(ch.vuLeft.load() * kVuDecay + rmsL * (1.0f - kVuDecay));
    ch.vuRight.store(ch.vuRight.load() * kVuDecay + rmsR * (1.0f - kVuDecay));

    float prevPeakL = ch.peakLeft.load();
    float prevPeakR = ch.peakRight.load();
    ch.peakLeft.store(std::max(peakL, prevPeakL * kPeakHoldDecay));
    ch.peakRight.store(std::max(peakR, prevPeakR * kPeakHoldDecay));
}

void AudioMixer::getMasterOutput(float* output, int numFrames, int channels) {
    std::lock_guard<std::mutex> lock(channelsMutex_);

    size_t totalSamples = numFrames * channels;
    if (mixBuffer_.size() < totalSamples) mixBuffer_.resize(totalSamples);
    if (tempBuffer_.size() < totalSamples) tempBuffer_.resize(totalSamples);

    std::memset(mixBuffer_.data(), 0, totalSamples * sizeof(float));

    bool anySoloed = hasSoloedChannels();

    for (auto& ch : channels_) {
        bool shouldPlay = true;
        if (ch->muted.load()) shouldPlay = false;
        if (anySoloed && !ch->solo.load()) shouldPlay = false;

        if (!shouldPlay || !ch->sourceCallback) {
            ch->vuLeft.store(ch->vuLeft.load() * kVuDecay);
            ch->vuRight.store(ch->vuRight.load() * kVuDecay);
            continue;
        }

        std::memset(tempBuffer_.data(), 0, totalSamples * sizeof(float));
        ch->sourceCallback(tempBuffer_.data(), numFrames, channels);

        float vol = ch->volume.load();
        float pan = ch->pan.load();

        // Panoramique a puissance constante (cos/sin sur 0..pi/2).
        float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
        float gainL = std::cos(angle) * vol;
        float gainR = std::sin(angle) * vol;

        for (int i = 0; i < numFrames; ++i) {
            if (channels >= 2) {
                mixBuffer_[i * channels] += tempBuffer_[i * channels] * gainL;
                mixBuffer_[i * channels + 1] += tempBuffer_[i * channels + 1] * gainR;
            } else {
                mixBuffer_[i] += tempBuffer_[i] * vol;
            }
        }

        updateVU(*ch, tempBuffer_.data(), numFrames, channels);
    }

    float masterVol = masterVolume_.load();
    float sumL = 0.0f, sumR = 0.0f;
    for (size_t i = 0; i < totalSamples; ++i) {
        mixBuffer_[i] *= masterVol;
        output[i] = mixBuffer_[i];
    }

    for (int i = 0; i < numFrames; ++i) {
        float l = std::fabs(output[i * channels]);
        float r = (channels > 1) ? std::fabs(output[i * channels + 1]) : l;
        sumL += l * l;
        sumR += r * r;
    }
    float rmsL = std::sqrt(sumL / numFrames);
    float rmsR = std::sqrt(sumR / numFrames);
    masterVuLeft_.store(masterVuLeft_.load() * kVuDecay + rmsL * (1.0f - kVuDecay));
    masterVuRight_.store(masterVuRight_.load() * kVuDecay + rmsR * (1.0f - kVuDecay));
}

float AudioMixer::getChannelVU(int id, int channel) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) {
        return (channel == 0) ? ch->vuLeft.load() : ch->vuRight.load();
    }
    return 0.0f;
}

float AudioMixer::getChannelPeak(int id, int channel) const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    if (auto* ch = findChannel(id)) {
        return (channel == 0) ? ch->peakLeft.load() : ch->peakRight.load();
    }
    return 0.0f;
}

float AudioMixer::getMasterVU(int channel) const {
    return (channel == 0) ? masterVuLeft_.load() : masterVuRight_.load();
}

void AudioMixer::resetPeaks() {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    for (auto& ch : channels_) {
        ch->peakLeft.store(0.0f);
        ch->peakRight.store(0.0f);
    }
}

int AudioMixer::getChannelCount() const {
    std::lock_guard<std::mutex> lock(channelsMutex_);
    return static_cast<int>(channels_.size());
}

} // namespace BeatMate::Core
