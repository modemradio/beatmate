#include "BatchAnalysis.h"
#include "../../core/analysis/AudioAnalysisPipeline.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Batch {
bool BatchAnalysis::analyzeAll(const std::vector<Models::Track>& tracks, AnalysisProgressCallback callback) {
    cancelled_ = false;
    int total = static_cast<int>(tracks.size());
    int errors = 0;
    for (int i = 0; i < total; ++i) {
        if (cancelled_) { spdlog::info("BatchAnalysis: Cancelled"); return false; }
        if (!analyzeTrack(tracks[static_cast<size_t>(i)])) errors++;
        if (callback) callback(i + 1, total, tracks[static_cast<size_t>(i)].title);
    }
    spdlog::info("BatchAnalysis: Analyzed {} tracks ({} errors)", total, errors);
    return errors == 0;
}
bool BatchAnalysis::analyzeTrack(const Models::Track& track) {
    if (track.filePath.empty()) return false;
    Core::AudioAnalysisPipeline pipeline;
    auto result = pipeline.analyzeTrack(track.filePath);
    spdlog::debug("BatchAnalysis: Analyzed '{}' BPM={:.1f} Key='{}'", track.title, result.bpm.bpm, result.key.key);
    return result.bpm.bpm > 0.0;
}
}
