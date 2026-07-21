#include "SetValidationService.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <sstream>

namespace BeatMate::Services::Preparation {

ValidationReport SetValidationService::validate(const std::vector<Models::Track>& tracks, const ValidationConfig& config) {
    ValidationReport report;
    report.valid = true;

    if (tracks.empty()) {
        addIssue(report, -1, "set", "Set is empty", ValidationSeverity::Error);
        report.valid = false;
        computeSummary(report, 0);
        return report;
    }

    auto bpmReport = validateBpmFlow(tracks, config.maxBpmJump);
    report.issues.insert(report.issues.end(), bpmReport.issues.begin(), bpmReport.issues.end());

    if (config.requireCompatibleKeys) {
        auto keyReport = validateKeyCompatibility(tracks);
        report.issues.insert(report.issues.end(), keyReport.issues.begin(), keyReport.issues.end());
    }

    auto energyReport = validateEnergyCoherence(tracks, config.maxEnergyJump);
    report.issues.insert(report.issues.end(), energyReport.issues.begin(), energyReport.issues.end());

    auto durReport = validateTrackDurations(tracks, config.minTrackDuration, config.maxTrackDuration);
    report.issues.insert(report.issues.end(), durReport.issues.begin(), durReport.issues.end());

    if (config.requireSameGenre && tracks.size() > 1) {
        std::string firstGenre = tracks[0].genre;
        for (size_t i = 1; i < tracks.size(); ++i) {
            if (!tracks[i].genre.empty() && tracks[i].genre != firstGenre) {
                addIssue(report, static_cast<int>(i), "genre",
                         "Genre mismatch: '" + tracks[i].genre + "' vs '" + firstGenre + "'",
                         ValidationSeverity::Warning);
            }
        }
    }

    computeSummary(report, static_cast<int>(tracks.size()));
    spdlog::info("SetValidationService: Validated {} tracks - score={:.1f}%, errors={}, warnings={}",
                 tracks.size(), report.overallScore * 100.0f, report.errorCount, report.warningCount);
    return report;
}

ValidationReport SetValidationService::validateBpmFlow(const std::vector<Models::Track>& tracks, double maxBpmJump) {
    ValidationReport report;
    for (size_t i = 1; i < tracks.size(); ++i) {
        if (tracks[i].bpm <= 0 || tracks[i - 1].bpm <= 0) continue;
        double diff = std::abs(tracks[i].bpm - tracks[i - 1].bpm);
        if (diff > maxBpmJump) {
            addIssue(report, static_cast<int>(i), "bpm",
                     "BPM jump of " + std::to_string(static_cast<int>(diff)) +
                     " between '" + tracks[i - 1].title + "' and '" + tracks[i].title + "'",
                     diff > maxBpmJump * 2 ? ValidationSeverity::Error : ValidationSeverity::Warning,
                     static_cast<float>(diff));
        }
    }
    return report;
}

ValidationReport SetValidationService::validateKeyCompatibility(const std::vector<Models::Track>& tracks) {
    ValidationReport report;
    for (size_t i = 1; i < tracks.size(); ++i) {
        std::string key1 = tracks[i - 1].camelotKey.empty() ? tracks[i - 1].key : tracks[i - 1].camelotKey;
        std::string key2 = tracks[i].camelotKey.empty() ? tracks[i].key : tracks[i].camelotKey;
        if (key1.empty() || key2.empty()) continue;
        if (!isKeyCompatible(key1, key2)) {
            addIssue(report, static_cast<int>(i), "key",
                     "Incompatible keys: " + key1 + " -> " + key2 +
                     " ('" + tracks[i - 1].title + "' -> '" + tracks[i].title + "')",
                     ValidationSeverity::Warning);
        }
    }
    return report;
}

ValidationReport SetValidationService::validateEnergyCoherence(const std::vector<Models::Track>& tracks, float maxEnergyJump) {
    ValidationReport report;
    for (size_t i = 1; i < tracks.size(); ++i) {
        float diff = std::abs(tracks[i].energy - tracks[i - 1].energy);
        if (diff > maxEnergyJump) {
            addIssue(report, static_cast<int>(i), "energy",
                     "Energy jump of " + std::to_string(static_cast<int>(diff)) +
                     " between '" + tracks[i - 1].title + "' and '" + tracks[i].title + "'",
                     diff > maxEnergyJump * 2 ? ValidationSeverity::Error : ValidationSeverity::Warning,
                     diff);
        }
    }
    return report;
}

ValidationReport SetValidationService::validateTrackDurations(
    const std::vector<Models::Track>& tracks, double minDuration, double maxDuration) {
    ValidationReport report;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].duration > 0 && tracks[i].duration < minDuration) {
            addIssue(report, static_cast<int>(i), "duration",
                     "Track '" + tracks[i].title + "' is very short (" +
                     std::to_string(static_cast<int>(tracks[i].duration)) + "s)",
                     ValidationSeverity::Info, static_cast<float>(tracks[i].duration));
        }
        if (tracks[i].duration > maxDuration) {
            addIssue(report, static_cast<int>(i), "duration",
                     "Track '" + tracks[i].title + "' is very long (" +
                     std::to_string(static_cast<int>(tracks[i].duration / 60.0)) + " min)",
                     ValidationSeverity::Info, static_cast<float>(tracks[i].duration));
        }
    }
    return report;
}

bool SetValidationService::isKeyCompatible(const std::string& key1, const std::string& key2) const {
    if (key1 == key2) return true;
    if (key1.size() < 2 || key2.size() < 2) return false;
    try {
        int num1 = std::stoi(key1.substr(0, key1.size() - 1));
        int num2 = std::stoi(key2.substr(0, key2.size() - 1));
        char let1 = key1.back();
        char let2 = key2.back();
        if (num1 == num2 && let1 != let2) return true;
        int diff = ((num2 - num1) + 12) % 12;
        if ((diff == 1 || diff == 11) && let1 == let2) return true;
    } catch (...) {}
    return false;
}

void SetValidationService::addIssue(ValidationReport& report, int index, const std::string& field,
                                     const std::string& message, ValidationSeverity severity, float value) {
    ValidationIssue issue;
    issue.trackIndex = index;
    issue.field = field;
    issue.message = message;
    issue.severity = severity;
    issue.value = value;
    report.issues.push_back(issue);

    switch (severity) {
        case ValidationSeverity::Error: ++report.errorCount; report.valid = false; break;
        case ValidationSeverity::Warning: ++report.warningCount; break;
        case ValidationSeverity::Info: ++report.infoCount; break;
    }
}

void SetValidationService::computeSummary(ValidationReport& report, int trackCount) {
    int totalTransitions = std::max(1, trackCount - 1);
    int problemTransitions = report.errorCount + report.warningCount;
    report.overallScore = std::max(0.0f, 1.0f - static_cast<float>(problemTransitions) / static_cast<float>(totalTransitions));

    std::ostringstream ss;
    ss << trackCount << " tracks validated: ";
    if (report.errorCount == 0 && report.warningCount == 0) {
        ss << "All transitions are compatible.";
    } else {
        ss << report.errorCount << " errors, " << report.warningCount << " warnings, " << report.infoCount << " infos.";
    }
    report.summary = ss.str();
}

} // namespace BeatMate::Services::Preparation
