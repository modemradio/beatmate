#pragma once
#include <string>
#include <vector>
namespace BeatMate::Core {
class AudioTrack;
enum class ScratchType { Baby, Scribble, Tear, Chirp, Flare, Orbit, Transform, Crab, Twiddle, Hydroplane };
class ScratchEngine {
public:
    ScratchEngine();
    ~ScratchEngine();
    void apply(float* output, const AudioTrack& track, double position, ScratchType type,
               int numSamples, int channels, int sampleRate);
    static std::string getScratchName(ScratchType type);
    static std::vector<ScratchType> getAllTypes();
private:
    float lastSample_ = 0.0f;
};
} // namespace BeatMate::Core
