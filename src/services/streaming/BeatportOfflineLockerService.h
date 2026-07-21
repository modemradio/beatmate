#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace BeatMate::Services::Streaming {

enum class LockerTrackStatus {
    Purchased,
    Downloading,
    Downloaded,
    DownloadFailed,
    Expired,
    Removed
};

struct LockerTrack {
    int64_t beatportId = 0;
    std::string title;
    std::string artist;
    std::string label;
    std::string genre;
    int bpm = 0;
    std::string key;
    std::string localFilePath;
    std::string downloadUrl;
    std::string artworkPath;
    std::string format;          // "wav", "aiff", "mp3"
    int fileSizeBytes = 0;
    int64_t purchasedAt = 0;
    int64_t downloadedAt = 0;
    LockerTrackStatus status = LockerTrackStatus::Purchased;
    double downloadProgress = 0.0;
};

using DownloadProgressCallback = std::function<void(int64_t trackId, double progress, const std::string& status)>;

class BeatportOfflineLockerService {
public:
    BeatportOfflineLockerService();
    ~BeatportOfflineLockerService() = default;

    void setLockerDirectory(const std::string& path);
    std::string getLockerDirectory() const { return lockerDir_; }
    void setPreferredFormat(const std::string& format) { preferredFormat_ = format; }
    void setMaxConcurrentDownloads(int max) { maxConcurrentDownloads_ = max; }

    void addPurchasedTrack(const LockerTrack& track);
    void removePurchasedTrack(int64_t beatportId);
    std::vector<LockerTrack> getAllTracks() const;
    std::vector<LockerTrack> getDownloadedTracks() const;
    std::vector<LockerTrack> getPendingDownloads() const;
    LockerTrack getTrack(int64_t beatportId) const;
    bool hasTrack(int64_t beatportId) const;

    bool downloadTrack(int64_t beatportId);
    bool downloadAllPending();
    bool cancelDownload(int64_t beatportId);
    void pauseAllDownloads();
    void resumeAllDownloads();
    int getActiveDownloadCount() const;

    std::string getLocalFilePath(int64_t beatportId) const;
    bool isAvailableOffline(int64_t beatportId) const;
    int64_t getTotalLockerSizeBytes() const;
    int64_t getAvailableDiskSpaceBytes() const;

    bool syncWithBeatportAccount(const std::string& accessToken);
    void verifyLocalFiles();

    bool saveLockerState(const std::string& filePath = "") const;
    bool loadLockerState(const std::string& filePath = "");

    void setDownloadProgressCallback(DownloadProgressCallback callback);

    std::vector<LockerTrack> searchLocker(const std::string& query) const;
    std::vector<LockerTrack> filterByGenre(const std::string& genre) const;
    std::vector<LockerTrack> filterByBpmRange(int minBpm, int maxBpm) const;
    std::vector<LockerTrack> filterByKey(const std::string& key) const;

private:
    bool performDownload(int64_t beatportId);
    std::string buildLocalPath(const LockerTrack& track) const;

    mutable std::mutex lockerMutex_;
    std::map<int64_t, LockerTrack> tracks_;

    std::string lockerDir_;
    std::string preferredFormat_ = "wav";
    int maxConcurrentDownloads_ = 3;
    int activeDownloads_ = 0;
    bool downloadsPaused_ = false;

    DownloadProgressCallback progressCallback_;
    std::string defaultStatePath_;
};

} // namespace BeatMate::Services::Streaming
