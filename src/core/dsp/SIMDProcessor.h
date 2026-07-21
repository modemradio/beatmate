#pragma once

#include <cstddef>

namespace BeatMate::Core {

class SIMDProcessor {
public:
    // Multiply buffer by scalar: buffer[i] *= gain
    static void multiply(float* buffer, float gain, int count);

    // Add two buffers: dst[i] += src[i]
    static void add(float* dst, const float* src, int count);

    // Mix with gain: dst[i] += src[i] * gain
    static void mix(float* dst, const float* src, float gain, int count);

    // Copy: dst[i] = src[i]
    static void copy(float* dst, const float* src, int count);

    // Clear: buffer[i] = 0
    static void clear(float* buffer, int count);

    // Interleave: mono channels to interleaved stereo
    static void interleave(float* dst, const float* left, const float* right, int frames);

    // Deinterleave: interleaved stereo to mono channels
    static void deinterleave(const float* src, float* left, float* right, int frames);

    static float findAbsMax(const float* buffer, int count);

    static bool isAVX2Available();

    static bool isSSEAvailable();
};

} // namespace BeatMate::Core
