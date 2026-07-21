#pragma once
#include "../../core/audio/AudioTrack.h"
namespace BeatMate::Services::Normalization {
class LoudnessNormalizer {
public:
    LoudnessNormalizer() = default;
    Core::AudioTrack normalize(const Core::AudioTrack& track, float targetLUFS = -14.0f);
    float measureLUFS(const Core::AudioTrack& track) const;
};
}
