#pragma once
#include <atomic>
namespace BeatMate::Core {
class LoopEngine {
public:
    LoopEngine();
    ~LoopEngine();
    void setLoop(double startSec, double endSec);
    void clearLoop();
    bool isLooping() const { return active_.load(std::memory_order_acquire); }
    // Lock-free snapshots: safe to read from the audio thread.
    double getLoopStart() const { return loopStart_.load(std::memory_order_acquire); }
    double getLoopEnd()   const { return loopEnd_.load(std::memory_order_acquire); }
    double getLoopLength() const {
        const double s = loopStart_.load(std::memory_order_acquire);
        const double e = loopEnd_.load(std::memory_order_acquire);
        return e - s;
    }
    // Loop roll: division in beats (0.5 = 1/2, 0.25 = 1/4, etc.)
    void setLoopRoll(double division, double bpm, double currentPosition);
    void setAutoLoop(int bars, double bpm, double currentPosition);
    void setBeatGrid(double firstBeatOffset);
    double processPosition(double position) const;
private:
    std::atomic<bool>   active_{false};
    // Publication channel UI thread -> audio thread (C++20 atomic<double>).
    std::atomic<double> loopStart_{0.0};
    std::atomic<double> loopEnd_{0.0};
    std::atomic<double> firstBeat_{0.0};
};
} // namespace BeatMate::Core
