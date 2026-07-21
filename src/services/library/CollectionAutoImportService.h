#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>
#include <set>
#include <map>
#include <thread>
#include <cstdint>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;
class TrackMetadata;
class UniversalMusicScanner;

enum class ImportEvent {
    FileDetected,
    FileImported,
    FileSkipped,
    FileFailed,
    ScanStarted,
    ScanCompleted
};

struct ImportResult {
    std::string filePath;
    int64_t trackId = -1;
    bool success = false;
    std::string error;
    ImportEvent event = ImportEvent::FileDetected;
};

struct ImportSummary {
    int filesDetected = 0;
    int filesImported = 0;
    int filesSkipped = 0;
    int filesFailed = 0;
    double durationMs = 0.0;
    std::vector<std::string> errors;
};

using ImportEventCallback = std::function<void(const ImportResult&)>;
using ImportCompleteCallback = std::function<void(const ImportSummary&)>;

struct AutoImportConfig {
    std::vector<std::string> watchedFolders;
    bool recursive = true;
    bool autoAnalyze = false;
    bool skipDuplicates = true;
    int scanIntervalSeconds = 30;
    std::set<std::string> excludedFolders;
};

class CollectionAutoImportService {
public:
    CollectionAutoImportService(std::shared_ptr<TrackDatabase> database,
                                 std::shared_ptr<TrackMetadata> metadata);
    ~CollectionAutoImportService();

    ImportSummary importDirectory(const std::string& path, bool recursive = true,
                                   ImportEventCallback eventCb = nullptr);
    ImportResult importFile(const std::string& filePath);
    ImportSummary importFiles(const std::vector<std::string>& files, ImportEventCallback eventCb = nullptr);

    bool startAutoImport(const AutoImportConfig& config);
    void stopAutoImport();
    bool isAutoImportRunning() const;

    bool addWatchedFolder(const std::string& path);
    bool removeWatchedFolder(const std::string& path);
    std::vector<std::string> getWatchedFolders() const;

    void cancel();
    bool isCancelled() const;

    void setOnImportEvent(ImportEventCallback cb) { eventCallback_ = std::move(cb); }
    void setOnImportComplete(ImportCompleteCallback cb) { completeCallback_ = std::move(cb); }

    ImportSummary getLastImportSummary() const;

private:
    void autoImportLoop();
    void scanAndImport(const std::string& folder, bool recursive, ImportSummary& summary);
    void refreshKnownPaths();

    std::shared_ptr<TrackDatabase> database_;
    std::shared_ptr<TrackMetadata> metadata_;

    std::unordered_set<std::string> knownPathsNorm_;
    bool knownPathsBuilt_ = false;

    AutoImportConfig config_;
    ImportSummary lastSummary_;

    std::atomic<bool> cancelled_{false};
    std::atomic<bool> autoImportRunning_{false};
    std::thread autoImportThread_;
    mutable std::mutex mutex_;

    ImportEventCallback eventCallback_;
    ImportCompleteCallback completeCallback_;
};

} // namespace BeatMate::Services::Library
