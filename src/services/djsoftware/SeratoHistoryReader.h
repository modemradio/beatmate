#pragma once

#include "DJHistoryReader.h"

namespace BeatMate::Services::DJSoftware {

class SeratoHistoryReader final : public DJHistoryReader {
public:
    std::vector<PlayedTrack> readRecentHistory(int maxTracks = 200) override;
    const char* sourceName() const override { return "Serato"; }

private:
    std::vector<PlayedTrack> parseBinarySessions(const std::string& sessionsDir, int maxTracks);
    std::vector<PlayedTrack> parseCsvFallback   (const std::string& sessionsDir, int maxTracks);
};

} // namespace BeatMate::Services::DJSoftware
