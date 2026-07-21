#pragma once
#include "HotCueManager.h"
#include <map>
#include <mutex>
#include <vector>
namespace BeatMate::Core {
class MemoryCueManager {
public:
    MemoryCueManager() = default;
    ~MemoryCueManager() = default;
    int addCue(const std::string& trackId, double position, const std::string& name = "", uint32_t color = 0xFF808080);
    void removeCue(const std::string& trackId, int cueId);
    std::vector<CuePoint> getCues(const std::string& trackId) const;
    void clearAll(const std::string& trackId);
private:
    std::map<std::string, std::vector<CuePoint>> cues_;
    mutable std::mutex mutex_;
    int nextId_ = 1;
};
} // namespace BeatMate::Core
