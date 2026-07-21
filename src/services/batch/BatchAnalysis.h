#pragma once
#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include "../../models/Track.h"
namespace BeatMate::Services::Batch {
using AnalysisProgressCallback = std::function<void(int processed, int total, const std::string& current)>;
class BatchAnalysis {
public:
    BatchAnalysis() = default;
    bool analyzeAll(const std::vector<Models::Track>& tracks, AnalysisProgressCallback callback = nullptr);
    void cancel() { cancelled_ = true; }
private:
    bool analyzeTrack(const Models::Track& track);
    std::atomic<bool> cancelled_{false};
};
} // namespace BeatMate::Services::Batch
