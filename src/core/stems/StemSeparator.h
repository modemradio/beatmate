#pragma once

#include <functional>
#include <memory>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct StemResult {
    std::shared_ptr<AudioTrack> vocals;
    std::shared_ptr<AudioTrack> drums;
    std::shared_ptr<AudioTrack> bass;
    std::shared_ptr<AudioTrack> other;
    bool success = false;
};

using StemProgressCallback = std::function<void(float progress, const std::string& stage)>;

class StemSeparator {
public:
    StemSeparator();
    ~StemSeparator();

    StemResult separate(const AudioTrack& track, StemProgressCallback progress = nullptr);

private:
    StemResult separateWithSpectral(const AudioTrack& track, StemProgressCallback progress);
};

} // namespace BeatMate::Core
