#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <future>
namespace BeatMate::Services::Batch {
struct BatchJob { int id = 0; std::string description; std::function<bool()> task; };
using BatchProgressCallback = std::function<void(int completed, int total, const std::string& current)>;
class BatchProcessor {
public:
    BatchProcessor(int threadCount = 4);
    ~BatchProcessor() = default;
    void addJob(const BatchJob& job);
    bool processAll(BatchProgressCallback callback = nullptr);
    void cancel() { cancelled_ = true; }
    int getJobCount() const { return static_cast<int>(jobs_.size()); }
private:
    std::vector<BatchJob> jobs_;
    int threadCount_;
    std::atomic<bool> cancelled_{false};
};
} // namespace BeatMate::Services::Batch
