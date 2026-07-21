#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Core {

struct TimecodeState {
    double absoluteSeconds = 0.0;       // Total elapsed time
    double bpm = 0.0;                   // Current BPM
    int bar = 0;                        // Current bar number
    int beat = 0;                       // Current beat within bar (0-3)
    int phrase = 0;                     // Current 8-bar phrase
    double beatPhase = 0.0;             // Phase within beat [0..1)
    bool running = false;
    std::string timecodeString;         // HH:MM:SS.mmm
    std::string barBeatString;          // "Bar:Beat"
};

class SetTimecodeService {
public:
    using TimecodeCallback = std::function<void(const TimecodeState&)>;

    SetTimecodeService();
    ~SetTimecodeService() = default;

    void start();
    void stop();
    void reset();
    bool isRunning() const { return running_.load(); }

    void setBpm(double bpm);
    double getBpm() const { return bpm_.load(); }

    void setBeatsPerBar(int beats) { beatsPerBar_ = beats; }
    void setBarsPerPhrase(int bars) { barsPerPhrase_ = bars; }

    void setPosition(double seconds);
    void nudge(double offsetSeconds);

    // Appelable depuis le callback audio ou un timer.
    void advance(double seconds);
    void advanceSamples(int numSamples, double sampleRate);

    TimecodeState getState() const;
    double getElapsedSeconds() const;
    int getCurrentBar() const;
    int getCurrentBeat() const;
    int getCurrentPhrase() const;
    double getBeatPhase() const;
    std::string getTimecodeString() const;
    std::string getBarBeatString() const;

    std::string formatTimecode(double seconds) const;
    std::string formatBeatPosition() const;

    // true sur le temps 1 de la mesure.
    bool isOnDownbeat() const;
    bool isOnPhraseBoundary() const;

    void setTimecodeCallback(TimecodeCallback callback);

private:
    void updateBarBeat();
    void notifyCallback();

    std::atomic<double> elapsed_{0.0};
    std::atomic<double> bpm_{120.0};
    std::atomic<bool> running_{false};

    int beatsPerBar_ = 4;
    int barsPerPhrase_ = 8;

    std::atomic<int> bar_{0};
    std::atomic<int> beat_{0};
    std::atomic<int> phrase_{0};
    std::atomic<double> beatPhase_{0.0};

    double beatAccumulator_ = 0.0;
    int totalBeats_ = 0;

    std::chrono::steady_clock::time_point startTime_;
    TimecodeCallback callback_;
    mutable std::mutex mutex_;
};

} // namespace BeatMate::Core
