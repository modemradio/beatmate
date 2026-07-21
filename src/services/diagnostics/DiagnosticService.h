#pragma once
#include <string>
#include <vector>
namespace BeatMate::Services::Diagnostics {
struct DiagnosticItem { std::string name; bool passed = false; std::string details; };
struct DiagnosticReport { std::vector<DiagnosticItem> items; bool allPassed = false; std::string summary; };
class DiagnosticService {
public:
    DiagnosticService() = default;
    DiagnosticReport runFullDiagnostic();
    DiagnosticItem checkAudio(); DiagnosticItem checkDatabase(); DiagnosticItem checkNetwork();
    DiagnosticItem checkDiskSpace(); DiagnosticItem checkPermissions();
};
} // namespace BeatMate::Services::Diagnostics
