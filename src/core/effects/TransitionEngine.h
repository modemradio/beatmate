#pragma once
#include <map>
#include <string>
#include <vector>

namespace BeatMate::Core {

enum class TransitionType {
    Cut, XFade, Echo, Backspin, Brake, Filter, Hook,
    PingPong, Extended, Scratch, PowerDown, DropSwap, BeatMatch, VocalBridge
};

struct TransitionParams {
    float duration = 4.0f;
    float intensity = 0.5f;
    float bpm = 0.0f;
};

class TransitionEngine {
public:
    TransitionEngine();
    ~TransitionEngine();

    void apply(const float* trackA, const float* trackB, float* output,
               int numSamples, int channels, int sampleRate,
               TransitionType type, const TransitionParams& params, float progress);

    static std::string getTransitionName(TransitionType type);
    static std::vector<TransitionType> getAllTransitions();

    static float sampleEnvelope(TransitionType type, float t);

private:
    void applyCut(const float* a, const float* b, float* out, int n, int ch, float p);
    void applyXFade(const float* a, const float* b, float* out, int n, int ch, float p);
    void applyEcho(const float* a, const float* b, float* out, int n, int ch, int sr, float p);
    void applyBrake(const float* a, const float* b, float* out, int n, int ch, float p);
    void applyFilter(const float* a, const float* b, float* out, int n, int ch, int sr, float p);
    void applyPowerDown(const float* a, const float* b, float* out, int n, int ch, float p);
};

}
