#include "DrumMachine.h"
#include <cstring>

namespace BeatMate::Core {

DrumMachine::DrumMachine() {
    for (auto& track : pattern_) track.fill(false);
}
DrumMachine::~DrumMachine() = default;

void DrumMachine::setStep(int track, int step, bool active) {
    if (track >= 0 && track < kTracks && step >= 0 && step < kSteps)
        pattern_[track][step] = active;
}

bool DrumMachine::getStep(int track, int step) const {
    if (track >= 0 && track < kTracks && step >= 0 && step < kSteps)
        return pattern_[track][step];
    return false;
}

void DrumMachine::start() { playing_.store(true); stepPosition_ = 0; currentStep_ = 0; }
void DrumMachine::stop() { playing_.store(false); sampler_.stopAll(); }

bool DrumMachine::loadKit(int track, const std::string& samplePath) {
    return sampler_.loadSample(track, samplePath);
}

void DrumMachine::processBlock(float* output, int numFrames, int channels, int sampleRate) {
    std::memset(output, 0, numFrames * channels * sizeof(float));
    if (!playing_.load()) return;

    double bpm = bpm_.load();
    double stepDuration = 60.0 / bpm / 4.0; // 16th notes
    double stepDurationSamples = stepDuration * sampleRate;

    for (int i = 0; i < numFrames; ++i) {
        int step = static_cast<int>(stepPosition_ / stepDurationSamples) % kSteps;

        if (step != currentStep_) {
            currentStep_ = step;
            for (int track = 0; track < kTracks; ++track) {
                if (pattern_[track][step]) {
                    sampler_.triggerPad(track);
                }
            }
        }
        stepPosition_ += 1.0;
    }

    sampler_.processBlock(output, numFrames, channels);
}

} // namespace BeatMate::Core
