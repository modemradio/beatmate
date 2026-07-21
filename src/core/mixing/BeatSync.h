#pragma once
namespace BeatMate::Core {
struct SyncResult { double tempoAdjustment; double phaseOffset; bool needsAdjustment; };
class BeatSync {
public:
    BeatSync() = default;
    SyncResult sync(double bpmA, double bpmB, double phaseA = 0, double phaseB = 0);
    double calculateTempoRatio(double bpmMaster, double bpmSlave);
    double calculatePhaseOffset(double posA, double posB, double bpm);
};
} // namespace BeatMate::Core
