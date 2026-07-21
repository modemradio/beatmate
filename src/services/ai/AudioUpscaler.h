#pragma once
#include <string>
#include <memory>
#include "../../core/audio/AudioTrack.h"

namespace BeatMate::Services::AI {
class ONNXInference;

class AudioUpscaler {
public:
    explicit AudioUpscaler(std::shared_ptr<ONNXInference> inference);
    ~AudioUpscaler() = default;
    Core::AudioTrack upscale(const Core::AudioTrack& track, int targetSampleRate = 96000);
    bool loadModel(const std::string& path);
private:
    std::shared_ptr<ONNXInference> inference_;
};

} // namespace BeatMate::Services::AI
