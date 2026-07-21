#include "ScratchEngine.h"
#include "../audio/AudioTrack.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

ScratchEngine::ScratchEngine() = default;
ScratchEngine::~ScratchEngine() = default;

std::string ScratchEngine::getScratchName(ScratchType type) {
    switch (type) {
        case ScratchType::Baby: return "Baby";
        case ScratchType::Scribble: return "Scribble";
        case ScratchType::Tear: return "Tear";
        case ScratchType::Chirp: return "Chirp";
        case ScratchType::Flare: return "Flare";
        case ScratchType::Orbit: return "Orbit";
        case ScratchType::Transform: return "Transform";
        case ScratchType::Crab: return "Crab";
        case ScratchType::Twiddle: return "Twiddle";
        case ScratchType::Hydroplane: return "Hydroplane";
    }
    return "Unknown";
}

std::vector<ScratchType> ScratchEngine::getAllTypes() {
    return { ScratchType::Baby, ScratchType::Scribble, ScratchType::Tear, ScratchType::Chirp,
             ScratchType::Flare, ScratchType::Orbit, ScratchType::Transform, ScratchType::Crab,
             ScratchType::Twiddle, ScratchType::Hydroplane };
}

void ScratchEngine::apply(float* output, const AudioTrack& track, double position,
                           ScratchType type, int numSamples, int channels, int sampleRate) {
    double outputSr = sampleRate;
    size_t numFrames = track.getNumFrames();
    if (numFrames == 0) return;
    const double trackSr = track.getSampleRate();
    const double srRatio = (outputSr > 0.0 && trackSr > 0.0) ? (trackSr / outputSr) : 1.0;
    double posFrame = position * trackSr;

    for (int i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / outputSr;
        double speed = 0.0;

        switch (type) {
            case ScratchType::Baby:
                speed = std::sin(2.0 * kPi * 2.0 * t) * 2.0;
                break;
            case ScratchType::Scribble:
                speed = std::sin(2.0 * kPi * 8.0 * t) * 1.5;
                break;
            case ScratchType::Tear:
                speed = (std::fmod(t * 4.0, 1.0) < 0.7) ? 2.0 : -3.0;
                break;
            case ScratchType::Chirp: {
                double cycle = std::fmod(t * 3.0, 1.0);
                speed = (cycle < 0.5) ? 3.0 : 0.0;
                break;
            }
            case ScratchType::Flare: {
                double cycle = std::fmod(t * 2.0, 1.0);
                speed = std::sin(2.0 * kPi * cycle) * 2.0;
                if (std::fabs(cycle - 0.25) < 0.02 || std::fabs(cycle - 0.75) < 0.02) speed = 0;
                break;
            }
            case ScratchType::Orbit:
                speed = std::sin(2.0 * kPi * 1.5 * t) * 2.5;
                break;
            case ScratchType::Transform: {
                double cycle = std::fmod(t * 6.0, 1.0);
                speed = (cycle < 0.5) ? 2.0 : 0.0;
                break;
            }
            case ScratchType::Crab: {
                double cycle = std::fmod(t * 12.0, 1.0);
                speed = (cycle < 0.3) ? 2.0 : 0.0;
                break;
            }
            case ScratchType::Twiddle:
                speed = std::sin(2.0 * kPi * 4.0 * t) * 1.5 * std::cos(2.0 * kPi * 0.5 * t);
                break;
            case ScratchType::Hydroplane:
                speed = std::sin(2.0 * kPi * 1.0 * t) * 3.0;
                break;
        }

        posFrame += speed * srRatio;
        posFrame = std::clamp(posFrame, 0.0, static_cast<double>(numFrames - 1));

        size_t frame0 = static_cast<size_t>(posFrame);
        float frac = static_cast<float>(posFrame - frame0);
        size_t frame1 = std::min(frame0 + 1, numFrames - 1);

        for (int ch = 0; ch < channels; ++ch) {
            int srcCh = std::min(ch, track.getChannels() - 1);
            float s0 = track.getSample(frame0, srcCh);
            float s1 = track.getSample(frame1, srcCh);
            output[i * channels + ch] = s0 + (s1 - s0) * frac;
        }
    }
}

}
