#include "AutoMixRecorderService.h"
#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>
#include <juce_events/juce_events.h>

namespace BeatMate::Core {

AutoMixRecorderService::AutoMixRecorderService()
    : recorder_(std::make_unique<LiveSetRecorder>())
{
    lastAudioTime_ = std::chrono::steady_clock::now();
}

AutoMixRecorderService::~AutoMixRecorderService() {
    if (isRecording()) stopRecording();
}

void AutoMixRecorderService::setSettings(const AutoRecordSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    // Mirror hot-path fields into atomics so the audio thread can read lock-free.
    silenceThresholdDb_.store(settings.silenceThresholdDb, std::memory_order_relaxed);
    silenceTimeoutMs_.store(settings.silenceTimeoutMs, std::memory_order_relaxed);
    sampleRateAtomic_.store(settings.sampleRate, std::memory_order_relaxed);
    channelsAtomic_.store(settings.channels, std::memory_order_relaxed);
    autoStopOnSilence_.store(settings.autoStopOnSilence, std::memory_order_relaxed);
    autoStartOnPlay_.store(settings.autoStartOnPlay, std::memory_order_relaxed);
    autoAddMarkers_.store(settings.autoAddMarkers, std::memory_order_relaxed);
    autoSplit_.store(settings.autoSplit, std::memory_order_relaxed);
    autoSplitSilenceMs_.store(settings.autoSplitSilenceMs, std::memory_order_relaxed);
    maxFileSizeMB_.store(settings.maxFileSizeMB, std::memory_order_relaxed);
}

AutoRecordSettings AutoMixRecorderService::getSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void AutoMixRecorderService::enableAutoRecord(bool enabled) {
    autoEnabled_.store(enabled);
    spdlog::info("AutoMixRecorderService: auto-record {}", enabled ? "enabled" : "disabled");
}

void AutoMixRecorderService::startRecording() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recorder_->isRecording()) return;

    currentFilePath_ = generateFilePath();
    if (recorder_->startRecording(currentFilePath_, settings_.fileFormat,
                                   settings_.sampleRate, settings_.channels)) {
        recordingCount_.fetch_add(1);
        silenceFrameCount_.store(0);
        aboveThresholdFrameCount_.store(0);
        inSilence_.store(false);
        totalBytesWritten_.store(0);
        spdlog::info("AutoMixRecorderService: recording started -> '{}'", currentFilePath_);
    } else {
        spdlog::error("AutoMixRecorderService: failed to start recording");
    }
}

void AutoMixRecorderService::stopRecording() {
    // Re-entrancy guard: the silence callback can fire concurrently.
    bool expected = false;
    if (!stopInFlight_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::string filePath;
    double duration = 0.0;
    RecordingCompleteCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recorder_->isRecording()) {
            stopInFlight_.store(false);
            return;
        }
        duration = recorder_->getElapsedSeconds();
        filePath = recorder_->stopRecording();
        cb = completeCallback_;
    }
    spdlog::info("AutoMixRecorderService: recording stopped ({:.1f}s) -> '{}'", duration, filePath);
    if (cb) cb(filePath, duration);
    stopInFlight_.store(false);
}

bool AutoMixRecorderService::isRecording() const {
    return recorder_->isRecording();
}

void AutoMixRecorderService::feedAudio(const float* buffer, int numFrames, int channels) {
    if (!recorder_->isRecording()) return;
    recorder_->feedAudio(buffer, numFrames, channels);

    // Lock-free atomic reads only on the audio path.
    if (autoStopOnSilence_.load(std::memory_order_relaxed)) {
        checkSilence(buffer, numFrames, channels);
    }

    const int64_t bytesThisCall =
        static_cast<int64_t>(numFrames) * static_cast<int64_t>(channels) * sizeof(float);
    const int64_t total = totalBytesWritten_.fetch_add(bytesThisCall, std::memory_order_relaxed) + bytesThisCall;
    const int maxMB = maxFileSizeMB_.load(std::memory_order_relaxed);
    if (maxMB > 0 && total > static_cast<int64_t>(maxMB) * 1024 * 1024) {
        // Defer split to message thread (file IO not safe on audio thread).
        juce::MessageManager::callAsync([this]{ splitFile(); });
    }
}

