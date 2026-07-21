#pragma once
#include <cmath>
namespace BeatMate::Core {
enum class CrossfadeType { Linear, EqualPower, SCurve, ConstantPower };
class CrossfadeEngine {
public:
    CrossfadeEngine() = default;
    void crossfade(const float* trackA, const float* trackB, float* output,
                   int numSamples, int channels, float position, CrossfadeType type);
    static float getCurve(float position, CrossfadeType type, bool isA);
};
} // namespace BeatMate::Core
