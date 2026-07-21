#pragma once
#include <cstdint>

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;
class TrackMetadata;

struct ImportProgress {
    int total = 0;
    int processed = 0;
    int imported = 0;
    int skipped = 0;
    int errors = 0;
    std::string currentFile;
    float percentage() const { return total > 0 ? (static_cast<float>(processed) / total) * 100.0f : 0.0f; }
};

using ImportProgressCallback = std::function<void(const ImportProgress&)>;

class DuplicateDetector;

struct StagedImportEntry {
    std::string filePath;
    std::string titleOverride;
    std::string artistOverride;
    std::string albumOverride;
    std::string genreOverride;
};

struct FileImportOptions {
    bool readTags = true;
    bool detectDuplicates = true;
    bool copyToLibrary = false;
    bool autoAnalyze = true;
    std::string libraryFolder;
};

struct FileImportError {
    std::string filePath;
    std::string reason;
};

struct FileImportReport {
    int imported = 0;
    int duplicates = 0;
    int errors = 0;
    bool cancelled = false;
    std::vector<int64_t> importedIds;
    std::vector<FileImportError> errorFiles;
};

class TrackImporter {
public:
    TrackImporter(std::shared_ptr<TrackDatabase> database, std::shared_ptr<TrackMetadata> metadata);
    ~TrackImporter() = default;

    int64_t importFile(const std::string& filePath);

    std::vector<int64_t> importFolder(const std::string& folderPath, bool recursive = true,
                                       ImportProgressCallback progressCallback = nullptr);

    void setDuplicateDetector(std::shared_ptr<DuplicateDetector> detector);
    FileImportReport importFiles(const std::vector<StagedImportEntry>& entries,
                                 const FileImportOptions& options,
                                 ImportProgressCallback progressCallback = nullptr);

    void cancel();
    bool isCancelled() const;

    bool isDuplicate(const std::string& filePath) const;
    void setSkipDuplicates(bool skip) { skipDuplicates_ = skip; }

private:
    std::shared_ptr<TrackDatabase> database_;
    std::shared_ptr<TrackMetadata> metadata_;
    std::shared_ptr<DuplicateDetector> duplicateDetector_;
    std::atomic<bool> cancelled_{false};
    bool skipDuplicates_ = true;
};

} // namespace BeatMate::Services::Library
