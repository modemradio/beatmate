#pragma once
#include <memory>
namespace BeatMate::Core {
class AudioTrack;
class PaulstretchProcessor {
public:
    PaulstretchProcessor();
    ~PaulstretchProcessor();
    std::shared_ptr<AudioTrack> process(const AudioTrack& input, double stretchFactor);
    void setWindowSize(int samples) { windowSize_ = samples; }
private:
    int windowSize_ = 4096;
};
} // namespace BeatMate::Core
