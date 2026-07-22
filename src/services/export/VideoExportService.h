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
        int width;
        int height;
        int fps;
        int videoBitrateKbps;
        int audioBitrateKbps;
        Options()
            : width(1920), height(1080), fps(30),
              videoBitrateKbps(10000), audioBitrateKbps(320) {}
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
