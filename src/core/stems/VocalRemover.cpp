#include "VocalRemover.h"
#include "../audio/AudioTrack.h"
#include "StemSeparator.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

VocalRemover::VocalRemover() = default;
VocalRemover::~VocalRemover() = default;

std::shared_ptr<AudioTrack> VocalRemover::removeSimple(const AudioTrack& track) {
    if (track.getChannels() < 2) {
        spdlog::warn("VocalRemover: mono track, cannot use phase cancellation");
        return nullptr;
    }

    int sr = track.getSampleRate();
    size_t frames = track.getNumFrames();
    std::vector<float> output(frames * 2);

    for (size_t i = 0; i < frames; ++i) {
        float l = track.getSample(i, 0);
        float r = track.getSample(i, 1);
        float side = (l - r) * 0.5f;
        output[i * 2] = side;
        output[i * 2 + 1] = -side;
    }

    auto result = std::make_shared<AudioTrack>();
    result->loadData(std::move(output), sr, 2);
    spdlog::info("VocalRemover: simple phase cancellation complete");
    return result;
}

std::shared_ptr<AudioTrack> VocalRemover::remove(const AudioTrack& track) {
    StemSeparator separator;
    auto stems = separator.separate(track);

    if (stems.success && stems.drums && stems.bass && stems.other) {
        int sr = track.getSampleRate();
        int ch = track.getChannels();
        size_t frames = track.getNumFrames();
        std::vector<float> instrumental(frames * ch, 0.0f);

        auto mixStem = [&](const std::shared_ptr<AudioTrack>& stem) {
            if (!stem) return;
            size_t n = std::min(frames, stem->getNumFrames());
            for (size_t i = 0; i < n * ch; ++i) {
                instrumental[i] += stem->getRawData()[i];
            }
        };

        mixStem(stems.drums);
        mixStem(stems.bass);
        mixStem(stems.other);

        auto result = std::make_shared<AudioTrack>();
        result->loadData(std::move(instrumental), sr, ch);
        return result;
    }

    return removeSimple(track);
}

} // namespace BeatMate::Core