void AutoMixRecorderService::onPlaybackStarted() {
    if (autoEnabled_.load() && autoStartOnPlay_.load() && !isRecording()) {
        startRecording();
    }
}

void AutoMixRecorderService::onPlaybackStopped() {
}

void AutoMixRecorderService::onTrackLoaded(int deckIndex, const std::string& trackName,
                                            double bpm, const std::string& key) {
    if (isRecording() && autoAddMarkers_.load()) {
        recorder_->onTrackLoaded(deckIndex, trackName, bpm, key);
    }
}

void AutoMixRecorderService::onSilenceDetected() {
    // Never call stopRecording() inline here: checkSilence() runs on the audio thread.
    if (autoEnabled_.load() && autoStopOnSilence_.load() && isRecording()) {
        spdlog::info("AutoMixRecorderService: silence detected, scheduling stop");
        juce::MessageManager::callAsync([this]{ stopRecording(); });
    }
}

double AutoMixRecorderService::getElapsedSeconds() const {
    return recorder_->getElapsedSeconds();
}

std::string AutoMixRecorderService::getCurrentFilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentFilePath_;
}

void AutoMixRecorderService::setRecordingCompleteCallback(RecordingCompleteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    completeCallback_ = std::move(callback);
}

std::string AutoMixRecorderService::generateFilePath() const {
    std::string dir = settings_.outputDirectory;
    if (dir.empty()) dir = ".";

    std::string filename = "BeatMate_Mix";
    if (settings_.addTimestampToFilename) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        // Thread-safe local time on Windows.
        std::tm buf{};
#if defined(_WIN32)
        localtime_s(&buf, &t);
#else
        localtime_r(&t, &buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&buf, "_%Y%m%d_%H%M%S");
        filename += oss.str();
    }
    filename += "_" + std::to_string(recordingCount_.load() + 1);
    filename += "." + settings_.fileFormat;

    return dir + "/" + filename;
}

void AutoMixRecorderService::splitFile() {
    spdlog::info("AutoMixRecorderService: auto-split (max size or long silence reached)");
    stopRecording();
    startRecording();
}

void AutoMixRecorderService::checkSilence(const float* buffer, int numFrames, int channels) {
    // Calculate RMS level (atomic-only reads, no mutex on audio thread).
    float rms = 0.0f;
    const int totalSamples = numFrames * channels;
    for (int i = 0; i < totalSamples; ++i) {
        rms += buffer[i] * buffer[i];
    }
    rms = std::sqrt(rms / std::max(1, totalSamples));

    const float db = (rms > 0.0f) ? 20.0f * std::log10(rms) : -100.0f;
    const float threshDb = silenceThresholdDb_.load(std::memory_order_relaxed);
    const int   timeoutMs = silenceTimeoutMs_.load(std::memory_order_relaxed);
    const int   splitMs = autoSplitSilenceMs_.load(std::memory_order_relaxed);
    const int   sr = std::max(1, sampleRateAtomic_.load(std::memory_order_relaxed));

    if (db < threshDb) {
        const int sf = silenceFrameCount_.fetch_add(numFrames, std::memory_order_relaxed) + numFrames;
        aboveThresholdFrameCount_.store(0, std::memory_order_relaxed);
        const int silenceMs = static_cast<int>((static_cast<int64_t>(sf) * 1000) / sr);
        if (silenceMs >= timeoutMs && !inSilence_.load(std::memory_order_relaxed)) {
            inSilence_.store(true, std::memory_order_relaxed);
            onSilenceDetected();
        }
        if (autoSplit_.load(std::memory_order_relaxed) && silenceMs >= splitMs) {
            silenceFrameCount_.store(0, std::memory_order_relaxed);
            juce::MessageManager::callAsync([this]{ splitFile(); });
        }
    } else {
        const int af = aboveThresholdFrameCount_.fetch_add(numFrames, std::memory_order_relaxed) + numFrames;
        const int aboveMs = static_cast<int>((static_cast<int64_t>(af) * 1000) / sr);
        if (aboveMs >= 100) {
            silenceFrameCount_.store(0, std::memory_order_relaxed);
            inSilence_.store(false, std::memory_order_relaxed);
        }
        lastAudioTime_ = std::chrono::steady_clock::now();
    }
}

} // namespace BeatMate::Core
