#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

namespace BeatMate::Core {

struct CuePoint {
    int number = 0;        // 1-8
    double position = 0.0; // seconds
    uint32_t color = 0xFFFF0000;
    std::string name;
    std::string deckId;
};

class HotCueManager {
public:
    HotCueManager();
    ~HotCueManager();

    void setCue(const std::string& deckId, int number, double position,
                uint32_t color = 0xFFFF0000, const std::string& name = "");
    CuePoint getCue(const std::string& deckId, int number) const;
    bool hasCue(const std::string& deckId, int number) const;
    void deleteCue(const std::string& deckId, int number);
    void deleteAllCues(const std::string& deckId);
    std::vector<CuePoint> getAllCues(const std::string& deckId) const;

private:
    std::map<std::string, std::map<int, CuePoint>> cues_; // deckId -> number -> cue
    mutable std::mutex mutex_;
};

}
