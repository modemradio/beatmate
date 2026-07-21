#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class AutoGain {
public:
    AutoGain() = default;
    float calculateGain(const AudioTrack& track, float targetLUFS = -14.0f);
    void setTargetLUFS(float lufs) { targetLUFS_ = lufs; }
private:
    float targetLUFS_ = -14.0f;
};
}
