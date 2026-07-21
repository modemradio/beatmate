#include "StemSeparator5Channel.h"
#include "../audio/AudioTrack.h"
#include "../dsp/FilterProcessor.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

StemSeparator5Channel::StemSeparator5Channel()
    : baseSeparator_(std::make_unique<StemSeparator>()) {}

StemSeparator5Channel::~StemSeparator5Channel() = default;

StemResult5 StemSeparator5Channel::separate(const AudioTrack& track, StemProgressCallback progress) {
    auto base = baseSeparator_->separate(track, progress);

    StemResult5 result;
    result.vocals = base.vocals;
    result.drums = base.drums;
    result.bass = base.bass;
    result.success = base.success;

    if (base.other) {
        int sr = base.other->getSampleRate();
        int ch = base.other->getChannels();
        size_t total = base.other->getTotalSamples();
        const float* data = base.other->getRawData();

        // Melody: 300-4000Hz
        std::vector<float> melodyData(data, data + total);
        FilterProcessor hp, lp;
        hp.setSampleRate(sr); hp.setType(FilterType::HighPass);
        hp.setFrequency(300.0f); hp.setQ(0.707f);
        hp.process(melodyData.data(), static_cast<int>(total / ch), ch);

        lp.setSampleRate(sr); lp.setType(FilterType::LowPass);
        lp.setFrequency(4000.0f); lp.setQ(0.707f);
        lp.process(melodyData.data(), static_cast<int>(total / ch), ch);

        result.melody = std::make_shared<AudioTrack>();
        result.melody->loadData(std::move(melodyData), sr, ch);

        // Other: everything above 4000Hz
        std::vector<float> otherData(data, data + total);
        FilterProcessor hp2;
        hp2.setSampleRate(sr); hp2.setType(FilterType::HighPass);
        hp2.setFrequency(4000.0f); hp2.setQ(0.707f);
        hp2.process(otherData.data(), static_cast<int>(total / ch), ch);

        result.other = std::make_shared<AudioTrack>();
        result.other->loadData(std::move(otherData), sr, ch);
    }

    spdlog::info("StemSeparator5Channel: separation complete");
    return result;
}

} // namespace BeatMate::Core
