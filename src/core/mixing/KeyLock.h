#pragma once
#include <atomic>
#include <string>
namespace BeatMate::Core {
class KeyLock {
public:
    KeyLock() = default;
    void lock(const std::string& originalKey);
    void unlock();
    bool isLocked() const { return locked_.load(); }
    std::string getLockedKey() const { return lockedKey_; }
    double getPitchCorrection(double tempoRatio) const;
private:
    std::atomic<bool> locked_{false};
    std::string lockedKey_;
};
} // namespace BeatMate::Core
