#pragma once

#include <juce_events/juce_events.h>
#include "../../models/Track.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::Core { class AudioAnalysisPipeline; struct TrackAnalysis; }

namespace BeatMate::Services::Analysis {

struct AnalysisOptions {
    bool bpm = true;
    bool key = true;
    bool energy = true;
    bool structure = true;
    bool waveform = true;
    bool mood = false;
    bool ultraStems = false;
};

struct TrackRowResult {
    int64_t trackId = 0;
    std::string path;
    juce::String title;
    double bpm = 0.0;
    float bpmConfidence = 0.0f;
    juce::String key;
    juce::String camelotKey;
    float keyConfidence = 0.0f;
    int energy = 0;
    float lufs = 0.0f;
    bool ok = false;
};

struct AnalysisCallbacks {
    std::function<void(const juce::String& path)> onTrackStarted;
    std::function<void(const TrackRowResult&)> onTrackFinished;
    std::function<void(int processed, int total, int skipped)> onProgress;
    std::function<void(int processed, int total, int skipped, bool cancelled)> onFinished;
};

class AnalysisRunner {
public:
    explicit AnalysisRunner(Library::TrackDataProvider& provider);
    ~AnalysisRunner();

    bool start(std::vector<Models::Track> tracks,
               const AnalysisOptions& options,
               AnalysisCallbacks callbacks);
    void cancel();
    bool isRunning() const { return running_.load(); }

private:
    void workerLoop();
    void processTrack(const Models::Track& track, Core::AudioAnalysisPipeline& pipeline);
    void persistResult(const Models::Track& track, const Core::TrackAnalysis& result);
    void runUltraStems(const Models::Track& track);
    void notifyProgress();
    void joinWorkers();

    Library::TrackDataProvider& provider_;
    std::vector<Models::Track> tracks_;
    AnalysisOptions options_;
    AnalysisCallbacks callbacks_;
    std::vector<std::thread> workers_;
    std::mutex stemsMutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<int> nextIndex_{0};
    std::atomic<int> processed_{0};
    std::atomic<int> skipped_{0};
    std::atomic<int> activeWorkers_{0};
    int total_ = 0;
};

} // namespace BeatMate::Services::Analysis
