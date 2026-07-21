#include "MixRecorder.h"
#include "../audio/AudioFileWriter.h"
#include "../audio/AudioTrack.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

MixRecorder::MixRecorder()
    : fifo_(std::make_unique<juce::AbstractFifo>(kFifoCapacityFloats))
{
    fifoStorage_.assign(kFifoCapacityFloats, 0.0f);
}

MixRecorder::~MixRecorder() { if (recording_.load()) stopRecording(); }

bool MixRecorder::startRecording(const std::string& outputPath, const std::string& format,
                                  int sampleRate, int channels) {
    if (recording_.load()) return false;
    outputPath_ = outputPath;
    sampleRate_ = sampleRate;
    channels_ = channels;

    recordBuffer_.clear();
    recordBuffer_.reserve(static_cast<size_t>(sampleRate) * channels * 300);

    fifo_->reset();
    droppedSamples_.store(0);
    recordedFrames_.store(0);
    startTime_ = std::chrono::steady_clock::now();
    recording_.store(true);
    spdlog::info("MixRecorder: started recording to {}", outputPath);
    return true;
}

std::string MixRecorder::stopRecording() {
    if (!recording_.load()) return "";
    recording_.store(false);

    drainFifoToBuffer();

    AudioTrack track;
    track.loadData(std::move(recordBuffer_), sampleRate_, channels_);

    AudioFileWriter writer;
    writer.writeFile(track, outputPath_);

    const int64_t dropped = droppedSamples_.load();
    if (dropped > 0) {
        spdlog::warn("MixRecorder: dropped {} samples (FIFO full)", dropped);
    }
    spdlog::info("MixRecorder: stopped recording ({:.1f}s)", track.getDuration());
    return outputPath_;
}

void MixRecorder::feedAudio(const float* buffer, int numFrames, int channels) {
    if (!recording_.load()) return;
    const int totalFloats = numFrames * channels;
    if (totalFloats <= 0 || buffer == nullptr) return;

    // Lock-free push ; if FIFO full we drop, never block the audio thread
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo_->prepareToWrite(totalFloats, start1, size1, start2, size2);
    const int written = size1 + size2;

    if (size1 > 0) {
        std::memcpy(fifoStorage_.data() + start1, buffer, size1 * sizeof(float));
    }
    if (size2 > 0) {
        std::memcpy(fifoStorage_.data() + start2, buffer + size1, size2 * sizeof(float));
    }
    fifo_->finishedWrite(written);

    if (written < totalFloats) {
        droppedSamples_.fetch_add(totalFloats - written, std::memory_order_relaxed);
    }
    recordedFrames_.fetch_add(written / std::max(1, channels));
}

void MixRecorder::drainFifoToBuffer() {
    int available = fifo_->getNumReady();
    while (available > 0) {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo_->prepareToRead(available, start1, size1, start2, size2);
        if (size1 > 0) {
            recordBuffer_.insert(recordBuffer_.end(),
                                 fifoStorage_.data() + start1,
                                 fifoStorage_.data() + start1 + size1);
        }
        if (size2 > 0) {
            recordBuffer_.insert(recordBuffer_.end(),
                                 fifoStorage_.data() + start2,
                                 fifoStorage_.data() + start2 + size2);
        }
        fifo_->finishedRead(size1 + size2);
        available = fifo_->getNumReady();
    }
}

double MixRecorder::getElapsedSeconds() const {
    if (!recording_.load()) return 0.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - startTime_).count();
}

}
