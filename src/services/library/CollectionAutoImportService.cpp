#include "CollectionAutoImportService.h"
#include "TrackDatabase.h"
#include "TrackMetadata.h"
#include "UniversalMusicScanner.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <chrono>

namespace BeatMate::Services::Library {

namespace {

std::string normImportPath(const std::string& p) {
    std::string out; out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        out += (char) std::tolower((unsigned char) c);
    }
    return out;
}

} // namespace

CollectionAutoImportService::CollectionAutoImportService(
    std::shared_ptr<TrackDatabase> database,
    std::shared_ptr<TrackMetadata> metadata)
    : database_(std::move(database))
    , metadata_(std::move(metadata)) {
}

CollectionAutoImportService::~CollectionAutoImportService() {
    stopAutoImport();
}

void CollectionAutoImportService::refreshKnownPaths() {
    std::unordered_set<std::string> fresh;
    if (database_ && database_->isOpen()) {
        for (const auto& t : database_->getAllTracks())
            if (!t.filePath.empty())
                fresh.insert(normImportPath(t.filePath));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    knownPathsNorm_ = std::move(fresh);
    knownPathsBuilt_ = true;
    spdlog::info("CollectionAutoImport: known-path index built ({} paths)",
                 knownPathsNorm_.size());
}

ImportResult CollectionAutoImportService::importFile(const std::string& filePath) {
    ImportResult result;
    result.filePath = filePath;
    result.event = ImportEvent::FileDetected;

    const juce::File file{juce::String(filePath)};
    if (!file.existsAsFile()) {
        result.error = "File does not exist or is not a regular file";
        result.event = ImportEvent::FileFailed;
        spdlog::warn("CollectionAutoImport: File not found: {}", filePath);
        return result;
    }

    if (!UniversalMusicScanner::isSupportedFormat(filePath)) {
        result.error = "Unsupported audio format";
        result.event = ImportEvent::FileSkipped;
        return result;
    }

    if (database_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool needBuild = !knownPathsBuilt_;
            lock.unlock();
            if (needBuild) refreshKnownPaths();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (knownPathsNorm_.count(normImportPath(filePath))) {
                result.event = ImportEvent::FileSkipped;
                result.error = "Already in collection";
                return result;
            }
        }
        auto existing = database_->getTrackByPath(filePath);
        if (existing.has_value()) {
            result.trackId = existing->id;
            result.event = ImportEvent::FileSkipped;
            result.error = "Already in collection";
            return result;
        }
    }

    if (!metadata_) {
        result.error = "Metadata reader not available";
        result.event = ImportEvent::FileFailed;
        return result;
    }

    auto trackOpt = metadata_->readMetadata(filePath);
    if (!trackOpt.has_value()) {
        result.error = "Failed to read metadata";
        result.event = ImportEvent::FileFailed;
        spdlog::warn("CollectionAutoImport: Failed to read metadata: {}", filePath);
        return result;
    }

    auto track = trackOpt.value();
    track.filePath = filePath;

    track.fileSize = static_cast<int64_t>(file.getSize());
    track.lastModified = file.getLastModificationTime().toMilliseconds() / 1000;
    track.dateAdded = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    track.fileFormat = file.getFileExtension().trimCharactersAtStart(".")
                           .toLowerCase().toStdString();

    if (track.title.empty()) {
        track.title = file.getFileNameWithoutExtension().toStdString();
    }

    if (database_) {
        int64_t id = database_->addTrack(track);
        if (id > 0) {
            result.trackId = id;
            result.success = true;
            result.event = ImportEvent::FileImported;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                knownPathsNorm_.insert(normImportPath(filePath));
            }
            spdlog::debug("CollectionAutoImport: Imported '{}' (id={})", track.title, id);
        } else {
            result.error = "Failed to add track to database";
            result.event = ImportEvent::FileFailed;
        }
    }

    return result;
}

ImportSummary CollectionAutoImportService::importFiles(const std::vector<std::string>& files,
                                                         ImportEventCallback eventCb) {
    ImportSummary summary;
    summary.filesDetected = static_cast<int>(files.size());

    auto startTime = std::chrono::steady_clock::now();

    if (database_) database_->beginTransaction();

    for (const auto& file : files) {
        if (cancelled_) break;

        auto result = importFile(file);

        switch (result.event) {
            case ImportEvent::FileImported:
                summary.filesImported++;
                break;
            case ImportEvent::FileSkipped:
                summary.filesSkipped++;
                break;
            case ImportEvent::FileFailed:
                summary.filesFailed++;
                if (!result.error.empty()) summary.errors.push_back(result.filePath + ": " + result.error);
                break;
            default:
                break;
        }

        if (eventCb) eventCb(result);
    }

    if (database_) database_->commitTransaction();

    auto endTime = std::chrono::steady_clock::now();
    summary.durationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    spdlog::info("CollectionAutoImport: Batch import complete - {} imported, {} skipped, {} failed in {:.1f}ms",
                 summary.filesImported, summary.filesSkipped, summary.filesFailed, summary.durationMs);

    return summary;
}

