#include "DrumIsolator.h"
#include "../audio/AudioTrack.h"
#include "StemSeparator.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

DrumIsolator::DrumIsolator() = default;
DrumIsolator::~DrumIsolator() = default;

std::shared_ptr<AudioTrack> DrumIsolator::isolate(const AudioTrack& track) {
    StemSeparator separator;
    auto stems = separator.separate(track);

    if (stems.success && stems.drums) {
        spdlog::info("DrumIsolator: drum isolation complete");
        return stems.drums;
    }

    spdlog::warn("DrumIsolator: separation failed");
    return nullptr;
}

}
