#include "SetTimecodeService.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

SetTimecodeService::SetTimecodeService() {
    startTime_ = std::chrono::steady_clock::now();
}

void SetTimecodeService::start() {
    if (running_.load()) return;
    startTime_ = std::chrono::steady_clock::now();
    running_.store(true);
    spdlog::info("SetTimecodeService: started at BPM={:.1f}", bpm_.load());
}

void SetTimecodeService::stop() {
    running_.store(false);
    spdlog::info("SetTimecodeService: stopped at {}", formatTimecode(elapsed_.load()));
}

void SetTimecodeService::reset() {
    elapsed_.store(0.0);
    beatAccumulator_ = 0.0;
    totalBeats_ = 0;
    bar_.store(0);
    beat_.store(0);
    phrase_.store(0);
    beatPhase_.store(0.0);
    startTime_ = std::chrono::steady_clock::now();
}

void SetTimecodeService::setBpm(double bpm) {
    bpm_.store(std::max(1.0, bpm));
}

void SetTimecodeService::setPosition(double seconds) {
    elapsed_.store(std::max(0.0, seconds));
    double bpm = bpm_.load();
    if (bpm > 0) {
        double beatDuration = 60.0 / bpm;
        totalBeats_ = static_cast<int>(seconds / beatDuration);
        beatAccumulator_ = seconds - totalBeats_ * beatDuration;
        updateBarBeat();
    }
}

void SetTimecodeService::nudge(double offsetSeconds) {
    elapsed_.store(std::max(0.0, elapsed_.load() + offsetSeconds));
}

void SetTimecodeService::advance(double seconds) {
    if (!running_.load()) return;

    double newElapsed = elapsed_.load() + seconds;
    elapsed_.store(newElapsed);

    double bpm = bpm_.load();
    if (bpm > 0) {
        double beatDuration = 60.0 / bpm;
        beatAccumulator_ += seconds;
        while (beatAccumulator_ >= beatDuration) {
            beatAccumulator_ -= beatDuration;
            totalBeats_++;
        }
        beatPhase_.store(beatAccumulator_ / beatDuration);
        updateBarBeat();
    }

    notifyCallback();
}

void SetTimecodeService::advanceSamples(int numSamples, double sampleRate) {
    if (sampleRate > 0) advance(numSamples / sampleRate);
}

TimecodeState SetTimecodeService::getState() const {
    TimecodeState s;
    s.absoluteSeconds = elapsed_.load();
    s.bpm = bpm_.load();
    s.bar = bar_.load();
    s.beat = beat_.load();
    s.phrase = phrase_.load();
    s.beatPhase = beatPhase_.load();
    s.running = running_.load();
    s.timecodeString = formatTimecode(s.absoluteSeconds);
    s.barBeatString = std::to_string(s.bar + 1) + ":" + std::to_string(s.beat + 1);
    return s;
}

double SetTimecodeService::getElapsedSeconds() const { return elapsed_.load(); }
int SetTimecodeService::getCurrentBar() const { return bar_.load(); }
int SetTimecodeService::getCurrentBeat() const { return beat_.load(); }
int SetTimecodeService::getCurrentPhrase() const { return phrase_.load(); }
double SetTimecodeService::getBeatPhase() const { return beatPhase_.load(); }

std::string SetTimecodeService::getTimecodeString() const {
    return formatTimecode(elapsed_.load());
}

std::string SetTimecodeService::getBarBeatString() const {
    return std::to_string(bar_.load() + 1) + ":" + std::to_string(beat_.load() + 1);
}

std::string SetTimecodeService::formatTimecode(double seconds) const {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
    return buf;
}

std::string SetTimecodeService::formatBeatPosition() const {
    int p = phrase_.load() + 1;
    int b = bar_.load() % barsPerPhrase_ + 1;
    int bt = beat_.load() + 1;
    return "P" + std::to_string(p) + " B" + std::to_string(b) + "." + std::to_string(bt);
}

bool SetTimecodeService::isOnDownbeat() const {
    return beat_.load() == 0 && beatPhase_.load() < 0.1;
}

bool SetTimecodeService::isOnPhraseBoundary() const {
    return isOnDownbeat() && (bar_.load() % barsPerPhrase_) == 0;
}

void SetTimecodeService::setTimecodeCallback(TimecodeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

void SetTimecodeService::updateBarBeat() {
    int beatInBar = totalBeats_ % beatsPerBar_;
    int barNumber = totalBeats_ / beatsPerBar_;
    int phraseNumber = barNumber / barsPerPhrase_;
    beat_.store(beatInBar);
    bar_.store(barNumber);
    phrase_.store(phraseNumber);
}

void SetTimecodeService::notifyCallback() {
    TimecodeCallback cb;
    { std::lock_guard<std::mutex> lock(mutex_); cb = callback_; }
    if (cb) cb(getState());
}

} // namespace BeatMate::Core