ImportSummary CollectionAutoImportService::importDirectory(const std::string& path, bool recursive,
                                                             ImportEventCallback eventCb) {
    cancelled_ = false;
    refreshKnownPaths();

    std::vector<std::string> filePaths;
    const juce::File root{juce::String(path)};
    if (root.isDirectory()) {
        for (const auto& entry : juce::RangedDirectoryIterator(
                 root, recursive, "*", juce::File::findFiles)) {
            if (cancelled_) break;
            const std::string p = entry.getFile().getFullPathName().toStdString();
            if (UniversalMusicScanner::isSupportedFormat(p))
                filePaths.push_back(p);
        }
    }

    auto summary = importFiles(filePaths, eventCb);

    std::lock_guard<std::mutex> lock(mutex_);
    lastSummary_ = summary;
    return summary;
}

bool CollectionAutoImportService::startAutoImport(const AutoImportConfig& config) {
    if (autoImportRunning_) {
        spdlog::warn("CollectionAutoImport: Auto-import already running");
        return false;
    }

    config_ = config;
    cancelled_ = false;
    autoImportRunning_ = true;

    autoImportThread_ = std::thread(&CollectionAutoImportService::autoImportLoop, this);
    spdlog::info("CollectionAutoImport: Auto-import started, watching {} folders",
                 config_.watchedFolders.size());
    return true;
}

void CollectionAutoImportService::stopAutoImport() {
    if (autoImportRunning_) {
        cancelled_ = true;
        autoImportRunning_ = false;
        if (autoImportThread_.joinable()) {
            autoImportThread_.join();
        }
        spdlog::info("CollectionAutoImport: Auto-import stopped");
    }
}

bool CollectionAutoImportService::isAutoImportRunning() const {
    return autoImportRunning_;
}

bool CollectionAutoImportService::addWatchedFolder(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (juce::File{juce::String(path)}.isDirectory()) {
        config_.watchedFolders.push_back(path);
        spdlog::info("CollectionAutoImport: Added watched folder: {}", path);
        return true;
    }
    return false;
}

bool CollectionAutoImportService::removeWatchedFolder(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& folders = config_.watchedFolders;
    auto it = std::find(folders.begin(), folders.end(), path);
    if (it != folders.end()) {
        folders.erase(it);
        spdlog::info("CollectionAutoImport: Removed watched folder: {}", path);
        return true;
    }
    return false;
}

std::vector<std::string> CollectionAutoImportService::getWatchedFolders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.watchedFolders;
}

void CollectionAutoImportService::cancel() {
    cancelled_ = true;
}

bool CollectionAutoImportService::isCancelled() const {
    return cancelled_;
}

ImportSummary CollectionAutoImportService::getLastImportSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSummary_;
}

void CollectionAutoImportService::autoImportLoop() {
    spdlog::info("CollectionAutoImport: Auto-import loop started");

    while (autoImportRunning_ && !cancelled_) {
        ImportSummary totalSummary;
        refreshKnownPaths();

        for (const auto& folder : config_.watchedFolders) {
            if (!autoImportRunning_ || cancelled_) break;

            scanAndImport(folder, config_.recursive, totalSummary);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastSummary_ = totalSummary;
        }

        if (completeCallback_ && totalSummary.filesImported > 0) {
            completeCallback_(totalSummary);
        }

        for (int i = 0; i < config_.scanIntervalSeconds * 10 && autoImportRunning_ && !cancelled_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    spdlog::info("CollectionAutoImport: Auto-import loop ended");
}

void CollectionAutoImportService::scanAndImport(const std::string& folder, bool recursive,
                                                  ImportSummary& summary) {
    const juce::File root{juce::String(folder)};
    if (!root.isDirectory()) return;

    for (const auto& entry : juce::RangedDirectoryIterator(
             root, recursive, "*", juce::File::findFiles)) {
        if (!autoImportRunning_ || cancelled_) return;
        const std::string p = entry.getFile().getFullPathName().toStdString();
        if (!UniversalMusicScanner::isSupportedFormat(p)) continue;

        auto result = importFile(p);
        summary.filesDetected++;

        switch (result.event) {
            case ImportEvent::FileImported:
                summary.filesImported++;
                break;
            case ImportEvent::FileSkipped:
                summary.filesSkipped++;
                break;
            case ImportEvent::FileFailed:
                summary.filesFailed++;
                break;
            default:
                break;
        }

        if (eventCallback_) eventCallback_(result);
    }
}

} // namespace BeatMate::Services::Library
