#pragma once
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::Preparation {

enum class ValidationSeverity { Info, Warning, Error };

struct ValidationIssue {
    int trackIndex = -1;
    std::string field;
    std::string message;
    ValidationSeverity severity = ValidationSeverity::Warning;
    float value = 0.0f;
};

struct ValidationReport {
    bool valid = true;
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    float overallScore = 0.0f;
    std::vector<ValidationIssue> issues;
    std::string summary;
};

struct ValidationConfig {
    double maxBpmJump = 8.0;
    float maxEnergyJump = 3.0f;
    bool requireCompatibleKeys = true;
    bool requireSameGenre = false;
    double minTrackDuration = 60.0;
    double maxTrackDuration = 600.0;
};

class SetValidationService {
public:
    SetValidationService() = default;

    ValidationReport validate(const std::vector<Models::Track>& tracks, const ValidationConfig& config = {});
    ValidationReport validateBpmFlow(const std::vector<Models::Track>& tracks, double maxBpmJump = 8.0);
    ValidationReport validateKeyCompatibility(const std::vector<Models::Track>& tracks);
    ValidationReport validateEnergyCoherence(const std::vector<Models::Track>& tracks, float maxEnergyJump = 3.0f);
    ValidationReport validateTrackDurations(const std::vector<Models::Track>& tracks, double minDuration = 60.0, double maxDuration = 600.0);

private:
    bool isKeyCompatible(const std::string& key1, const std::string& key2) const;
    void addIssue(ValidationReport& report, int index, const std::string& field,
                  const std::string& message, ValidationSeverity severity, float value = 0.0f);
    void computeSummary(ValidationReport& report, int trackCount);
};

} // namespace BeatMate::Services::Preparation
