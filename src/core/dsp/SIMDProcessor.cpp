#include "SIMDProcessor.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define BEATMATE_HAS_X86
#include <immintrin.h>

#ifdef _MSC_VER
#include <intrin.h>
static bool cpuHasAVX2() {
    int info[4];
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0; // EBX bit 5 = AVX2
}
static bool cpuHasSSE() {
    int info[4];
    __cpuid(info, 1);
    return (info[3] & (1 << 25)) != 0; // EDX bit 25 = SSE
}
#else
#include <cpuid.h>
static bool cpuHasAVX2() {
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1 << 5)) != 0;
    }
    return false;
}
static bool cpuHasSSE() {
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return (edx & (1 << 25)) != 0;
    }
    return false;
}
#endif

#endif // x86

namespace BeatMate::Core {

bool SIMDProcessor::isAVX2Available() {
#ifdef BEATMATE_HAS_X86
    static bool avx2 = cpuHasAVX2();
    return avx2;
#else
    return false;
#endif
}

bool SIMDProcessor::isSSEAvailable() {
#ifdef BEATMATE_HAS_X86
    static bool sse = cpuHasSSE();
    return sse;
#else
    return false;
#endif
}

void SIMDProcessor::multiply(float* buffer, float gain, int count) {
#ifdef BEATMATE_HAS_X86
    if (isAVX2Available()) {
        __m256 vGain = _mm256_set1_ps(gain);
        int i = 0;
        for (; i + 7 < count; i += 8) {
            __m256 v = _mm256_loadu_ps(buffer + i);
            v = _mm256_mul_ps(v, vGain);
            _mm256_storeu_ps(buffer + i, v);
        }
        for (; i < count; ++i) {
            buffer[i] *= gain;
        }
        return;
    }
    if (isSSEAvailable()) {
        __m128 vGain = _mm_set1_ps(gain);
        int i = 0;
        for (; i + 3 < count; i += 4) {
            __m128 v = _mm_loadu_ps(buffer + i);
            v = _mm_mul_ps(v, vGain);
            _mm_storeu_ps(buffer + i, v);
        }
        for (; i < count; ++i) {
            buffer[i] *= gain;
        }
        return;
    }
#endif
    // Scalar fallback
    for (int i = 0; i < count; ++i) {
        buffer[i] *= gain;
    }
}

void SIMDProcessor::add(float* dst, const float* src, int count) {
#ifdef BEATMATE_HAS_X86
    if (isAVX2Available()) {
        int i = 0;
        for (; i + 7 < count; i += 8) {
            __m256 a = _mm256_loadu_ps(dst + i);
            __m256 b = _mm256_loadu_ps(src + i);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
        }
        for (; i < count; ++i) {
            dst[i] += src[i];
        }
        return;
    }
#endif
    for (int i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

void SIMDProcessor::mix(float* dst, const float* src, float gain, int count) {
#ifdef BEATMATE_HAS_X86
    if (isAVX2Available()) {
        __m256 vGain = _mm256_set1_ps(gain);
        int i = 0;
        for (; i + 7 < count; i += 8) {
            __m256 a = _mm256_loadu_ps(dst + i);
            __m256 b = _mm256_loadu_ps(src + i);
            b = _mm256_mul_ps(b, vGain);
            _mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
        }
        for (; i < count; ++i) {
            dst[i] += src[i] * gain;
        }
        return;
    }
#endif
    for (int i = 0; i < count; ++i) {
        dst[i] += src[i] * gain;
    }
}

void SIMDProcessor::copy(float* dst, const float* src, int count) {
    std::memcpy(dst, src, count * sizeof(float));
}

void SIMDProcessor::clear(float* buffer, int count) {
    std::memset(buffer, 0, count * sizeof(float));
}

void SIMDProcessor::interleave(float* dst, const float* left, const float* right, int frames) {
    for (int i = 0; i < frames; ++i) {
        dst[i * 2]     = left[i];
        dst[i * 2 + 1] = right[i];
    }
}

void SIMDProcessor::deinterleave(const float* src, float* left, float* right, int frames) {
    for (int i = 0; i < frames; ++i) {
        left[i]  = src[i * 2];
        right[i] = src[i * 2 + 1];
    }
}

float SIMDProcessor::findAbsMax(const float* buffer, int count) {
    float maxVal = 0.0f;
#ifdef BEATMATE_HAS_X86
    if (isAVX2Available()) {
        __m256 vMax = _mm256_setzero_ps();
        __m256 vSign = _mm256_set1_ps(-0.0f);
        int i = 0;
        for (; i + 7 < count; i += 8) {
            __m256 v = _mm256_loadu_ps(buffer + i);
            v = _mm256_andnot_ps(vSign, v); // abs
            vMax = _mm256_max_ps(vMax, v);
        }
        // Horizontal max
        float tmp[8];
        _mm256_storeu_ps(tmp, vMax);
        for (int j = 0; j < 8; ++j) {
            if (tmp[j] > maxVal) maxVal = tmp[j];
        }
        for (; i < count; ++i) {
            float abs_s = std::fabs(buffer[i]);
            if (abs_s > maxVal) maxVal = abs_s;
        }
        return maxVal;
    }
#endif
    for (int i = 0; i < count; ++i) {
        float abs_s = std::fabs(buffer[i]);
        if (abs_s > maxVal) maxVal = abs_s;
    }
    return maxVal;
}

} // namespace BeatMate::Core
