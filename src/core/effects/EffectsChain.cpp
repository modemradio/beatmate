#include "EffectsChain.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

EffectsChain::EffectsChain() = default;
EffectsChain::~EffectsChain() = default;

int EffectsChain::addEffect(std::unique_ptr<DSPProcessor> effect) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.push_back(std::move(effect));
    int idx = static_cast<int>(effects_.size()) - 1;
    spdlog::info("EffectsChain: added {} at index {}", effects_.back()->getName(), idx);
    return idx;
}

bool EffectsChain::removeEffect(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(effects_.size())) return false;
    effects_.erase(effects_.begin() + index);
    return true;
}

void EffectsChain::moveEffect(int from, int to) {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = static_cast<int>(effects_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    auto effect = std::move(effects_[from]);
    effects_.erase(effects_.begin() + from);
    effects_.insert(effects_.begin() + to, std::move(effect));
}

void EffectsChain::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.clear();
}

void EffectsChain::process(float* buffer, int numSamples, int channels) {
    if (bypassed_.load()) return;

    // Audio thread : never block. If the UI thread is mutating the chain, just
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;
    for (auto& effect : effects_) {
        effect->processWithMix(buffer, numSamples, channels);
    }
}

DSPProcessor* EffectsChain::getEffect(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(effects_.size())) return nullptr;
    return effects_[index].get();
}

int EffectsChain::getEffectCount() const {
    // Audio thread may call this for diagnostics ; try_lock instead of blocking.
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return 0;
    return static_cast<int>(effects_.size());
}

void EffectsChain::setSampleRate(double sr) {
    // setSampleRate is normally called from the host thread when the device
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;
    for (auto& effect : effects_) {
        effect->setSampleRate(sr);
    }
}

} // namespace BeatMate::Core
