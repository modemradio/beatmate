#pragma once

#include <memory>
#include <string>
#include <atomic>

namespace BeatMate::Core {

class AudioTrack;
class AudioPlayer;
class AudioFileReader;

class AudioPreview {
public:
    AudioPreview();
    ~AudioPreview();

    bool previewTrack(const std::string& path, double startSec = 0.0,
                      double durationSec = 30.0);

    void stop();

    // Fill output buffer (called from audio thread)
    void processBlock(float* output, int numFrames, int channels);

    bool isPlaying() const { return playing_.load(); }
    double getPosition() const;

    void setVolume(float vol);

private:
    std::unique_ptr<AudioFileReader> reader_;
    std::unique_ptr<AudioPlayer> player_;
    std::shared_ptr<AudioTrack> previewTrack_;
    std::atomic<bool> playing_{false};
};

} // namespace BeatMate::Core
