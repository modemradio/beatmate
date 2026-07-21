#include "AudioBuffer.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace BeatMate::Core {

AudioBuffer::AudioBuffer(size_t capacityFrames, int channels)
    : capacityFrames_(capacityFrames), channels_(channels) {
    capacitySamples_ = capacityFrames_ * channels_;
    buffer_ = allocateAligned(capacitySamples_);
    std::memset(buffer_, 0, capacitySamples_ * sizeof(float));
}

AudioBuffer::~AudioBuffer() {
    freeAligned(buffer_);
}

float* AudioBuffer::allocateAligned(size_t numSamples) {
    if (numSamples == 0) return nullptr;
#ifdef _WIN32
    return static_cast<float*>(_aligned_malloc(numSamples * sizeof(float), kAlignment));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, kAlignment, numSamples * sizeof(float)) != 0) {
        return nullptr;
    }
    return static_cast<float*>(ptr);
#endif
}

void AudioBuffer::freeAligned(float* ptr) {
    if (!ptr) return;
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

size_t AudioBuffer::write(const float* data, size_t frames) {
    size_t avail = availableWrite();
    size_t toWrite = std::min(frames, avail);
    if (toWrite == 0) return 0;

    size_t wp = writePos_.load(std::memory_order_relaxed);
    size_t samplesToWrite = toWrite * channels_;

    for (size_t i = 0; i < samplesToWrite; ++i) {
        buffer_[(wp + i) % capacitySamples_] = data[i];
    }

    writePos_.store((wp + samplesToWrite) % capacitySamples_, std::memory_order_release);
    return toWrite;
}

size_t AudioBuffer::read(float* data, size_t frames) {
    size_t avail = availableRead();
    size_t toRead = std::min(frames, avail);
    if (toRead == 0) return 0;

    size_t rp = readPos_.load(std::memory_order_relaxed);
    size_t samplesToRead = toRead * channels_;

    for (size_t i = 0; i < samplesToRead; ++i) {
        data[i] = buffer_[(rp + i) % capacitySamples_];
    }

    readPos_.store((rp + samplesToRead) % capacitySamples_, std::memory_order_release);
    return toRead;
}

size_t AudioBuffer::peek(float* data, size_t frames) const {
    size_t avail = availableRead();
    size_t toRead = std::min(frames, avail);
    if (toRead == 0) return 0;

    size_t rp = readPos_.load(std::memory_order_acquire);
    size_t samplesToRead = toRead * channels_;

    for (size_t i = 0; i < samplesToRead; ++i) {
        data[i] = buffer_[(rp + i) % capacitySamples_];
    }
    return toRead;
}

size_t AudioBuffer::availableRead() const {
    size_t wp = writePos_.load(std::memory_order_acquire);
    size_t rp = readPos_.load(std::memory_order_acquire);
    size_t availSamples = (wp >= rp) ? (wp - rp) : (capacitySamples_ - rp + wp);
    return availSamples / channels_;
}

size_t AudioBuffer::availableWrite() const {
    return capacityFrames_ - availableRead() - 1;
}

void AudioBuffer::clear() {
    writePos_.store(0, std::memory_order_release);
    readPos_.store(0, std::memory_order_release);
    std::memset(buffer_, 0, capacitySamples_ * sizeof(float));
}

void AudioBuffer::resize(size_t newCapacityFrames, int newChannels) {
    freeAligned(buffer_);
    capacityFrames_ = newCapacityFrames;
    if (newChannels > 0) channels_ = newChannels;
    capacitySamples_ = capacityFrames_ * channels_;
    buffer_ = allocateAligned(capacitySamples_);
    std::memset(buffer_, 0, capacitySamples_ * sizeof(float));
    writePos_.store(0);
    readPos_.store(0);
}


LinearAudioBuffer::LinearAudioBuffer(size_t frames, int channels) {
    allocate(frames, channels);
}

void LinearAudioBuffer::allocate(size_t frames, int channels) {
    frames_ = frames;
    channels_ = channels;
    size_t total = frames * channels;
#ifdef _WIN32
    float* raw = static_cast<float*>(_aligned_malloc(total * sizeof(float), 32));
    buffer_ = std::unique_ptr<float[], void(*)(float*)>(raw, [](float* p) { _aligned_free(p); });
#else
    void* ptr = nullptr;
    posix_memalign(&ptr, 32, total * sizeof(float));
    float* raw = static_cast<float*>(ptr);
    buffer_ = std::unique_ptr<float[], void(*)(float*)>(raw, [](float* p) { free(p); });
#endif
    std::memset(raw, 0, total * sizeof(float));
}

float& LinearAudioBuffer::sample(size_t frame, int channel) {
    return buffer_[frame * channels_ + channel];
}

float LinearAudioBuffer::sample(size_t frame, int channel) const {
    return buffer_[frame * channels_ + channel];
}

void LinearAudioBuffer::clear() {
    if (buffer_) {
        std::memset(buffer_.get(), 0, frames_ * channels_ * sizeof(float));
    }
}

void LinearAudioBuffer::copyFrom(const float* src, size_t frames) {
    size_t n = std::min(frames, frames_) * channels_;
    std::memcpy(buffer_.get(), src, n * sizeof(float));
}

void LinearAudioBuffer::copyTo(float* dst, size_t frames) const {
    size_t n = std::min(frames, frames_) * channels_;
    std::memcpy(dst, buffer_.get(), n * sizeof(float));
}

void LinearAudioBuffer::mixFrom(const float* src, size_t frames, float gain) {
    size_t n = std::min(frames, frames_) * channels_;
    for (size_t i = 0; i < n; ++i) {
        buffer_[i] += src[i] * gain;
    }
}

}
