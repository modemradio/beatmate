#include "TransitionEngine.h"
#include "CrossfadeEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace BeatMate::Core {

TransitionEngine::TransitionEngine() = default;
TransitionEngine::~TransitionEngine() = default;

std::string TransitionEngine::getTransitionName(TransitionType type) {
    static const std::map<TransitionType, std::string> names = {
        {TransitionType::Cut, "Cut"}, {TransitionType::XFade, "Crossfade"},
        {TransitionType::Echo, "Echo Out"}, {TransitionType::Backspin, "Backspin"},
        {TransitionType::Brake, "Brake"}, {TransitionType::Filter, "Filter Sweep"},
        {TransitionType::Hook, "Hook"}, {TransitionType::PingPong, "Ping Pong"},
        {TransitionType::Extended, "Extended Mix"}, {TransitionType::Scratch, "Scratch"},
        {TransitionType::PowerDown, "Power Down"}, {TransitionType::DropSwap, "Drop Swap"},
        {TransitionType::BeatMatch, "Beat Match"}, {TransitionType::VocalBridge, "Vocal Bridge"}
    };
    auto it = names.find(type);
    return (it != names.end()) ? it->second : "Unknown";
}

std::vector<TransitionType> TransitionEngine::getAllTransitions() {
    return { TransitionType::Cut, TransitionType::XFade, TransitionType::Echo,
             TransitionType::Backspin, TransitionType::Brake, TransitionType::Filter,
             TransitionType::Hook, TransitionType::PingPong, TransitionType::Extended,
             TransitionType::Scratch, TransitionType::PowerDown, TransitionType::DropSwap,
             TransitionType::BeatMatch, TransitionType::VocalBridge };
}

float TransitionEngine::sampleEnvelope(TransitionType type, float t) {
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;

    constexpr float kHalfPi = 1.5707963f;

    switch (type) {
        case TransitionType::Cut:
            return (t < 0.5f) ? 1.0f : 0.0f;

        case TransitionType::XFade:
        case TransitionType::Extended:
        case TransitionType::BeatMatch:
        case TransitionType::VocalBridge:
            return std::cos(t * kHalfPi);

        case TransitionType::Echo:
        case TransitionType::Hook:
        case TransitionType::PingPong: {
            float gainA = std::cos(t * kHalfPi);
            if (t > 0.3f) {
                float decay = (t - 0.3f) / 0.7f;
                gainA *= (1.0f - decay * 0.8f);
            }
            return gainA;
        }

        case TransitionType::Filter:
            return 1.0f - t;

        case TransitionType::Brake:
        case TransitionType::DropSwap:
            return 1.0f - t;

        case TransitionType::Backspin:
        case TransitionType::Scratch:
            return std::cos(t * kHalfPi);

        case TransitionType::PowerDown:
            return 1.0f - t;
    }
    return 1.0f - t;
}

void TransitionEngine::applyCut(const float* a, const float* b, float* out, int n, int ch, float p) {
    int total = n * ch;
    if (p < 0.5f) {
        std::memcpy(out, a, total * sizeof(float));
    } else {
        std::memcpy(out, b, total * sizeof(float));
    }
}

void TransitionEngine::applyXFade(const float* a, const float* b, float* out, int n, int ch, float p) {
    CrossfadeEngine xf;
    xf.crossfade(a, b, out, n, ch, p, CrossfadeType::EqualPower);
}

void TransitionEngine::applyEcho(const float* a, const float* b, float* out, int n, int ch, int sr, float p) {
    int total = n * ch;
    float gainA = std::cos(p * 1.5707963f);
    float gainB = std::sin(p * 1.5707963f);

    for (int i = 0; i < total; ++i) {
        float echoedA = a[i] * gainA;
        if (p > 0.3f) {
            float decay = (p - 0.3f) / 0.7f;
            echoedA *= (1.0f - decay * 0.8f);
        }
        out[i] = echoedA + b[i] * gainB;
    }
}

void TransitionEngine::applyBrake(const float* a, const float* b, float* out, int n, int ch, float p) {
    int total = n * ch;
    float speed = 1.0f - p * p;
    float gainB = std::sin(p * 1.5707963f);

    for (int i = 0; i < n; ++i) {
        float sampleA = a[static_cast<int>(i * speed) % n * ch];
        for (int c = 0; c < ch; ++c) {
            int srcIdx = std::clamp(static_cast<int>(i * speed), 0, n - 1);
            out[i * ch + c] = a[srcIdx * ch + c] * (1.0f - p) + b[i * ch + c] * gainB;
        }
    }
}

void TransitionEngine::applyFilter(const float* a, const float* b, float* out, int n, int ch, int sr, float p) {
    int total = n * ch;
    float cutoff = 20000.0f * (1.0f - p);
    float rc = 1.0f / (2.0f * 3.14159f * std::max(cutoff, 20.0f));
    float dt = 1.0f / sr;
    float alpha = dt / (rc + dt);

    float gainB = std::sin(p * 1.5707963f);
    float prev = 0;

    for (int i = 0; i < n; ++i) {
        float monoA = 0;
        for (int c = 0; c < ch; ++c) monoA += a[i * ch + c];
        monoA /= ch;

        prev = prev + alpha * (monoA - prev);

        for (int c = 0; c < ch; ++c) {
            float filteredA = prev * (1.0f - p);
            out[i * ch + c] = filteredA + b[i * ch + c] * gainB;
        }
    }
}

void TransitionEngine::applyPowerDown(const float* a, const float* b, float* out, int n, int ch, float p) {
    float speed = std::pow(1.0f - p, 3.0f);
    float gainB = (p > 0.5f) ? (p - 0.5f) * 2.0f : 0.0f;

    for (int i = 0; i < n; ++i) {
        int srcIdx = std::clamp(static_cast<int>(i * speed), 0, n - 1);
        for (int c = 0; c < ch; ++c) {
            out[i * ch + c] = a[srcIdx * ch + c] * (1.0f - p) + b[i * ch + c] * gainB;
        }
    }
}

void TransitionEngine::apply(const float* trackA, const float* trackB, float* output,
                              int numSamples, int channels, int sampleRate,
                              TransitionType type, const TransitionParams& /*params*/,
                              float progress) {
    switch (type) {
        case TransitionType::Cut:
            applyCut(trackA, trackB, output, numSamples, channels, progress); break;
        case TransitionType::XFade:
        case TransitionType::Extended:
        case TransitionType::BeatMatch:
        case TransitionType::VocalBridge:
            applyXFade(trackA, trackB, output, numSamples, channels, progress); break;
        case TransitionType::Echo:
        case TransitionType::Hook:
        case TransitionType::PingPong:
            applyEcho(trackA, trackB, output, numSamples, channels, sampleRate, progress); break;
        case TransitionType::Backspin:
        case TransitionType::Scratch:
            applyBrake(trackA, trackB, output, numSamples, channels, progress); break;
        case TransitionType::Brake:
        case TransitionType::DropSwap:
            applyBrake(trackA, trackB, output, numSamples, channels, progress); break;
        case TransitionType::Filter:
            applyFilter(trackA, trackB, output, numSamples, channels, sampleRate, progress); break;
        case TransitionType::PowerDown:
            applyPowerDown(trackA, trackB, output, numSamples, channels, progress); break;
    }
}

} // namespace BeatMate::Core
