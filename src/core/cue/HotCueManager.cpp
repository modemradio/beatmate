#include "HotCueManager.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

HotCueManager::HotCueManager() = default;
HotCueManager::~HotCueManager() = default;

void HotCueManager::setCue(const std::string& deckId, int number, double position,
                            uint32_t color, const std::string& name) {
    if (number < 1 || number > 8) return;
    std::lock_guard<std::mutex> lock(mutex_);
    CuePoint cue;
    cue.number = number;
    cue.position = position;
    cue.color = color;
    cue.name = name.empty() ? ("Cue " + std::to_string(number)) : name;
    cue.deckId = deckId;
    cues_[deckId][number] = cue;
    spdlog::info("HotCue: set {} #{} at {:.3f}s", deckId, number, position);
}

CuePoint HotCueManager::getCue(const std::string& deckId, int number) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto dit = cues_.find(deckId);
    if (dit == cues_.end()) return {};
    auto cit = dit->second.find(number);
    if (cit == dit->second.end()) return {};
    return cit->second;
}

bool HotCueManager::hasCue(const std::string& deckId, int number) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto dit = cues_.find(deckId);
    if (dit == cues_.end()) return false;
    return dit->second.count(number) > 0;
}

void HotCueManager::deleteCue(const std::string& deckId, int number) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto dit = cues_.find(deckId);
    if (dit != cues_.end()) dit->second.erase(number);
}

void HotCueManager::deleteAllCues(const std::string& deckId) {
    std::lock_guard<std::mutex> lock(mutex_);
    cues_.erase(deckId);
}

std::vector<CuePoint> HotCueManager::getAllCues(const std::string& deckId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CuePoint> result;
    auto dit = cues_.find(deckId);
    if (dit != cues_.end()) {
        for (auto& [num, cue] : dit->second) result.push_back(cue);
    }
    return result;
}

} // namespace BeatMate::Core
