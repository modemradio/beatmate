#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <juce_core/juce_core.h>
#include "../../core/analysis/RgbPeaksGenerator.h"

namespace BeatMate::Services::Library {

class TrackDataProvider;

class WaveformPrecacheService : public juce::Thread {
public:
    using ProgressCallback = std::function<void(const Core::RgbPeaksData&)>;
    using DoneCallback = std::function<void(const Core::RgbPeaksData&)>;

    explicit WaveformPrecacheService(TrackDataProvider* provider);
    ~WaveformPrecacheService() override;

    void requestPriority(const std::string& audioPath,
                         ProgressCallback onProgress,
                         DoneCallback onDone);
    void requestScan();
    int pendingCount();

    void run() override;

private:
    struct Job {
        std::string path;
        bool priority = false;
        uint64_t seq = 0;
        ProgressCallback onProgress;
        DoneCallback onDone;
    };

    enum class JobOutcome { Skipped, Generated, Preempted };

    bool popJob(Job& out);
    JobOutcome processJob(const Job& job);
    bool hasPriorityJob();
    void requeueAfterPriorities(Job&& job);
    void enqueueScan();
    void loadQuarantine();
    void addToQuarantine(const std::string& path);
    void enforceDiskCap();

    TrackDataProvider* m_provider = nullptr;
    std::mutex m_mutex;
    std::deque<Job> m_queue;
    std::set<std::string> m_quarantine;
    std::string m_inflightPath;
    bool m_inflightPriority = false;
    std::atomic<uint64_t> m_prioritySeq{0};
    std::atomic<bool> m_rescanPending{false};
    std::atomic<uint32_t> m_rescanAtMs{0};
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformPrecacheService)
};

} // namespace BeatMate::Services::Library
