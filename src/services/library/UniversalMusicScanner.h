#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <memory>
#include <cstdint>

#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

class TrackDatabase;

struct AudioFormatInfo {
    std::string extension;
    std::string mimeType;
    std::string description;
    bool lossless = false;
    int maxBitDepth = 16;
    int maxSampleRate = 48000;
};

struct ScanProgress {
    int totalFiles = 0;
    int scannedFiles = 0;
    int newFiles = 0;
    int modifiedFiles = 0;
    int errorFiles = 0;
    int skippedFiles = 0;
    std::string currentFile;
    std::string currentFolder;
    float percentage() const { return totalFiles > 0 ? (static_cast<float>(scannedFiles) / totalFiles) * 100.0f : 0.0f; }
    bool isComplete() const { return scannedFiles >= totalFiles; }
};

struct ScanOptions {
    bool recursive = true;
    bool followSymlinks = false;
    bool skipHidden = true;
    bool detectModified = true;
    int64_t minFileSize = 1024;
    int64_t maxFileSize = 2147483648LL;
    std::set<std::string> excludedFolders;
    std::set<std::string> includedFormats;  // Empty = all supported
};

using ScanProgressCallback = std::function<void(const ScanProgress&)>;
using ScanCompleteCallback = std::function<void(const ScanProgress&)>;

class UniversalMusicScanner {
public:
    UniversalMusicScanner();
    explicit UniversalMusicScanner(std::shared_ptr<TrackDatabase> database);
    ~UniversalMusicScanner() = default;

    ScanProgress scanDirectory(const std::string& path, const ScanOptions& options = {},
                               ScanProgressCallback progressCb = nullptr);

    ScanProgress scanDirectories(const std::vector<std::string>& paths, const ScanOptions& options = {},
                                  ScanProgressCallback progressCb = nullptr);

    // Quick scan: only detect new/modified files
    ScanProgress quickScan(const std::string& path, ScanProgressCallback progressCb = nullptr);

    void cancel();
    bool isCancelled() const;
    bool isScanning() const;

    static bool isSupportedFormat(const std::string& filePath);
    static std::vector<AudioFormatInfo> getSupportedFormats();
    static std::vector<std::string> getSupportedExtensions();
    static std::string getFormatDescription(const std::string& extension);

    static bool isValidAudioFile(const std::string& filePath);
    static int64_t getFileSize(const std::string& filePath);
    static int64_t getFileModTime(const std::string& filePath);

    int64_t getTotalScannedFiles() const { return totalScannedFiles_; }
    double getLastScanDurationMs() const { return lastScanDurationMs_; }

private:
    std::vector<std::string> collectFiles(const std::string& path, const ScanOptions& options);
    bool shouldSkipFile(const std::string& filePath, const ScanOptions& options) const;
    bool shouldSkipFolder(const std::string& folderPath, const ScanOptions& options) const;
    bool isFileModified(const std::string& filePath) const;
    std::string normalizeExtension(const std::string& ext) const;

    std::shared_ptr<TrackDatabase> database_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> scanning_{false};
    int64_t totalScannedFiles_ = 0;
    double lastScanDurationMs_ = 0.0;
    mutable std::mutex mutex_;

    static const std::map<std::string, AudioFormatInfo>& getFormatMap();
};

}
