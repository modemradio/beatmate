#include "ChartScheduler.h"
#include "BillboardService.h"
#include "BeatportService.h"

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Streaming {

ChartScheduler::ChartScheduler() {
}

ChartScheduler::~ChartScheduler() {
    stop();
}

void ChartScheduler::scheduleUpdate(int intervalMinutes) {
    startTimer(intervalMinutes * 60 * 1000);
    running_ = true;
    spdlog::info("ChartScheduler: Scheduled updates every {} minutes", intervalMinutes);

    updateCharts();
}

void ChartScheduler::stop() {
    stopTimer();
    running_ = false;
    spdlog::info("ChartScheduler: Stopped");
}

void ChartScheduler::updateCharts() {
    spdlog::info("ChartScheduler: Updating charts");

    if (billboard_) {
        auto hot100 = billboard_->getHot100();
        bool success = !hot100.empty();
        if (callback_) callback_("hot-100", success);
        spdlog::info("ChartScheduler: Billboard Hot 100: {} entries", hot100.size());
    }

    if (beatport_) {
        auto topTracks = beatport_->getTopCharts();
        bool success = !topTracks.tracks.empty();
        if (callback_) callback_("beatport-top", success);
        spdlog::info("ChartScheduler: Beatport Top: {} entries", topTracks.tracks.size());
    }

    if (chartsUpdatedCallback_) chartsUpdatedCallback_();
}

bool ChartScheduler::isRunning() const {
    return running_;
}

void ChartScheduler::timerCallback() {
    updateCharts();
}

} // namespace BeatMate::Services::Streaming
