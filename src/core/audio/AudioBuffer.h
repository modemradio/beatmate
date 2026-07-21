#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace BeatMate::Core {

// Lock-free ring buffer for audio thread communication
class AudioBuffer {
public:
    // capacity in frames, channels = number of interleaved channels
    explicit AudioBuffer(size_t capacityFrames = 8192, int channels = 2);
    ~AudioBuffer();

    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;

    // Write samples to ring buffer. Returns number of frames actually written.
    size_t write(const float* data, size_t frames);

    // Read samples from ring buffer. Returns number of frames actually read.
    size_t read(float* data, size_t frames);

    size_t peek(float* data, size_t frames) const;

    size_t availableRead() const;

    size_t availableWrite() const;

    size_t capacity() const { return capacityFrames_; }

    int getChannels() const { return channels_; }

    void clear();

    // Resize (not thread-safe, call when audio is stopped)
    void resize(size_t newCapacityFrames, int newChannels = -1);

private:
    float* allocateAligned(size_t numSamples);
    void freeAligned(float* ptr);

    float* buffer_ = nullptr;
    size_t capacityFrames_ = 0;
    size_t capacitySamples_ = 0;
    int channels_ = 2;

    std::atomic<size_t> writePos_{0};
    std::atomic<size_t> readPos_{0};

    static constexpr size_t kAlignment = 32; // AVX alignment
};

class LinearAudioBuffer {
public:
    LinearAudioBuffer() = default;
    explicit LinearAudioBuffer(size_t frames, int channels = 2);

    void allocate(size_t frames, int channels = 2);

    float* data() { return buffer_.get(); }
    const float* data() const { return buffer_.get(); }

    float& sample(size_t frame, int channel);
    float sample(size_t frame, int channel) const;

    size_t frames() const { return frames_; }
    int channels() const { return channels_; }
    size_t totalSamples() const { return frames_ * channels_; }

    void clear();
    void copyFrom(const float* src, size_t frames);
    void copyTo(float* dst, size_t frames) const;
    void mixFrom(const float* src, size_t frames, float gain = 1.0f);

private:
    std::unique_ptr<float[], void(*)(float*)> buffer_{nullptr, [](float*){}};
    size_t frames_ = 0;
    int channels_ = 0;
};

} // namespace BeatMate::Core
