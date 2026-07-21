#include "SignalsmithWrapper.h"
#include "../audio/AudioTrack.h"
#include <cmath>
#include <spdlog/spdlog.h>

// Signalsmith Stretch is a header-only library
#if __has_include(<signalsmith-stretch.h>)
#define HAS_SIGNALSMITH
#include <signalsmith-stretch.h>
#endif

namespace BeatMate::Core {

struct SignalsmithWrapper::Impl {
#ifdef HAS_SIGNALSMITH
    signalsmith::stretch::SignalsmithStretch<float> stretch;
#endif
};

SignalsmithWrapper::SignalsmithWrapper() : impl_(std::make_unique<Impl>()) {}
SignalsmithWrapper::~SignalsmithWrapper() = default;

void SignalsmithWrapper::initialize(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;
#ifdef HAS_SIGNALSMITH
    impl_->stretch.presetDefault(channels, sampleRate);
#endif
}

void SignalsmithWrapper::setStretchFactor(double factor) { stretchFactor_ = factor; }
void SignalsmithWrapper::setPitchFactor(double factor) { pitchFactor_ = factor; }

std::shared_ptr<AudioTrack> SignalsmithWrapper::process(const AudioTrack& input) {
#ifdef HAS_SIGNALSMITH
    size_t inputFrames = input.getNumFrames();
    size_t outputFrames = static_cast<size_t>(inputFrames * stretchFactor_);

    std::vector<std::vector<float>> inputChannels(channels_);
    std::vector<std::vector<float>> outputChannels(channels_);

    for (int ch = 0; ch < channels_; ++ch) {
        inputChannels[ch].resize(inputFrames);
        outputChannels[ch].resize(outputFrames);
        for (size_t i = 0; i < inputFrames; ++i) {
            inputChannels[ch][i] = input.getSample(i, ch);
        }
    }

    std::vector<float*> inputPtrs(channels_), outputPtrs(channels_);
    for (int ch = 0; ch < channels_; ++ch) {
        inputPtrs[ch] = inputChannels[ch].data();
        outputPtrs[ch] = outputChannels[ch].data();
    }

    impl_->stretch.setTransposeFactor(pitchFactor_);

    size_t blockSize = 1024;
    size_t inPos = 0, outPos = 0;

    while (inPos < inputFrames && outPos < outputFrames) {
        size_t inBlock = std::min(blockSize, inputFrames - inPos);
        size_t outBlock = static_cast<size_t>(inBlock * stretchFactor_);
        outBlock = std::min(outBlock, outputFrames - outPos);

        std::vector<float*> inPtrs(channels_), outPtrs(channels_);
        for (int ch = 0; ch < channels_; ++ch) {
            inPtrs[ch] = inputChannels[ch].data() + inPos;
            outPtrs[ch] = outputChannels[ch].data() + outPos;
        }

        impl_->stretch.process(inPtrs.data(), static_cast<int>(inBlock),
                                outPtrs.data(), static_cast<int>(outBlock));
        inPos += inBlock;
        outPos += outBlock;
    }

    std::vector<float> interleaved(outputFrames * channels_);
    for (size_t i = 0; i < outputFrames; ++i) {
        for (int ch = 0; ch < channels_; ++ch) {
            interleaved[i * channels_ + ch] = outputChannels[ch][i];
        }
    }

    auto result = std::make_shared<AudioTrack>();
    result->loadData(std::move(interleaved), sampleRate_, channels_);
    return result;
#else
    spdlog::warn("SignalsmithWrapper: library not available, returning copy");
    auto result = std::make_shared<AudioTrack>();
    result->loadData(input.getRawData(), input.getTotalSamples(),
                     input.getSampleRate(), input.getChannels());
    return result;
#endif
}

void SignalsmithWrapper::reset() {
#ifdef HAS_SIGNALSMITH
    impl_->stretch.reset();
#endif
}

} // namespace BeatMate::Core
