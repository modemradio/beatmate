#include "StemPlaybackRouter.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace BeatMate::Core {

StemPlaybackRouter::StemPlaybackRouter() {
    for (auto& state : states_) state = StemState{};
}

void StemPlaybackRouter::loadStem(StemType type, std::shared_ptr<AudioTrack> audio) {
    std::lock_guard<std::mutex> lock(mutex_);
    stems_[static_cast<size_t>(type)] = std::move(audio);
    spdlog::info("StemPlaybackRouter: Loaded {} stem", stemTypeName(type));
}

void StemPlaybackRouter::unloadStem(StemType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    stems_[static_cast<size_t>(type)].reset();
}

void StemPlaybackRouter::unloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : stems_) s.reset();
}

bool StemPlaybackRouter::hasStem(StemType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stems_[static_cast<size_t>(type)] != nullptr;
}

int StemPlaybackRouter::getLoadedStemCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& s : stems_) if (s) count++;
    return count;
}

void StemPlaybackRouter::setStemMuted(StemType type, bool muted) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)].muted = muted;
    notifyCallbacks(type);
}

void StemPlaybackRouter::setStemSoloed(StemType type, bool soloed) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)].soloed = soloed;
    notifyCallbacks(type);
}

void StemPlaybackRouter::setStemVolume(StemType type, float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)].volume = std::clamp(volume, 0.0f, 2.0f);
    notifyCallbacks(type);
}

void StemPlaybackRouter::setStemPan(StemType type, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)].pan = std::clamp(pan, -1.0f, 1.0f);
}

void StemPlaybackRouter::setStemEQ(StemType type, float low, float mid, float high) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& s = states_[static_cast<size_t>(type)];
    s.lowEQ = std::clamp(low, -24.0f, 12.0f);
    s.midEQ = std::clamp(mid, -24.0f, 12.0f);
    s.highEQ = std::clamp(high, -24.0f, 12.0f);
}

void StemPlaybackRouter::setBypassEQ(StemType type, bool bypass) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)].bypassEQ = bypass;
}

void StemPlaybackRouter::resetStem(StemType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_[static_cast<size_t>(type)] = StemState{};
    notifyCallbacks(type);
}

void StemPlaybackRouter::resetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : states_) s = StemState{};
}

StemState StemPlaybackRouter::getStemState(StemType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_[static_cast<size_t>(type)];
}

bool StemPlaybackRouter::isMuted(StemType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_[static_cast<size_t>(type)].muted;
}

bool StemPlaybackRouter::isSoloed(StemType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_[static_cast<size_t>(type)].soloed;
}

bool StemPlaybackRouter::hasAnySoloed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : states_) if (s.soloed) return true;
    return false;
}

void StemPlaybackRouter::processBlock(float* outputBuffer, int numFrames, int numChannels,
                                        int64_t startFrame) {
    std::fill(outputBuffer, outputBuffer + numFrames * numChannels, 0.0f);
    getStemmixSamples(outputBuffer, numFrames, numChannels, startFrame);
}

void StemPlaybackRouter::getStemmixSamples(float* dest, int numFrames, int numChannels,
                                             int64_t startFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool anySoloed = false;
    for (const auto& s : states_) if (s.soloed) { anySoloed = true; break; }

    for (size_t t = 0; t < static_cast<size_t>(StemType::Count); ++t) {
        if (!stems_[t] || !stems_[t]->isLoaded()) continue;

        float gain = computeStemGain(static_cast<StemType>(t));
        if (gain < 1e-6f) continue; // Effectively silent

        const auto& state = states_[t];

        if (anySoloed && !state.soloed) continue;
        if (state.muted) continue;

        float pan = state.pan;
        float leftGain = gain * std::cos((pan + 1.0f) * 0.25f * 3.14159265f);
        float rightGain = gain * std::sin((pan + 1.0f) * 0.25f * 3.14159265f);

        int stemChannels = stems_[t]->getChannels();
        size_t stemFrames = stems_[t]->getNumFrames();

        for (int f = 0; f < numFrames; ++f) {
            int64_t frame = startFrame + f;
            if (frame < 0 || static_cast<size_t>(frame) >= stemFrames) continue;

            float left = stems_[t]->getSample(static_cast<size_t>(frame), 0);
            float right = (stemChannels > 1) ? stems_[t]->getSample(static_cast<size_t>(frame), 1) : left;

            if (numChannels >= 1) dest[f * numChannels + 0] += left * leftGain;
            if (numChannels >= 2) dest[f * numChannels + 1] += right * rightGain;
        }
    }

    for (int i = 0; i < numFrames * numChannels; ++i) {
        dest[i] = std::clamp(dest[i], -1.0f, 1.0f);
    }
}

void StemPlaybackRouter::setVocalsOnly() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : states_) { s.muted = true; s.soloed = false; }
    states_[static_cast<size_t>(StemType::Vocals)].muted = false;
    states_[static_cast<size_t>(StemType::Vocals)].soloed = true;
}

void StemPlaybackRouter::setInstrumentalOnly() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : states_) { s.muted = false; s.soloed = false; }
    states_[static_cast<size_t>(StemType::Vocals)].muted = true;
}

void StemPlaybackRouter::setDrumsAndBassOnly() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : states_) { s.muted = true; s.soloed = false; }
    states_[static_cast<size_t>(StemType::Drums)].muted = false;
    states_[static_cast<size_t>(StemType::Drums)].soloed = true;
    states_[static_cast<size_t>(StemType::Bass)].muted = false;
    states_[static_cast<size_t>(StemType::Bass)].soloed = true;
}

void StemPlaybackRouter::setAcapella() { setVocalsOnly(); }

void StemPlaybackRouter::setKaraoke() { setInstrumentalOnly(); }

void StemPlaybackRouter::registerCallback(StemStateCallback callback) {
    callbacks_.push_back(std::move(callback));
}

std::string StemPlaybackRouter::stemTypeName(StemType type) {
    switch (type) {
        case StemType::Vocals: return "Vocals";
        case StemType::Drums:  return "Drums";
        case StemType::Bass:   return "Bass";
        case StemType::Other:  return "Other";
        case StemType::Melody: return "Melody";
        default: return "Unknown";
    }
}

float StemPlaybackRouter::computeStemGain(StemType type) const {
    const auto& s = states_[static_cast<size_t>(type)];
    if (s.muted) return 0.0f;
    return s.volume;
}

void StemPlaybackRouter::notifyCallbacks(StemType type) {
    auto state = states_[static_cast<size_t>(type)];
    for (const auto& cb : callbacks_) cb(type, state);
}

} // namespace BeatMate::Core
