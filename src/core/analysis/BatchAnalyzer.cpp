#include "BatchAnalyzer.h"
#include <algorithm>
#include <future>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BatchAnalyzer::BatchAnalyzer(int numThreads)
    : numThreads_(std::max(1, numThreads)) {
}

BatchAnalyzer::~BatchAnalyzer() = default;

std::vector<TrackAnalysis> BatchAnalyzer::analyzeMultiple(
    const std::vector<std::string>& paths,
    BatchProgressCallback progress) {

    cancelled_.store(false);
    int total = static_cast<int>(paths.size());
    std::vector<TrackAnalysis> results(total);
    std::atomic<int> completed{0};
    std::mutex progressMutex;

    spdlog::info("BatchAnalyzer: starting analysis of {} tracks with {} threads",
                 total, numThreads_);

    std::vector<std::future<void>> futures;
    std::atomic<int> nextIndex{0};

    auto worker = [&]() {
        AudioAnalysisPipeline pipeline;
        while (!cancelled_.load()) {
            int idx = nextIndex.fetch_add(1);
            if (idx >= total) break;

            results[idx] = pipeline.analyzeTrack(paths[idx]);

            int done = completed.fetch_add(1) + 1;
            if (progress) {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(done, total, paths[idx]);
            }
        }
    };

    for (int t = 0; t < numThreads_; ++t) {
        futures.push_back(std::async(std::launch::async, worker));
    }

    for (auto& f : futures) {
        f.wait();
    }

    spdlog::info("BatchAnalyzer: completed {} of {} tracks", completed.load(), total);
    return results;
}

} // namespace BeatMate::Core
