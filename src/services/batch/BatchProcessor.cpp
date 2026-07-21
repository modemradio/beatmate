#include <algorithm>
#include "BatchProcessor.h"
#include <spdlog/spdlog.h>
#include <thread>
namespace BeatMate::Services::Batch {
BatchProcessor::BatchProcessor(int threadCount) : threadCount_(threadCount) {}
void BatchProcessor::addJob(const BatchJob& job) { jobs_.push_back(job); }
bool BatchProcessor::processAll(BatchProgressCallback callback) {
    cancelled_ = false;
    int total = static_cast<int>(jobs_.size());
    std::atomic<int> completed{0};
    std::atomic<int> failures{0};

    std::vector<std::future<void>> futures;
    size_t idx = 0;
    while (idx < jobs_.size()) {
        int batch = std::min(threadCount_, static_cast<int>(jobs_.size() - idx));
        for (int t = 0; t < batch && idx < jobs_.size(); ++t, ++idx) {
            if (cancelled_) break;
            size_t i = idx;
            futures.push_back(std::async(std::launch::async, [this, i, &completed, &failures, total, &callback]() {
                if (cancelled_) return;
                bool ok = jobs_[i].task();
                if (!ok) failures++;
                int done = ++completed;
                if (callback) callback(done, total, jobs_[i].description);
            }));
        }
        for (auto& f : futures) f.wait();
        futures.clear();
        if (cancelled_) break;
    }

    spdlog::info("BatchProcessor: Completed {}/{} jobs ({} failures)", completed.load(), total, failures.load());
    jobs_.clear();
    return failures == 0;
}
} // namespace BeatMate::Services::Batch
