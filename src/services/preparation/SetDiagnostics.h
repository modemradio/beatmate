#pragma once
#include <string>
#include <vector>
#include "SetPreparation.h"

namespace BeatMate::Services::Preparation {
struct DiagnosticWarning { int trackIndex; std::string message; std::string severity; /* info, warning, error */ };
struct DiagnosticReport { int totalTracks = 0; int warningCount = 0; float averageCompatibility = 0.0f; std::vector<DiagnosticWarning> warnings; };

class SetDiagnostics {
public:
    SetDiagnostics() = default;
    DiagnosticReport diagnose(const SetPlan& plan);
};
} // namespace BeatMate::Services::Preparation
