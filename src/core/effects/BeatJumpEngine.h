#pragma once
#include <atomic>
namespace BeatMate::Core {
class BeatJumpEngine {
public:
    BeatJumpEngine() = default;
    double jump(double currentPosition, int beats, double bpm);
    void setBeatGrid(double firstBeatOffset, double bpm);
    void setMaxDuration(double seconds);
    double quantizeToGrid(double position) const;
    bool isGridValid() const { return gridValid_.load(std::memory_order_acquire); }
private:
    // setBeatGrid runs on the UI thread, jump()/quantizeToGrid on the audio thread : atomics required.
    std::atomic<double> firstBeat_{0.0};
    std::atomic<double> bpm_{128.0};
    // Upper clamp for jump() ; 0 means "unknown" (no clamp on the high side).
    std::atomic<double> maxDuration_{0.0};
    std::atomic<bool>   gridValid_{false};
};
} // namespace BeatMate::Core
