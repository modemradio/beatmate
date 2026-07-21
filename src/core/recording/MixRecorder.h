#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
namespace BeatMate::Core {
class AudioFileWriter;
class MixRecorder {
public:
    MixRecorder();
    ~MixRecorder();
    bool startRecording(const std::string& outputPath, const std::string& format = "wav",
                        int sampleRate = 44100, int channels = 2);
    std::string stopRecording();
    void feedAudio(const float* buffer, int numFrames, int channels);
    bool isRecording() const { return recording_.load(); }
    double getElapsedSeconds() const;
    int64_t getRecordedFrames() const { return recordedFrames_.load(); }

private:
    // Drain the FIFO into the long-lived recordBuffer_ (called on the
    void drainFifoToBuffer();

    std::atomic<bool> recording_{false};
    std::string outputPath_;
    int sampleRate_ = 44100;
    int channels_ = 2;

    // Long-lived buffer (only accessed by stop/drain thread).
    std::vector<float> recordBuffer_;
    std::atomic<int64_t> recordedFrames_{0};
    std::chrono::steady_clock::time_point startTime_;

    // Lock-free SPSC FIFO (audio thread = producer, drain thread = consumer).
    static constexpr int kFifoCapacityFloats = 96000 * 2 * 30;
    std::unique_ptr<juce::AbstractFifo> fifo_;
    std::vector<float> fifoStorage_;

    // Drop counter (when audio thread cannot push because FIFO is full).
    std::atomic<int64_t> droppedSamples_{0};
};
} // namespace BeatMate::Core
