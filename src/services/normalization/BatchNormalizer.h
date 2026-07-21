#pragma once
#include <vector>
#include <string>
#include <functional>
#include <atomic>
namespace BeatMate::Services::Normalization {
using NormProgressCallback = std::function<void(int processed, int total, const std::string& current)>;
class BatchNormalizer {
public:
    BatchNormalizer() = default;
    bool normalizeAll(const std::vector<std::string>& files, float targetLUFS = -14.0f, NormProgressCallback callback = nullptr);
    void cancel() { cancelled_ = true; }
private:
    std::atomic<bool> cancelled_{false};
};
} // namespace BeatMate::Services::Normalization
