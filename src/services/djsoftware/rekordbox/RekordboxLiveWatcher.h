#pragma once

#include "../DJHistoryReader.h"

#include <optional>
#include <string>

namespace BeatMate::Services::Rekordbox {

class RekordboxLiveWatcher {
public:
    RekordboxLiveWatcher() = default;
    ~RekordboxLiveWatcher() = default;

    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readNowPlaying();

private:
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readFromWindowTitle();
    std::optional<BeatMate::Services::DJSoftware::PlayedTrack> readFromLogFile();

    std::string lastSignature_;
};

} // namespace BeatMate::Services::Rekordbox
