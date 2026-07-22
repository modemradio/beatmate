#include "UniversalMusicScanner.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

const std::map<std::string, AudioFormatInfo>& UniversalMusicScanner::getFormatMap() {
    static const std::map<std::string, AudioFormatInfo> formats = {
        {"mp3",  {"mp3",  "audio/mpeg",        "MPEG Audio Layer 3",           false, 16, 48000}},
        {"flac", {"flac", "audio/flac",        "Free Lossless Audio Codec",    true,  32, 384000}},
        {"wav",  {"wav",  "audio/wav",         "Waveform Audio File",          true,  32, 384000}},
        {"ogg",  {"ogg",  "audio/ogg",         "Ogg Vorbis",                   false, 16, 48000}},
        {"aac",  {"aac",  "audio/aac",         "Advanced Audio Coding",        false, 16, 96000}},
        {"m4a",  {"m4a",  "audio/mp4",         "MPEG-4 Audio",                 false, 16, 96000}},
        {"aiff", {"aiff", "audio/aiff",        "Audio Interchange File Format",true,  32, 192000}},
        {"aif",  {"aif",  "audio/aiff",        "Audio Interchange File Format",true,  32, 192000}},
        {"wma",  {"wma",  "audio/x-ms-wma",    "Windows Media Audio",          false, 16, 48000}},
        {"opus", {"opus", "audio/opus",        "Opus Interactive Audio Codec",  false, 16, 48000}},
        {"mp4",  {"mp4",  "audio/mp4",         "MPEG-4 Audio Container",       false, 16, 96000}},
        {"alac", {"alac", "audio/alac",        "Apple Lossless Audio Codec",    true,  32, 384000}},
    };
    return formats;
}

UniversalMusicScanner::UniversalMusicScanner() = default;

