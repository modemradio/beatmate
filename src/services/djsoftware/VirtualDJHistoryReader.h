#pragma once

#include "DJHistoryReader.h"

namespace BeatMate::Services::DJSoftware {

class VirtualDJHistoryReader final : public DJHistoryReader {
public:
    std::vector<PlayedTrack> readRecentHistory(int maxTracks = 200) override;
    const char* sourceName() const override { return "VirtualDJ"; }
};

} // namespace BeatMate::Services::DJSoftware
