#include "StreamingPlayer.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>

namespace BeatMate::Core {

StreamingPlayer::StreamingPlayer() {
    formatManager_.registerBasicFormats();
    // NO Windows Media Foundation - user preference
    readAheadThread_.startThread(juce::Thread::Priority::high);
}

StreamingPlayer::~StreamingPlayer() {
    release();
    readAheadThread_.stopThread(1000);
}

void StreamingPlayer::prepare(double sr, int bs) {
    sampleRate_ = sr > 0 ? sr : 44100.0;
    blockSize_ = bs > 0 ? bs : 512;
    transport_.prepareToPlay(blockSize_, sampleRate_);
    // Over-allocate so the audio callback never needs to resize (allocating
    scratch_.setSize(8, 16384, /*keepExisting*/ false,
                     /*clearExtraSpace*/ true, /*avoidReallocating*/ false);
    scratch_.clear();
    prepared_.store(true);
    spdlog::info("StreamingPlayer: prepared at {}Hz, blockSize={} (scratch 8x16384)",
                 sampleRate_, blockSize_);
}

void StreamingPlayer::release() {
    prepared_.store(false);
    transport_.stop();
    transport_.setSource(nullptr);
    transport_.releaseResources();
    readerSource_.reset();
}

bool StreamingPlayer::loadAndPlay(const juce::File& file, double startSec) {
    auto t0 = std::chrono::steady_clock::now();

    // Step 1: Open reader - headers only, 1-5ms typically
    juce::AudioFormatReader* reader = formatManager_.createReaderFor(file);
    if (!reader) {
        spdlog::error("StreamingPlayer: no reader for {}", file.getFullPathName().toStdString());
        return false;
    }
    auto t1 = std::chrono::steady_clock::now();

    // Step 2: Wrap in AudioFormatReaderSource (takes ownership)
    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);

    double fileSR = reader->sampleRate;
    const int srcCh = static_cast<int>(reader->numChannels);

    // Step 3: Schedule a 256-sample fade-out before stopping the transport.
    if (transport_.isPlaying()) {
        m_fadeRemaining_.store(kFadeOutSamples, std::memory_order_release);
        m_fadingOut_.store(true, std::memory_order_release);
        // Wait for the audio thread to drain the ramp. 256 samples @ 44.1kHz
        const auto fadeStart = std::chrono::steady_clock::now();
        while (m_fadingOut_.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() - fadeStart
                > std::chrono::milliseconds(50)) {
                m_fadingOut_.store(false, std::memory_order_release);
                break;
            }
            juce::Thread::sleep(1);
        }
    }
    transport_.stop();
    // Use juce::jmax(2, srcCh) so mono and 5.1/multichannel sources are
    transport_.setSource(newSource.get(),
                         32768,                          // readAhead samples
                         &readAheadThread_,
                         fileSR,                         // source sample rate
                         juce::jmax(2, srcCh));          // max channels
    readerSource_ = std::move(newSource);

    // Step 4: Seek, then wait briefly for the read-ahead buffer to fill so
    transport_.setPosition(startSec);
    {
        const auto seekStart = std::chrono::steady_clock::now();
        while (!transport_.hasStreamFinished()) {
            if (std::chrono::steady_clock::now() - seekStart
                > std::chrono::milliseconds(50)) break;
            juce::Thread::sleep(1);
        }
    }
    transport_.start();

    auto t2 = std::chrono::steady_clock::now();
    auto openMs = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count() / 1000.0;
    auto setupMs = std::chrono::duration_cast<std::chrono::microseconds>(t2-t1).count() / 1000.0;
    spdlog::info("StreamingPlayer: file={}Hz device={}Hz srcCh={} openReader={:.1f}ms setup={:.1f}ms",
                 fileSR, sampleRate_, srcCh, openMs, setupMs);
    return true;
}

void StreamingPlayer::stop() {
    transport_.stop();
}

void StreamingPlayer::pause() {
    transport_.stop(); // JUCE AudioTransportSource has no pause, stop keeps position
}

void StreamingPlayer::play() {
    transport_.start();
}

void StreamingPlayer::processBlock(float* out, int numFrames, int numChannels) {
    if (!prepared_.load()) {
        std::memset(out, 0, static_cast<size_t>(numFrames * numChannels) * sizeof(float));
        return;
    }

    // NEVER setSize() in the audio callback — that allocates on the real-time
    if (numChannels > scratch_.getNumChannels() || numFrames > scratch_.getNumSamples()) {
        std::memset(out, 0, static_cast<size_t>(numFrames * numChannels) * sizeof(float));
        return;
    }
    // clearActiveBufferRegion: only zero the slice we're about to fill.
    for (int ch = 0; ch < scratch_.getNumChannels(); ++ch)
        std::memset(scratch_.getWritePointer(ch), 0, static_cast<size_t>(numFrames) * sizeof(float));

    juce::AudioSourceChannelInfo info(&scratch_, 0, numFrames);
    transport_.getNextAudioBlock(info);

    // Click-free swap: if loadAndPlay armed a fade-out, ramp the scratch
    if (m_fadingOut_.load(std::memory_order_acquire)) {
        int remaining = m_fadeRemaining_.load(std::memory_order_relaxed);
        const int chCount = scratch_.getNumChannels();
        for (int s = 0; s < numFrames; ++s) {
            const float gain = remaining > 0
                ? static_cast<float>(remaining) / static_cast<float>(kFadeOutSamples)
                : 0.0f;
            for (int ch = 0; ch < chCount; ++ch)
                scratch_.getWritePointer(ch)[s] *= gain;
            if (remaining > 0) --remaining;
        }
        m_fadeRemaining_.store(remaining, std::memory_order_relaxed);
        if (remaining <= 0)
            m_fadingOut_.store(false, std::memory_order_release);
    }

    const int srcCh = scratch_.getNumChannels();
    if (numChannels == 2 && srcCh >= 2) {
        const float* L = scratch_.getReadPointer(0);
        const float* R = scratch_.getReadPointer(1);
        for (int s = 0; s < numFrames; ++s) {
            out[s * 2]     = L[s];
            out[s * 2 + 1] = R[s];
        }
    } else {
        for (int s = 0; s < numFrames; ++s) {
            for (int c = 0; c < numChannels; ++c) {
                int srcC = std::min(c, srcCh - 1);
                out[s * numChannels + c] = scratch_.getReadPointer(srcC)[s];
            }
        }
    }
}

} // namespace BeatMate::Core
