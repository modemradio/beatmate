#include "ReplayGainCalc.h"
#include <spdlog/spdlog.h>
#include <cmath>
namespace BeatMate::Services::Normalization {
float ReplayGainCalc::calculate(const Core::AudioTrack& track) {
    if (!track.isLoaded()) return 0.0f;
    double sumSquares = 0.0;
    size_t n = track.getTotalSamples();
    for (size_t i = 0; i < n; ++i) { float s = track.getSample(i); sumSquares += s * s; }
    double rms = std::sqrt(sumSquares / n);
    float rmsDB = static_cast<float>(20.0 * std::log10(rms + 1e-10));
    float gain = REFERENCE_LEVEL - rmsDB;
    spdlog::debug("ReplayGainCalc: RMS={:.1f} dB, Gain={:.1f} dB", rmsDB, gain);
    return gain;
}
float ReplayGainCalc::calculateAlbumGain(const std::vector<Core::AudioTrack>& tracks) {
    float totalGain = 0.0f;
    for (const auto& t : tracks) totalGain += calculate(t);
    return tracks.empty() ? 0.0f : totalGain / tracks.size();
}
} // namespace BeatMate::Services::Normalization
