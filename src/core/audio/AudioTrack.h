#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Core {

struct WaveformData {
    std::vector<float> peaks;    // Max amplitude per segment
    std::vector<float> rms;      // RMS per segment
    int resolution = 0;         // samples per segment
};

class AudioTrack {
public:
    AudioTrack();
    ~AudioTrack();

    // Copy deleted because of std::mutex
    AudioTrack(AudioTrack&& other) noexcept;
    AudioTrack& operator=(AudioTrack&& other) noexcept;
    AudioTrack(const AudioTrack&) = delete;
    AudioTrack& operator=(const AudioTrack&) = delete;

    void loadData(std::vector<float>&& pcmData, int sampleRate, int channels);
    void loadData(const float* data, size_t numSamples, int sampleRate, int channels);

    // Sample access is thread-safe
    float getSample(size_t index) const;
    float getSample(size_t frame, int channel) const;
    const float* getRawData() const;
    size_t getTotalSamples() const;

    void getSamples(float* dest, size_t startFrame, size_t numFrames) const;

    int getSampleRate() const { return sampleRate_; }
    int getChannels() const { return channels_; }
    size_t getNumFrames() const;
    double getDuration() const; // seconds

    void setFilePath(const std::string& path) { filePath_ = path; }
    std::string getFilePath() const { return filePath_; }
    void setTitle(const std::string& title) { title_ = title; }
    std::string getTitle() const { return title_; }
    void setArtist(const std::string& artist) { artist_ = artist; }
    std::string getArtist() const { return artist_; }

    void setWaveform(WaveformData&& wf) { waveform_ = std::move(wf); }
    const WaveformData& getWaveform() const { return waveform_; }
    bool hasWaveform() const { return !waveform_.peaks.empty(); }

    bool isLoaded() const { return !data_.empty(); }

    void normalize(float targetPeak = 1.0f);

    AudioTrack toMono() const;

    AudioTrack resample(int targetSampleRate) const;

private:
    std::vector<float> data_; // Interleaved PCM
    int sampleRate_ = 44100;
    int channels_ = 2;

    std::string filePath_;
    std::string title_;
    std::string artist_;

    WaveformData waveform_;

    mutable std::mutex dataMutex_;
};

} // namespace BeatMate::Core
