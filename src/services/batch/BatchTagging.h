#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../models/Track.h"
namespace BeatMate::Services::Batch {
using TaggingProgressCallback = std::function<void(int processed, int total, const std::string& current)>;
class BatchTagging {
public:
    BatchTagging() = default;
    bool tagAll(const std::vector<Models::Track>& tracks, const std::map<std::string, std::string>& tags, TaggingProgressCallback callback = nullptr);
    void cancel() { cancelled_ = true; }
private:
    std::atomic<bool> cancelled_{false};
};
} // namespace BeatMate::Services::Batch
