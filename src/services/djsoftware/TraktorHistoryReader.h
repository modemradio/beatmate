#pragma once

#include "DJHistoryReader.h"

namespace BeatMate::Services::DJSoftware {

// TraktorHistoryReader - reads Native Instruments Traktor history
class TraktorHistoryReader final : public DJHistoryReader {
public:
    std::vector<PlayedTrack> readRecentHistory(int maxTracks = 200) override;
    const char* sourceName() const override { return "Traktor"; }
};

} // namespace BeatMate::Services::DJSoftware
