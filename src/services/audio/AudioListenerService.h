#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::Services::Audio {

class AudioListenerService {
public:
    struct Detection {
        int64_t     trackId = 0;     // BeatMate track id (0 si inconnu)
        std::string title;
        std::string artist;
        double      confidence = 0.0; // 0..1
        std::chrono::system_clock::time_point at{};
    };

    using DetectionCallback = std::function<void(const Detection&)>;

    AudioListenerService();
    ~AudioListenerService();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    void setProvider(BeatMate::Services::Library::TrackDataProvider* provider);

    void setOnDetection(DetectionCallback cb);

    struct DJHint {
        double      bpm = 0.0;
        std::string key;
        std::string title;
        std::string artist;
    };
    void setDJHint(const DJHint& hint);
    void clearDJHint();

    struct Status {
        bool        deviceOpen   = false;
        bool        indexReady   = false;
        int         tracksIndexed = 0;
        int         tracksTotal  = 0;
        std::string lastError;
        std::string lastMatch;
    };
    Status getStatus() const;

private:
    void captureLoop();

    void buildIndexLoop();

    std::vector<uint32_t> computeChromaHashes(const std::vector<float>& mono,
                                               int sampleRate) const;

    std::pair<int64_t, double> matchAgainstIndex(
        const std::vector<uint32_t>& liveHashes) const;

    std::atomic<bool> running_ { false };
    std::atomic<bool> indexRunning_ { false };
    std::thread captureThread_;
    std::thread indexThread_;

    mutable std::mutex ringMutex_;
    std::vector<float> ring_;        // capacity = ringSeconds_ * fsHash_
    size_t             ringWritePos_ = 0;
    bool               ringFilled_ = false;
    static constexpr int fsHash_      = 11025;
    static constexpr int ringSeconds_ = 10;
    static constexpr int fftSize_     = 4096;
    static constexpr int hopSize_     = 2048;

    mutable std::mutex indexMutex_;
    std::unordered_map<uint32_t, std::vector<int64_t>> index_;
    std::unordered_map<int64_t, int> indexTrackHashCount_;

    int64_t  lastDetected_ = 0;
    int      lastDetectedHits_ = 0;
    double   lastConfidenceEma_ = 0.0;

    mutable std::mutex hintMutex_;
    bool  hintValid_ = false;
    DJHint hint_{};

    BeatMate::Services::Library::TrackDataProvider* provider_ = nullptr;
    DetectionCallback onDetection_;
    mutable std::mutex detectionCbMutex_;

    mutable std::mutex providerMutex_;
    std::condition_variable providerCv_;

    mutable std::mutex statusMutex_;
    Status status_;
};

} // namespace BeatMate::Services::Audio
