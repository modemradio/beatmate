#pragma once
#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ClapEncoder.h"

namespace BeatMate::Services::Library { class TrackDatabase; }

namespace BeatMate::Services::AI {

class ClapEmbedQueue {
public:
    using EmbeddingMap = std::unordered_map<int64_t, std::vector<float>>;
    static constexpr float kGenreTagMinScore = 0.20f;

    explicit ClapEmbedQueue(std::shared_ptr<Library::TrackDatabase> db);
    ~ClapEmbedQueue();

    void start();
    void stop();

    std::shared_ptr<const EmbeddingMap> snapshot() const;
    void prioritizeTrack(int64_t trackId);
    void prioritizeTracks(const std::vector<int64_t>& trackIds);
    void rescanLibrary();
    void setOnPublish(std::function<void(int done, int total)> cb);

private:
    struct Worker : juce::Thread {
        ClapEmbedQueue& owner;
        Worker(ClapEmbedQueue& o, const juce::String& name) : juce::Thread(name), owner(o) {}
        void run() override { owner.workerLoop(*this); }
    };

    void workerLoop(juce::Thread& thread);
    void bootstrap();
    bool processOne(int64_t trackId);
    void publish(bool force);
    bool enqueueUnlocked(int64_t trackId, bool high);

    std::shared_ptr<Library::TrackDatabase> db_;
    ClapEncoder encoder_;
    std::unique_ptr<ClapEncoder> tagEncoder_;

    mutable std::mutex mapMutex_;
    EmbeddingMap master_;
    std::shared_ptr<const EmbeddingMap> published_;
    int pendingSincePublish_ = 0;
    double lastPublishMs_ = 0.0;

    std::mutex queueMutex_;
    std::condition_variable cv_;
    std::deque<int64_t> high_;
    std::deque<int64_t> normal_;
    std::unordered_set<int64_t> queued_;
    std::unordered_set<int64_t> failed_;
    bool stopping_ = false;
    bool bootstrapDone_ = false;
    int total_ = 0;

    std::function<void(int, int)> onPublish_;
    std::mutex cbMutex_;

    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> started_{false};
};

} // namespace BeatMate::Services::AI
