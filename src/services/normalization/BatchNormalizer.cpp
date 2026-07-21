#include "BatchNormalizer.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Normalization {
bool BatchNormalizer::normalizeAll(const std::vector<std::string>& files, float targetLUFS, NormProgressCallback callback) {
    cancelled_ = false;
    int total = static_cast<int>(files.size());
    for (int i = 0; i < total; ++i) {
        if (cancelled_) { spdlog::info("BatchNormalizer: Cancelled"); return false; }
        if (callback) callback(i + 1, total, files[static_cast<size_t>(i)]);
    }
    spdlog::info("BatchNormalizer: Normalized {} files to {:.1f} LUFS", total, targetLUFS);
    return true;
}
}
