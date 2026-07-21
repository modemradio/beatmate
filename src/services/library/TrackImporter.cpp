#include "TrackImporter.h"
#include "TrackDatabase.h"
#include "TrackMetadata.h"
#include "DuplicateDetector.h"
#include "../config/I18n.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <juce_core/juce_core.h>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

TrackImporter::TrackImporter(std::shared_ptr<TrackDatabase> database, std::shared_ptr<TrackMetadata> metadata)
    : database_(std::move(database)), metadata_(std::move(metadata)) {
}

void TrackImporter::setDuplicateDetector(std::shared_ptr<DuplicateDetector> detector) {
    duplicateDetector_ = std::move(detector);
}

FileImportReport TrackImporter::importFiles(const std::vector<StagedImportEntry>& entries,
                                            const FileImportOptions& options,
                                            ImportProgressCallback progressCallback) {
    FileImportReport report;
    cancelled_ = false;

    if (!database_) {
        report.errors = static_cast<int>(entries.size());
        return report;
    }

    juce::File libraryDir;
    if (options.copyToLibrary && !options.libraryFolder.empty()) {
        libraryDir = juce::File(juce::String::fromUTF8(options.libraryFolder.c_str()));
        if (!libraryDir.isDirectory() && !libraryDir.createDirectory().wasOk()) {
            spdlog::error("TrackImporter: Cannot create library folder {}", options.libraryFolder);
            libraryDir = juce::File();
        }
    }

    std::vector<Models::Track> snapshot;
    if (options.detectDuplicates && duplicateDetector_)
        snapshot = database_->getAllTracks();

    ImportProgress progress;
    progress.total = static_cast<int>(entries.size());

    database_->beginTransaction();

    for (const auto& entry : entries) {
        if (cancelled_) {
            report.cancelled = true;
            break;
        }

        progress.processed++;
        progress.currentFile = entry.filePath;

        auto fail = [&](const std::string& reason) {
            report.errors++;
            progress.errors++;
            report.errorFiles.push_back({ entry.filePath, reason });
        };

        juce::File sourceFile(juce::String::fromUTF8(entry.filePath.c_str()));
        if (!sourceFile.existsAsFile()) {
            fail(BM_T("import.err.notFound"));
            if (progressCallback) progressCallback(progress);
            continue;
        }
        if (!TrackMetadata::isSupportedFormat(entry.filePath)) {
            fail(BM_T("import.err.unsupported"));
            if (progressCallback) progressCallback(progress);
            continue;
        }

        if (database_->getTrackByPath(entry.filePath).has_value()) {
            report.duplicates++;
            progress.skipped++;
            if (progressCallback) progressCallback(progress);
            continue;
        }

        Models::Track track;
        bool haveMeta = false;
        if (options.readTags && metadata_) {
            auto trackOpt = metadata_->readMetadata(entry.filePath);
            if (trackOpt.has_value()) {
                track = std::move(*trackOpt);
                haveMeta = true;
                auto art = metadata_->readAlbumArt(entry.filePath);
                if (!art.empty())
                    track.albumArt = std::move(art);
            }
        }
        if (!haveMeta) {
            track = Models::Track{};
            track.filePath = entry.filePath;
            track.title = sourceFile.getFileNameWithoutExtension().toStdString();
            track.fileFormat = sourceFile.getFileExtension().substring(1).toLowerCase().toStdString();
            track.fileSize = sourceFile.getSize();
        }
        track.dateAdded = juce::Time::currentTimeMillis() / 1000;

        if (!entry.titleOverride.empty()) track.title = entry.titleOverride;
        if (!entry.artistOverride.empty()) track.artist = entry.artistOverride;
        if (!entry.albumOverride.empty()) track.album = entry.albumOverride;
        if (!entry.genreOverride.empty()) track.genre = entry.genreOverride;

        if (options.detectDuplicates && duplicateDetector_) {
            auto match = duplicateDetector_->findMatchForCandidate(track, snapshot);
            if (match.has_value()) {
                report.duplicates++;
                progress.skipped++;
                spdlog::info("TrackImporter: Fuzzy duplicate skipped '{}' (matches '{}', {:.0f}%)",
                             entry.filePath, match->existing.filePath, match->confidence * 100.0f);
                if (progressCallback) progressCallback(progress);
                continue;
            }
        }

        if (options.copyToLibrary && libraryDir.isDirectory()) {
            juce::File dest = libraryDir.getChildFile(sourceFile.getFileName());
            int suffix = 2;
            while (dest.existsAsFile()) {
                if (database_->getTrackByPath(dest.getFullPathName().toStdString()).has_value()
                    || dest.getSize() != sourceFile.getSize()) {
                    dest = libraryDir.getChildFile(
                        sourceFile.getFileNameWithoutExtension()
                        + " (" + juce::String(suffix++) + ")" + sourceFile.getFileExtension());
                } else {
                    break;
                }
            }
            if (!dest.existsAsFile() && !sourceFile.copyFileTo(dest)) {
                fail(BM_T("import.err.copyFailed"));
                if (progressCallback) progressCallback(progress);
                continue;
            }
            const std::string destPath = dest.getFullPathName().toStdString();
            if (database_->getTrackByPath(destPath).has_value()) {
                report.duplicates++;
                progress.skipped++;
                if (progressCallback) progressCallback(progress);
                continue;
            }
            track.filePath = destPath;
        }

        int64_t id = database_->addTrack(track);
        if (id > 0) {
            report.imported++;
            progress.imported++;
            report.importedIds.push_back(id);
            if (options.detectDuplicates && duplicateDetector_)
                snapshot.push_back(track);
        } else {
            fail(BM_T("import.err.dbInsert"));
        }

        if (progressCallback) progressCallback(progress);
    }

    database_->commitTransaction();

    spdlog::info("TrackImporter: importFiles done. Imported={}, Duplicates={}, Errors={}, Cancelled={}",
                 report.imported, report.duplicates, report.errors, report.cancelled);
    return report;
}

