#include "SetDiagnostics.h"
#include "../suggestions/TrackCompatibility.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace BeatMate::Services::Preparation {

DiagnosticReport SetDiagnostics::diagnose(const SetPlan& plan) {
    DiagnosticReport report;
    report.totalTracks = static_cast<int>(plan.tracks.size());
    float totalCompat = 0.0f;

    for (size_t i = 0; i + 1 < plan.tracks.size(); ++i) {
        auto score = Suggestions::TrackCompatibility::calculateScore(plan.tracks[i], plan.tracks[i + 1]);
        totalCompat += score.overall;

        if (score.bpm < 0.3f) {
            report.warnings.push_back({static_cast<int>(i), "Large BPM difference: " +
                std::to_string(static_cast<int>(plan.tracks[i].bpm)) + " -> " +
                std::to_string(static_cast<int>(plan.tracks[i+1].bpm)), "warning"});
        }
        if (score.key < 0.1f && !plan.tracks[i].key.empty() && !plan.tracks[i+1].key.empty()) {
            report.warnings.push_back({static_cast<int>(i), "Incompatible keys: " +
                plan.tracks[i].camelotKey + " -> " + plan.tracks[i+1].camelotKey, "warning"});
        }
        if (std::abs(plan.tracks[i].energy - plan.tracks[i+1].energy) > 4.0f) {
            report.warnings.push_back({static_cast<int>(i), "Large energy jump", "info"});
        }
    }

    report.warningCount = static_cast<int>(report.warnings.size());
    report.averageCompatibility = plan.tracks.size() > 1 ? totalCompat / (plan.tracks.size() - 1) : 1.0f;

    spdlog::info("SetDiagnostics: {} tracks, {} warnings, avg compat: {:.1f}%",
                 report.totalTracks, report.warningCount, report.averageCompatibility * 100);
    return report;
}

} // namespace BeatMate::Services::Preparation
