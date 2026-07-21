#pragma once

#include "AudioAnalysisPipeline.h"
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace BeatMate::Core {

using BatchProgressCallback = std::function<void(int completed, int total,
                                                   const std::string& currentFile)>;

class BatchAnalyzer {
public:
    explicit BatchAnalyzer(int numThreads = 4);
    ~BatchAnalyzer();

    std::vector<TrackAnalysis> analyzeMultiple(
        const std::vector<std::string>& paths,
        BatchProgressCallback progress = nullptr);

    void cancel() { cancelled_.store(true); }
    bool isCancelled() const { return cancelled_.load(); }

private:
    int numThreads_;
    std::atomic<bool> cancelled_{false};
};

}
