#include "MemoryCueManager.h"
#include <algorithm>

namespace BeatMate::Core {

int MemoryCueManager::addCue(const std::string& trackId, double position,
                              const std::string& name, uint32_t color) {
    std::lock_guard<std::mutex> lock(mutex_);
    CuePoint cue;
    cue.number = nextId_++;
    cue.position = position;
    cue.name = name.empty() ? ("Memory " + std::to_string(cue.number)) : name;
    cue.color = color;
    cue.deckId = trackId;
    cues_[trackId].push_back(cue);
    return cue.number;
}

void MemoryCueManager::removeCue(const std::string& trackId, int cueId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& cues = cues_[trackId];
    cues.erase(std::remove_if(cues.begin(), cues.end(),
        [cueId](const CuePoint& c) { return c.number == cueId; }), cues.end());
}

std::vector<CuePoint> MemoryCueManager::getCues(const std::string& trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cues_.find(trackId);
    if (it != cues_.end()) return it->second;
    return {};
}

void MemoryCueManager::clearAll(const std::string& trackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    cues_.erase(trackId);
}

} // namespace BeatMate::Core