int64_t TrackImporter::importFile(const std::string& filePath) {
    if (!fs::exists(filePath)) {
        spdlog::error("TrackImporter: File not found: {}", filePath);
        return -1;
    }

    if (!TrackMetadata::isSupportedFormat(filePath)) {
        spdlog::warn("TrackImporter: Unsupported format: {}", filePath);
        return -1;
    }

    if (skipDuplicates_ && isDuplicate(filePath)) {
        spdlog::debug("TrackImporter: Skipping duplicate: {}", filePath);
        return -1;
    }

    auto trackOpt = metadata_->readMetadata(filePath);
    if (!trackOpt) {
        spdlog::error("TrackImporter: Failed to read metadata: {}", filePath);
        return -1;
    }

    auto albumArt = metadata_->readAlbumArt(filePath);
    trackOpt->albumArt = std::move(albumArt);

    int64_t id = database_->addTrack(*trackOpt);
    if (id > 0) {
        spdlog::info("TrackImporter: Imported '{}' by '{}' (id={})", trackOpt->title, trackOpt->artist, id);
    }

    return id;
}

std::vector<int64_t> TrackImporter::importFolder(const std::string& folderPath, bool recursive,
                                                   ImportProgressCallback progressCallback) {
    std::vector<int64_t> importedIds;
    cancelled_ = false;

    if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
        spdlog::error("TrackImporter: Invalid folder: {}", folderPath);
        return importedIds;
    }

    std::vector<std::string> files;
    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(folderPath, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file() && TrackMetadata::isSupportedFormat(entry.path().string())) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (entry.is_regular_file() && TrackMetadata::isSupportedFormat(entry.path().string())) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("TrackImporter: Error scanning folder {}: {}", folderPath, e.what());
    }

    spdlog::info("TrackImporter: Found {} supported files in {}", files.size(), folderPath);

    ImportProgress progress;
    progress.total = static_cast<int>(files.size());

    database_->beginTransaction();

    for (const auto& file : files) {
        if (cancelled_) {
            spdlog::info("TrackImporter: Import cancelled");
            break;
        }

        progress.currentFile = file;
        progress.processed++;

        if (skipDuplicates_ && isDuplicate(file)) {
            progress.skipped++;
            if (progressCallback) progressCallback(progress);
            continue;
        }

        auto trackOpt = metadata_->readMetadata(file);
        if (!trackOpt) {
            progress.errors++;
            if (progressCallback) progressCallback(progress);
            continue;
        }

        int64_t id = database_->addTrack(*trackOpt);
        if (id > 0) {
            importedIds.push_back(id);
            progress.imported++;
        } else {
            progress.errors++;
        }

        if (progressCallback) progressCallback(progress);
    }

    database_->commitTransaction();

    spdlog::info("TrackImporter: Import complete. Imported: {}, Skipped: {}, Errors: {}",
                 progress.imported, progress.skipped, progress.errors);

    return importedIds;
}

void TrackImporter::cancel() {
    cancelled_ = true;
}

bool TrackImporter::isCancelled() const {
    return cancelled_;
}

bool TrackImporter::isDuplicate(const std::string& filePath) const {
    auto existing = database_->getTrackByPath(filePath);
    return existing.has_value();
}

} // namespace BeatMate::Services::Library
