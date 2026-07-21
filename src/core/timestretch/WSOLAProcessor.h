#pragma once
#include <memory>
#include <vector>
namespace BeatMate::Core {
class AudioTrack;
class WSOLAProcessor {
public:
    WSOLAProcessor();
    ~WSOLAProcessor();
    std::shared_ptr<AudioTrack> process(const AudioTrack& input, double tempoRatio);
    void setWindowSize(int samples) { windowSize_ = samples; }
private:
    int windowSize_ = 1024;
    int findBestOverlap(const float* src, const float* target, int len, int maxShift);
};
} // namespace BeatMate::Core
