#include "AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioTrack::AudioTrack() = default;
AudioTrack::~AudioTrack() = default;

AudioTrack::AudioTrack(AudioTrack&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.dataMutex_);
    data_ = std::move(other.data_);
    sampleRate_ = other.sampleRate_;
    channels_ = other.channels_;
    filePath_ = std::move(other.filePath_);
    title_ = std::move(other.title_);
    artist_ = std::move(other.artist_);
    waveform_ = std::move(other.waveform_);
}

AudioTrack& AudioTrack::operator=(AudioTrack&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(dataMutex_, other.dataMutex_);
        data_ = std::move(other.data_);
        sampleRate_ = other.sampleRate_;
        channels_ = other.channels_;
        filePath_ = std::move(other.filePath_);
        title_ = std::move(other.title_);
        artist_ = std::move(other.artist_);
        waveform_ = std::move(other.waveform_);
    }
    return *this;
}

void AudioTrack::loadData(std::vector<float>&& pcmData, int sampleRate, int channels) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (channels <= 0 || sampleRate <= 0) {
        spdlog::warn("AudioTrack::loadData rejected: channels={} sampleRate={} (likely corrupted audio header)",
                     channels, sampleRate);
        data_.clear();
        sampleRate_ = 0;
        channels_ = 0;
        return;
    }
    data_ = std::move(pcmData);
    sampleRate_ = sampleRate;
    channels_ = channels;
    spdlog::info("AudioTrack loaded: {} frames, {}Hz, {} ch",
                 data_.size() / static_cast<size_t>(channels_), sampleRate_, channels_);
}

void AudioTrack::loadData(const float* data, size_t numSamples, int sampleRate, int channels) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (channels <= 0 || sampleRate <= 0 || data == nullptr) {
        spdlog::warn("AudioTrack::loadData(ptr) rejected: channels={} sampleRate={} data={}",
                     channels, sampleRate, static_cast<const void*>(data));
        data_.clear();
        sampleRate_ = 0;
        channels_ = 0;
        return;
    }
    data_.assign(data, data + numSamples);
    sampleRate_ = sampleRate;
    channels_ = channels;
    spdlog::info("AudioTrack loaded: {} frames, {}Hz, {} ch",
                 numSamples / static_cast<size_t>(channels_), sampleRate_, channels_);
}

float AudioTrack::getSample(size_t index) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (index >= data_.size()) return 0.0f;
    return data_[index];
}

float AudioTrack::getSample(size_t frame, int channel) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (channels_ <= 0 || channel < 0 || channel >= channels_) return 0.0f;
    size_t idx = frame * static_cast<size_t>(channels_) + static_cast<size_t>(channel);
    if (idx >= data_.size()) return 0.0f;
    return data_[idx];
}

const float* AudioTrack::getRawData() const {
    return data_.data();
}

size_t AudioTrack::getTotalSamples() const {
    return data_.size();
}

void AudioTrack::getSamples(float* dest, size_t startFrame, size_t numFrames) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    size_t startIdx = startFrame * channels_;
    size_t count = numFrames * channels_;
    size_t available = (startIdx < data_.size()) ? (data_.size() - startIdx) : 0;
    size_t toCopy = std::min(count, available);

    if (toCopy > 0) {
        std::memcpy(dest, data_.data() + startIdx, toCopy * sizeof(float));
    }
    if (toCopy < count) {
        std::memset(dest + toCopy, 0, (count - toCopy) * sizeof(float));
    }
}

size_t AudioTrack::getNumFrames() const {
    if (channels_ == 0) return 0;
    return data_.size() / channels_;
}

double AudioTrack::getDuration() const {
    if (sampleRate_ == 0) return 0.0;
    return static_cast<double>(getNumFrames()) / sampleRate_;
}

void AudioTrack::normalize(float targetPeak) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (data_.empty()) return;

    float maxAbs = 0.0f;
    for (float s : data_) {
        float abs_s = std::fabs(s);
        if (abs_s > maxAbs) maxAbs = abs_s;
    }

    if (maxAbs < 1e-6f) return;

    float gain = targetPeak / maxAbs;
    for (float& s : data_) {
        s *= gain;
    }
    spdlog::info("AudioTrack normalized: gain = {:.2f}dB", 20.0f * std::log10(gain));
}

AudioTrack AudioTrack::toMono() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    AudioTrack mono;
    if (channels_ == 1) {
        mono.loadData(data_.data(), data_.size(), sampleRate_, 1);
        return mono;
    }

    size_t frames = getNumFrames();
    std::vector<float> monoData(frames);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < channels_; ++ch) {
            sum += data_[i * channels_ + ch];
        }
        monoData[i] = sum / channels_;
    }
    mono.loadData(std::move(monoData), sampleRate_, 1);
    return mono;
}

AudioTrack AudioTrack::resample(int targetSampleRate) const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (targetSampleRate == sampleRate_) {
        AudioTrack copy;
        copy.loadData(data_.data(), data_.size(), sampleRate_, channels_);
        return copy;
    }

    double ratio = static_cast<double>(targetSampleRate) / sampleRate_;
    size_t srcFrames = data_.size() / channels_;
    size_t dstFrames = static_cast<size_t>(srcFrames * ratio);
    std::vector<float> resampled(dstFrames * channels_);

    for (size_t i = 0; i < dstFrames; ++i) {
        double srcPos = i / ratio;
        size_t idx0 = static_cast<size_t>(srcPos);
        size_t idx1 = std::min(idx0 + 1, srcFrames - 1);
        double frac = srcPos - idx0;

        for (int ch = 0; ch < channels_; ++ch) {
            float s0 = data_[idx0 * channels_ + ch];
            float s1 = data_[idx1 * channels_ + ch];
            resampled[i * channels_ + ch] = static_cast<float>(s0 + (s1 - s0) * frac);
        }
    }

    AudioTrack result;
    result.loadData(std::move(resampled), targetSampleRate, channels_);
    return result;
}

}
