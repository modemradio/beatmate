#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../../models/Track.h"
#include "../../models/CuePoint.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::Services::Export {

struct BatchExportItem {
    int64_t trackId = 0;
    std::string sourcePath;
    std::string title;
    std::string artist;
    double durationSec = 0.0;
};

struct BatchExportSettings {
    int formatId = 1;
    int bitRateKbps = 320;
    bool vbr = false;
    int sampleRate = 44100;
    int bitDepth = 16;
    bool normalize = false;
    float targetLufs = -14.0f;
    bool writeTags = true;
    bool writeM3U = true;
    int structureId = 1;
    std::string destinationDir;
    bool targetRekordbox = false;
    bool targetSerato = false;
    bool targetTraktor = false;
    bool targetVirtualDJ = false;
};

struct TrackExportOutcome {
    enum class Status { Copied, Converted, Failed, Cancelled };
    int64_t trackId = 0;
    std::string sourcePath;
    std::string outputPath;
    std::string message;
    Status status = Status::Failed;
    bool normalized = false;
    bool normalizationSkipped = false;
    bool gainLimitedByPeak = false;
    float measuredLufs = 0.0f;
    float appliedGainDb = 0.0f;
    bool tagsWritten = false;
    bool seratoWritten = false;
    int64_t fileSizeBytes = 0;
};

struct BatchExportReport {
    std::vector<TrackExportOutcome> outcomes;
    std::vector<std::string> djLibraryFiles;
    std::string m3uPath;
    std::string reportJsonPath;
    int succeeded = 0;
    int failed = 0;
    int cancelledCount = 0;
    int64_t totalBytes = 0;
    bool cancelled = false;
    BatchExportSettings settings;
};

class BatchExportService {
public:
    struct Callbacks {
        std::function<void(int done, int total, juce::String current)> onProgress;
        std::function<void(BatchExportReport)> onFinished;
    };

    explicit BatchExportService(Services::Library::TrackDataProvider* provider);
    ~BatchExportService();

    bool start(std::vector<BatchExportItem> items,
               BatchExportSettings settings,
               Callbacks callbacks);
    void cancel();
    bool isRunning() const { return m_running.load(); }

    static bool isFfmpegAvailable();
    static std::string formatExtension(int formatId);

private:
    struct PreparedItem {
        BatchExportItem item;
        Models::Track track;
        std::vector<Models::CuePoint> cues;
        juce::File destFile;
        bool inLibrary = false;
    };

    class ExportJob;
    friend class ExportJob;

    void processItem(int index);
    void finishBatch();
    TrackExportOutcome exportOne(const PreparedItem& prepared);
    bool encodeWithJuce(const juce::AudioBuffer<float>& buffer, int sampleRate,
                        const juce::File& dest, std::string& error);
    bool encodeWithFfmpeg(const juce::AudioBuffer<float>& buffer, int sampleRate,
                          const juce::File& dest, std::string& error);
    bool writeTempWav(const juce::AudioBuffer<float>& buffer, int sampleRate,
                      const juce::File& tempFile, std::string& error);

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::unique_ptr<juce::ThreadPool> m_pool;
    std::vector<PreparedItem> m_prepared;
    BatchExportSettings m_settings;
    Callbacks m_callbacks;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<int> m_nextIndex{0};
    std::atomic<int> m_doneCount{0};
    std::mutex m_outcomesMutex;
    std::vector<TrackExportOutcome> m_outcomes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchExportService)
};

}
