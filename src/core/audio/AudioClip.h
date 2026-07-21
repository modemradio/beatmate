#pragma once

#include <memory>
#include <atomic>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

class AudioClip {
public:
    AudioClip();
    explicit AudioClip(std::shared_ptr<AudioTrack> track);
    ~AudioClip();

    void setTrack(std::shared_ptr<AudioTrack> track);
    std::shared_ptr<AudioTrack> getTrack() const { return track_; }

    void setStartPosition(double seconds) { startPosition_ = seconds; }
    double getStartPosition() const { return startPosition_; }

    void setEndPosition(double seconds) { endPosition_ = seconds; }
    double getEndPosition() const { return endPosition_; }

    double getDuration() const { return endPosition_ - startPosition_; }

    void setGain(float gainDb) { gain_.store(gainDb); }
    float getGain() const { return gain_.load(); }
    float getLinearGain() const;

    void setPan(float pan) { pan_.store(pan); } // -1 to 1
    float getPan() const { return pan_.load(); }

    void setFadeIn(double seconds) { fadeIn_ = seconds; }
    double getFadeIn() const { return fadeIn_; }

    void setFadeOut(double seconds) { fadeOut_ = seconds; }
    double getFadeOut() const { return fadeOut_; }

    void setLooped(bool looped) { isLooped_.store(looped); }
    bool isLooped() const { return isLooped_.load(); }

    void setMuted(bool muted) { isMuted_.store(muted); }
    bool isMuted() const { return isMuted_.load(); }

    void setName(const std::string& name) { name_ = name; }
    std::string getName() const { return name_; }

    void setColor(uint32_t color) { color_ = color; }
    uint32_t getColor() const { return color_; }

    float getGainAtPosition(double positionInClip) const;

    void readSamples(float* dest, size_t startFrame, size_t numFrames) const;

private:
    std::shared_ptr<AudioTrack> track_;
    double startPosition_ = 0.0;
    double endPosition_ = 0.0;
    std::atomic<float> gain_{0.0f}; // dB
    std::atomic<float> pan_{0.0f};
    double fadeIn_ = 0.0;
    double fadeOut_ = 0.0;
    std::atomic<bool> isLooped_{false};
    std::atomic<bool> isMuted_{false};
    std::string name_;
    uint32_t color_ = 0xFFFFFFFF;
};

} // namespace BeatMate::Core
