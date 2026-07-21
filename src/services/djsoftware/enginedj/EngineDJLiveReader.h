#pragma once

#include "../DJHistoryReader.h"

#include <optional>
#include <string>

namespace BeatMate::Services::EngineDJ {

class EngineDJLiveReader : public BeatMate::Services::DJSoftware::DJHistoryReader {
public:
    std::vector<BeatMate::Services::DJSoftware::PlayedTrack>
        readRecentHistory(int maxTracks = 200) override;

    std::optional<BeatMate::Services::DJSoftware::PlayedTrack>
        readNowPlaying() override;

    const char* sourceName() const override { return "EngineDJ"; }

    // True if m.db is findable on disk (does not guarantee schema validity).
    bool isPresent();

private:
    static std::string findEngineDbPath();
};

} // namespace BeatMate::Services::EngineDJ
