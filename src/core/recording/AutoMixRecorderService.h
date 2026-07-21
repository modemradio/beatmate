#pragma once
#include "LiveSetRecorder.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace BeatMate::Core {

struct AutoRecordSettings {
    bool autoStartOnPlay = true;        // Start recording when playback starts
    bool autoStopOnSilence = true;      // Stop after silence detected
    float silenceThresholdDb = -60.0f;  // Silence detection threshold
    int silenceTimeoutMs = 5000;        // How long silence before auto-stop
    bool autoSplit = false;             // Auto-split on long silence
    int autoSplitSilenceMs = 10000;     // Silence duration to trigger split
    std::string outputDirectory;        // Where to save recordings
    std::string fileFormat = "wav";     // wav, flac, mp3
    int sampleRate = 44100;
    int channels = 2;
    bool addTimestampToFilename = true;
    bool autoAddMarkers = true;         // Auto-add markers on track changes
    int maxFileSizeMB = 0;             // 0 = unlimited
};

class AutoMixRecorderService {
public:
    using RecordingCompleteCallback = std::function<void(const std::string& filePath, double durationSeconds)>;

    AutoMixRecorderService();
    ~AutoMixRecorderService();

    void setSettings(const AutoRecordSettings& settings);
    AutoRecordSettings getSettings() const;

    void enableAutoRecord(bool enabled);
    bool isAutoRecordEnabled() const { return autoEnabled_.load(); }

    // Contrôle manuel — prime sur l'auto
    void startRecording();
    void stopRecording();
    bool isRecording() const;

    void feedAudio(const float* buffer, int numFrames, int channels);

    void onPlaybackStarted();
    void onPlaybackStopped();
    void onTrackLoaded(int deckIndex, const std::string& trackName, double bpm, const std::string& key);
    void onSilenceDetected();

    LiveSetRecorder* getRecorder() { return recorder_.get(); }
    const LiveSetRecorder* getRecorder() const { return recorder_.get(); }

    double getElapsedSeconds() const;
    std::string getCurrentFilePath() const;
    int getRecordingCount() const { return recordingCount_.load(); }

    void setRecordingCompleteCallback(RecordingCompleteCallback callback);

private:
    std::string generateFilePath() const;
    void checkSilence(const float* buffer, int numFrames, int channels);
    void splitFile();

    std::unique_ptr<LiveSetRecorder> recorder_;

    // Réglages visibles depuis le thread audio (lock-free)
    std::atomic<float> silenceThresholdDb_{-60.0f};
    std::atomic<int>   silenceTimeoutMs_{5000};
    std::atomic<int>   sampleRateAtomic_{44100};
    std::atomic<bool>  autoStopOnSilence_{true};
    std::atomic<bool>  autoStartOnPlay_{true};
    std::atomic<bool>  autoAddMarkers_{true};
    std::atomic<bool>  autoSplit_{false};
    std::atomic<int>   autoSplitSilenceMs_{10000};
    std::atomic<int>   maxFileSizeMB_{0};
    std::atomic<int>   channelsAtomic_{2};

    // Réglages réservés au thread UI (champs string)
    AutoRecordSettings settings_;

    std::atomic<bool> autoEnabled_{false};
    std::atomic<int> recordingCount_{0};
    std::string currentFilePath_;
    RecordingCompleteCallback completeCallback_;

    std::chrono::steady_clock::time_point lastAudioTime_;
    std::atomic<bool> inSilence_{false};
    std::atomic<int> silenceFrameCount_{0};
    std::atomic<int> aboveThresholdFrameCount_{0};

    std::atomic<int64_t> totalBytesWritten_{0};

    // Garde de réentrance pour stopRecording
    std::atomic<bool> stopInFlight_{false};

    mutable std::mutex mutex_;
};

} // namespace BeatMate::Core
