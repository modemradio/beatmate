#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

namespace BeatMate::Core {

// Preview instantane via chaine de streaming JUCE : lecture <10 ms sans decoder tout le fichier.
class StreamingPlayer {
public:
    StreamingPlayer();
    ~StreamingPlayer();

    void prepare(double sampleRate, int blockSize);
    void release();

    bool loadAndPlay(const juce::File& file, double startSec = 0.0);

    void stop();
    void pause();
    void play();
    bool isPlaying() const { return transport_.isPlaying(); }
    double getPosition() const { return transport_.getCurrentPosition(); }
    void setPosition(double sec) { transport_.setPosition(sec); }
    double getDuration() const { return transport_.getLengthInSeconds(); }
    void setGain(float g) { transport_.setGain(g); }

    // Called from the audio thread
    void processBlock(float* interleavedOut, int numFrames, int numChannels);

private:
    juce::AudioFormatManager formatManager_;
    juce::TimeSliceThread readAheadThread_ { "StreamingPlayer-ReadAhead" };

    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
    juce::AudioTransportSource transport_;

    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    std::atomic<bool> prepared_ { false };

    // Swap de source sans clic : rampe de 256 echantillons dans processBlock
    std::atomic<bool> m_fadingOut_ { false };
    std::atomic<int>  m_fadeRemaining_ { 0 };       // samples left in the ramp
    static constexpr int kFadeOutSamples = 256;

    juce::AudioBuffer<float> scratch_;
};

} // namespace BeatMate::Core