UniversalMusicScanner::UniversalMusicScanner(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

ScanProgress UniversalMusicScanner::scanDirectory(const std::string& path, const ScanOptions& options,
                                                    ScanProgressCallback progressCb) {
    return scanDirectories({path}, options, progressCb);
}

ScanProgress UniversalMusicScanner::scanDirectories(const std::vector<std::string>& paths,
                                                      const ScanOptions& options,
                                                      ScanProgressCallback progressCb) {
    cancelled_ = false;
    scanning_ = true;
    ScanProgress progress;

    auto startTime = std::chrono::steady_clock::now();

    spdlog::info("UniversalMusicScanner: Collecting files from {} directories", paths.size());
    std::vector<std::string> allFiles;
    for (const auto& path : paths) {
        if (cancelled_) break;
        progress.currentFolder = path;
        auto files = collectFiles(path, options);
        allFiles.insert(allFiles.end(), files.begin(), files.end());
    }
    progress.totalFiles = static_cast<int>(allFiles.size());
    spdlog::info("UniversalMusicScanner: Found {} audio files to process", allFiles.size());

    for (const auto& filePath : allFiles) {
        if (cancelled_) {
            spdlog::info("UniversalMusicScanner: Scan cancelled");
            break;
        }

        progress.currentFile = filePath;
        progress.scannedFiles++;

        if (database_) {
            auto existing = database_->getTrackByPath(filePath);
            if (existing.has_value()) {
                if (options.detectModified && isFileModified(filePath)) {
                    progress.modifiedFiles++;
                } else {
                    progress.skippedFiles++;
                    if (progressCb && progress.scannedFiles % 50 == 0) {
                        progressCb(progress);
                    }
                    continue;
                }
            } else {
                progress.newFiles++;
            }
        } else {
            progress.newFiles++;
        }

        if (progressCb && progress.scannedFiles % 10 == 0) {
            progressCb(progress);
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    lastScanDurationMs_ = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    totalScannedFiles_ += progress.scannedFiles;
    scanning_ = false;

    spdlog::info("UniversalMusicScanner: Scan complete - {} total, {} new, {} modified, {} skipped, {} errors in {:.1f}ms",
                 progress.scannedFiles, progress.newFiles, progress.modifiedFiles,
                 progress.skippedFiles, progress.errorFiles, lastScanDurationMs_);

    if (progressCb) progressCb(progress);
    return progress;
}

ScanProgress UniversalMusicScanner::quickScan(const std::string& path, ScanProgressCallback progressCb) {
    ScanOptions options;
    options.recursive = true;
    options.detectModified = true;
    return scanDirectory(path, options, progressCb);
}

void UniversalMusicScanner::cancel() {
    cancelled_ = true;
    spdlog::info("UniversalMusicScanner: Cancel requested");
}

bool UniversalMusicScanner::isCancelled() const {
    return cancelled_;
}

bool UniversalMusicScanner::isScanning() const {
    return scanning_;
}

bool UniversalMusicScanner::isSupportedFormat(const std::string& filePath) {
    std::string ext = fs::path(filePath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return getFormatMap().count(ext) > 0;
}

std::vector<AudioFormatInfo> UniversalMusicScanner::getSupportedFormats() {
    std::vector<AudioFormatInfo> formats;
    for (const auto& [_, info] : getFormatMap()) {
        formats.push_back(info);
    }
    return formats;
}

std::vector<std::string> UniversalMusicScanner::getSupportedExtensions() {
    std::vector<std::string> exts;
    for (const auto& [ext, _] : getFormatMap()) {
        exts.push_back(ext);
    }
    return exts;
}

std::string UniversalMusicScanner::getFormatDescription(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = getFormatMap().find(ext);
    return it != getFormatMap().end() ? it->second.description : "Unknown format";
}

bool UniversalMusicScanner::isValidAudioFile(const std::string& filePath) {
    if (!fs::exists(filePath)) return false;
    if (!fs::is_regular_file(filePath)) return false;
    if (!isSupportedFormat(filePath)) return false;
    auto size = fs::file_size(filePath);
    return size > 1024; // At least 1KB
}

int64_t UniversalMusicScanner::getFileSize(const std::string& filePath) {
    try {
        return static_cast<int64_t>(fs::file_size(filePath));
    } catch (...) {
        return 0;
    }
}

int64_t UniversalMusicScanner::getFileModTime(const std::string& filePath) {
    try {
        auto ftime = fs::last_write_time(filePath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()));
        return sctp.time_since_epoch().count();
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> UniversalMusicScanner::collectFiles(const std::string& path,
                                                               const ScanOptions& options) {
    std::vector<std::string> files;

    if (!fs::exists(path) || !fs::is_directory(path)) {
        spdlog::warn("UniversalMusicScanner: Invalid directory: {}", path);
        return files;
    }

    try {
        auto dirOptions = fs::directory_options::skip_permission_denied;
        if (options.followSymlinks) {
            dirOptions |= fs::directory_options::follow_directory_symlink;
        }

        if (options.recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(path, dirOptions)) {
                if (cancelled_) break;
                if (entry.is_directory()) {
                    if (shouldSkipFolder(entry.path().string(), options)) {
                        continue;
                    }
                }
                if (entry.is_regular_file() && !shouldSkipFile(entry.path().string(), options)) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path, dirOptions)) {
                if (cancelled_) break;
                if (entry.is_regular_file() && !shouldSkipFile(entry.path().string(), options)) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("UniversalMusicScanner: Error scanning {}: {}", path, e.what());
    }

    return files;
}

bool UniversalMusicScanner::shouldSkipFile(const std::string& filePath, const ScanOptions& options) const {
    if (!isSupportedFormat(filePath)) return true;

    if (!options.includedFormats.empty()) {
        std::string ext = normalizeExtension(fs::path(filePath).extension().string());
        if (options.includedFormats.find(ext) == options.includedFormats.end()) return true;
    }

    if (options.skipHidden) {
        auto filename = fs::path(filePath).filename().string();
        if (!filename.empty() && filename[0] == '.') return true;
    }

    try {
        auto size = static_cast<int64_t>(fs::file_size(filePath));
        if (size < options.minFileSize || size > options.maxFileSize) return true;
    } catch (...) {
        return true;
    }

    return false;
}

bool UniversalMusicScanner::shouldSkipFolder(const std::string& folderPath, const ScanOptions& options) const {
    auto folderName = fs::path(folderPath).filename().string();

    if (options.skipHidden && !folderName.empty() && folderName[0] == '.') return true;

    if (options.excludedFolders.count(folderName) > 0) return true;

    return false;
}

bool UniversalMusicScanner::isFileModified(const std::string& filePath) const {
    if (!database_) return true;

    auto track = database_->getTrackByPath(filePath);
    if (!track.has_value()) return true;

    int64_t currentModTime = getFileModTime(filePath);
    return currentModTime > track->lastModified;
}

std::string UniversalMusicScanner::normalizeExtension(const std::string& ext) const {
    std::string result = ext;
    if (!result.empty() && result[0] == '.') result = result.substr(1);
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

} // namespace BeatMate::Services::Library
