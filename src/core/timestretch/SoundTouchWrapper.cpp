#include "SoundTouchWrapper.h"
#include "../audio/AudioTrack.h"
#include <SoundTouch.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

struct SoundTouchWrapper::Impl {
    soundtouch::SoundTouch st;
};

SoundTouchWrapper::SoundTouchWrapper() : impl_(std::make_unique<Impl>()) {}
SoundTouchWrapper::~SoundTouchWrapper() = default;

void SoundTouchWrapper::initialize(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;
    impl_->st.setSampleRate(sampleRate);
    impl_->st.setChannels(channels);
    impl_->st.setSetting(SETTING_USE_AA_FILTER, 1);
    impl_->st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
    impl_->st.setSetting(SETTING_SEQUENCE_MS, 40);
    impl_->st.setSetting(SETTING_SEEKWINDOW_MS, 15);
    impl_->st.setSetting(SETTING_OVERLAP_MS, 8);
}

void SoundTouchWrapper::setTempo(double ratio) {
    impl_->st.setTempo(ratio);
}

void SoundTouchWrapper::setPitch(double semitones) {
    impl_->st.setPitchSemiTones(static_cast<float>(semitones));
}

void SoundTouchWrapper::setRate(double ratio) {
    impl_->st.setRate(ratio);
}

std::shared_ptr<AudioTrack> SoundTouchWrapper::process(const AudioTrack& input) {
    const float* srcData = input.getRawData();
    size_t totalSamples = input.getTotalSamples();
    int ch = input.getChannels();
    size_t numFrames = totalSamples / ch;

    size_t chunkSize = 4096;
    std::vector<float> output;
    output.reserve(totalSamples * 2);

    for (size_t offset = 0; offset < numFrames; offset += chunkSize) {
        size_t frames = std::min(chunkSize, numFrames - offset);
        impl_->st.putSamples(srcData + offset * ch, static_cast<uint>(frames));

        float tempBuf[8192];
        uint received;
        do {
            received = impl_->st.receiveSamples(tempBuf, 4096 / ch);
            output.insert(output.end(), tempBuf, tempBuf + received * ch);
        } while (received > 0);
    }

    impl_->st.flush();
    float tempBuf[8192];
    uint received;
    do {
        received = impl_->st.receiveSamples(tempBuf, 4096 / ch);
        output.insert(output.end(), tempBuf, tempBuf + received * ch);
    } while (received > 0);

    auto result = std::make_shared<AudioTrack>();
    result->loadData(std::move(output), sampleRate_, ch);

    spdlog::info("SoundTouchWrapper: processed {} -> {} frames",
                 numFrames, result->getNumFrames());
    return result;
}

void SoundTouchWrapper::reset() {
    impl_->st.clear();
}

} // namespace BeatMate::Core
