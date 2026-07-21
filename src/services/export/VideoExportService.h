#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>

#include "../../models/Track.h"

namespace BeatMate::Services::Export {

class VideoExportService {
public:
    struct Options {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int videoBitrateKbps = 10000;
        int audioBitrateKbps = 320;
    };

    using ProgressCallback = std::function<bool(float pct, const std::string& phase)>;

    bool exportVideo(const std::string& sourceWav,
                     const std::vector<Models::Track>& tracks,
                     const std::vector<double>& trackStartTimes,
                     const std::string& outputMp4,
                     const Options& opts = {},
                     ProgressCallback progress = nullptr);

    void cancel() { cancelRequested_.store(true, std::memory_order_relaxed); }

    static std::string findFfmpeg();

private:
    std::atomic<bool> cancelRequested_{false};
};

}
