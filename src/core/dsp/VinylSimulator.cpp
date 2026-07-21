#include "VinylSimulator.h"
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

static constexpr double kPi = 3.14159265358979323846;

VinylSimulator::VinylSimulator() : rng_(std::random_device{}()) {}
VinylSimulator::~VinylSimulator() = default;

void VinylSimulator::setWow(float a) { wow_.store(std::clamp(a, 0.0f, 1.0f)); }
void VinylSimulator::setFlutter(float a) { flutter_.store(std::clamp(a, 0.0f, 1.0f)); }
void VinylSimulator::setCrackle(float a) { crackle_.store(std::clamp(a, 0.0f, 1.0f)); }
void VinylSimulator::setSurfaceNoise(float a) { surfaceNoise_.store(std::clamp(a, 0.0f, 1.0f)); }
void VinylSimulator::setRPM(int rpm) { rpm_ = (rpm == 45) ? 45 : 33; }

void VinylSimulator::process(float* buffer, int numSamples, int channels) {
    float wowAmt = wow_.load();
    float flutterAmt = flutter_.load();
    float crackleAmt = crackle_.load();
    float noiseAmt = surfaceNoise_.load();

    double wowFreq = 0.5;
    double flutterFreq = 12.0;

    float rpmFactor = (rpm_ == 45) ? 1.0f : (33.0f / 45.0f);

    for (int i = 0; i < numSamples; ++i) {
        float pitchMod = 1.0f;
        if (wowAmt > 0.0f) {
            float wow = static_cast<float>(std::sin(2.0 * kPi * wowPhase_));
            pitchMod += wow * wowAmt * 0.003f; // ±0.3% pitch variation
        }
        if (flutterAmt > 0.0f) {
            float flutter = static_cast<float>(std::sin(2.0 * kPi * flutterPhase_));
            pitchMod += flutter * flutterAmt * 0.001f; // ±0.1%
        }

        wowPhase_ += wowFreq / sampleRate_;
        if (wowPhase_ >= 1.0) wowPhase_ -= 1.0;
        flutterPhase_ += flutterFreq / sampleRate_;
        if (flutterPhase_ >= 1.0) flutterPhase_ -= 1.0;

        float crackleNoise = 0.0f;
        if (crackleAmt > 0.0f) {
            float r = dist_(rng_);
            if (r > (1.0f - crackleAmt * 0.001f)) {
                crackleNoise = (dist_(rng_) - 0.5f) * 2.0f * crackleAmt * 0.3f;
            }
        }

        float noise = 0.0f;
        if (noiseAmt > 0.0f) {
            float white = (dist_(rng_) - 0.5f) * 2.0f;
            // Filtre LP pour sonner comme un souffle vinyle
            noiseFilterState_ = noiseFilterState_ * 0.95f + white * 0.05f;
            noise = noiseFilterState_ * noiseAmt * 0.03f;
        }

        for (int ch = 0; ch < channels; ++ch) {
            float s = buffer[i * channels + ch];
            // Simplifié — un vrai pitch shift exigerait un resampling
            s *= pitchMod;
            s += crackleNoise + noise;
            buffer[i * channels + ch] = s;
        }
    }
}

void VinylSimulator::reset() {
    wowPhase_ = flutterPhase_ = 0.0;
    noiseFilterState_ = 0.0f;
}

} // namespace BeatMate::Core
