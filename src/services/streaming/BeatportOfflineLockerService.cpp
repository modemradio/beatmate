#include "BeatportOfflineLockerService.h"
#include "StreamingServiceBase.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace BeatMate::Services::Streaming {

BeatportOfflineLockerService::BeatportOfflineLockerService() {
    auto appDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BeatMate").getChildFile("BeatportLocker");
    appDir.createDirectory();
    lockerDir_ = appDir.getFullPathName().toStdString();
    defaultStatePath_ = appDir.getChildFile("locker_state.json").getFullPathName().toStdString();
}


void BeatportOfflineLockerService::setLockerDirectory(const std::string& path) {
    lockerDir_ = path;
    fs::create_directories(path);
    spdlog::info("BeatportOfflineLockerService: Locker directory set to {}", path);
}


void BeatportOfflineLockerService::addPurchasedTrack(const LockerTrack& track) {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    tracks_[track.beatportId] = track;
    if (tracks_[track.beatportId].status == LockerTrackStatus::Purchased) {
        tracks_[track.beatportId].purchasedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    spdlog::info("BeatportOfflineLockerService: Added '{}' by {} to locker", track.title, track.artist);
}

void BeatportOfflineLockerService::removePurchasedTrack(int64_t beatportId) {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    auto it = tracks_.find(beatportId);
    if (it == tracks_.end()) return;

    if (!it->second.localFilePath.empty() && fs::exists(it->second.localFilePath)) {
        fs::remove(it->second.localFilePath);
    }
    if (!it->second.artworkPath.empty() && fs::exists(it->second.artworkPath)) {
        fs::remove(it->second.artworkPath);
    }

    spdlog::info("BeatportOfflineLockerService: Removed track {} from locker", beatportId);
    tracks_.erase(it);
}

std::vector<LockerTrack> BeatportOfflineLockerService::getAllTracks() const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> result;
    result.reserve(tracks_.size());
    for (const auto& [id, track] : tracks_) result.push_back(track);
    return result;
}

std::vector<LockerTrack> BeatportOfflineLockerService::getDownloadedTracks() const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> result;
    for (const auto& [id, track] : tracks_) {
        if (track.status == LockerTrackStatus::Downloaded) result.push_back(track);
    }
    return result;
}

std::vector<LockerTrack> BeatportOfflineLockerService::getPendingDownloads() const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> result;
    for (const auto& [id, track] : tracks_) {
        if (track.status == LockerTrackStatus::Purchased ||
            track.status == LockerTrackStatus::DownloadFailed) {
            result.push_back(track);
        }
    }
    return result;
}

LockerTrack BeatportOfflineLockerService::getTrack(int64_t beatportId) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    auto it = tracks_.find(beatportId);
    return (it != tracks_.end()) ? it->second : LockerTrack{};
}

bool BeatportOfflineLockerService::hasTrack(int64_t beatportId) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    return tracks_.count(beatportId) > 0;
}


bool BeatportOfflineLockerService::downloadTrack(int64_t beatportId) {
    {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        if (downloadsPaused_) {
            spdlog::info("BeatportOfflineLockerService: Downloads paused, queuing {}", beatportId);
            return false;
        }
        if (activeDownloads_ >= maxConcurrentDownloads_) {
            spdlog::info("BeatportOfflineLockerService: Max concurrent downloads reached, queuing {}", beatportId);
            return false;
        }
    }

    return performDownload(beatportId);
}

bool BeatportOfflineLockerService::downloadAllPending() {
    auto pending = getPendingDownloads();
    int started = 0;
    for (const auto& track : pending) {
        if (downloadTrack(track.beatportId)) started++;
    }
    spdlog::info("BeatportOfflineLockerService: Started {} downloads ({} pending)",
                 started, pending.size());
    return started > 0;
}

bool BeatportOfflineLockerService::cancelDownload(int64_t beatportId) {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    auto it = tracks_.find(beatportId);
    if (it == tracks_.end()) return false;
    if (it->second.status == LockerTrackStatus::Downloading) {
        it->second.status = LockerTrackStatus::Purchased;
        it->second.downloadProgress = 0.0;
        activeDownloads_--;
        spdlog::info("BeatportOfflineLockerService: Cancelled download for {}", beatportId);
        return true;
    }
    return false;
}

void BeatportOfflineLockerService::pauseAllDownloads() {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    downloadsPaused_ = true;
    spdlog::info("BeatportOfflineLockerService: All downloads paused");
}

void BeatportOfflineLockerService::resumeAllDownloads() {
    {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        downloadsPaused_ = false;
    }
    spdlog::info("BeatportOfflineLockerService: Downloads resumed");
    downloadAllPending();
}

int BeatportOfflineLockerService::getActiveDownloadCount() const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    return activeDownloads_;
}


std::string BeatportOfflineLockerService::getLocalFilePath(int64_t beatportId) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    auto it = tracks_.find(beatportId);
    if (it != tracks_.end() && it->second.status == LockerTrackStatus::Downloaded) {
        return it->second.localFilePath;
    }
    return "";
}

bool BeatportOfflineLockerService::isAvailableOffline(int64_t beatportId) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    auto it = tracks_.find(beatportId);
    if (it == tracks_.end()) return false;
    return it->second.status == LockerTrackStatus::Downloaded &&
           fs::exists(it->second.localFilePath);
}

int64_t BeatportOfflineLockerService::getTotalLockerSizeBytes() const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    int64_t total = 0;
    for (const auto& [id, track] : tracks_) {
        if (track.status == LockerTrackStatus::Downloaded && !track.localFilePath.empty()) {
            try {
                if (fs::exists(track.localFilePath)) {
                    total += static_cast<int64_t>(fs::file_size(track.localFilePath));
                }
            } catch (...) {}
        }
    }
    return total;
}

int64_t BeatportOfflineLockerService::getAvailableDiskSpaceBytes() const {
    try {
        auto spaceInfo = fs::space(lockerDir_);
        return static_cast<int64_t>(spaceInfo.available);
    } catch (...) {
        return 0;
    }
}


bool BeatportOfflineLockerService::syncWithBeatportAccount(const std::string& accessToken) {
    spdlog::info("BeatportOfflineLockerService: Syncing with Beatport account...");

    auto httpResp = StreamingServiceBase::httpGet(
        "https://api.beatport.com/v4/my/tracks?per_page=200",
        "Authorization: Bearer " + juce::String(accessToken));
    if (!httpResp.ok()) {
        spdlog::error("BeatportOfflineLockerService: Sync failed (status={})", httpResp.status);
        return false;
    }

    try {
        auto resp = json::parse(httpResp.body);
        auto& items = resp.contains("results") ? resp["results"] : resp;
        int added = 0;
        if (items.is_array()) {
            for (const auto& item : items) {
                int64_t id = item.value("id", static_cast<int64_t>(0));
                if (id > 0 && !hasTrack(id)) {
                    LockerTrack lt;
                    lt.beatportId = id;
                    lt.title = item.value("name", "");
                    if (item.contains("artists") && !item["artists"].empty())
                        lt.artist = item["artists"][0].value("name", "");
                    if (item.contains("label")) lt.label = item["label"].value("name", "");
                    if (item.contains("genre")) lt.genre = item["genre"].value("name", "");
                    lt.bpm = static_cast<int>(item.value("bpm", 0.0));
                    if (item.contains("key")) lt.key = item["key"].value("name", "");
                    lt.downloadUrl = item.value("download_url", "");
                    lt.status = LockerTrackStatus::Purchased;
                    addPurchasedTrack(lt);
                    added++;
                }
            }
        }
        spdlog::info("BeatportOfflineLockerService: Sync complete, added {} new tracks", added);
        saveLockerState();
        return true;
    } catch (const std::exception& e) {
        spdlog::error("BeatportOfflineLockerService: Sync parse error: {}", e.what());
        return false;
    }
}

void BeatportOfflineLockerService::verifyLocalFiles() {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    int missing = 0;
    for (auto& [id, track] : tracks_) {
        if (track.status == LockerTrackStatus::Downloaded) {
            if (track.localFilePath.empty() || !fs::exists(track.localFilePath)) {
                track.status = LockerTrackStatus::Purchased;
                track.localFilePath.clear();
                missing++;
            }
        }
    }
    if (missing > 0) {
        spdlog::warn("BeatportOfflineLockerService: {} downloaded tracks have missing files", missing);
    }
}


bool BeatportOfflineLockerService::saveLockerState(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::string path = filePath.empty() ? defaultStatePath_ : filePath;

    json j = json::array();
    for (const auto& [id, track] : tracks_) {
        j.push_back({
            {"beatportId", track.beatportId},
            {"title", track.title},
            {"artist", track.artist},
            {"label", track.label},
            {"genre", track.genre},
            {"bpm", track.bpm},
            {"key", track.key},
            {"localFilePath", track.localFilePath},
            {"downloadUrl", track.downloadUrl},
            {"artworkPath", track.artworkPath},
            {"format", track.format},
            {"fileSizeBytes", track.fileSizeBytes},
            {"purchasedAt", track.purchasedAt},
            {"downloadedAt", track.downloadedAt},
            {"status", static_cast<int>(track.status)}
        });
    }

    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    spdlog::info("BeatportOfflineLockerService: Saved locker state ({} tracks) to {}", tracks_.size(), path);
    return true;
}

bool BeatportOfflineLockerService::loadLockerState(const std::string& filePath) {
    std::string path = filePath.empty() ? defaultStatePath_ : filePath;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;

    try {
        auto j = json::parse(ifs);
        std::lock_guard<std::mutex> lock(lockerMutex_);
        tracks_.clear();

        for (const auto& item : j) {
            LockerTrack track;
            track.beatportId = item.value("beatportId", static_cast<int64_t>(0));
            track.title = item.value("title", "");
            track.artist = item.value("artist", "");
            track.label = item.value("label", "");
            track.genre = item.value("genre", "");
            track.bpm = item.value("bpm", 0);
            track.key = item.value("key", "");
            track.localFilePath = item.value("localFilePath", "");
            track.downloadUrl = item.value("downloadUrl", "");
            track.artworkPath = item.value("artworkPath", "");
            track.format = item.value("format", "");
            track.fileSizeBytes = item.value("fileSizeBytes", 0);
            track.purchasedAt = item.value("purchasedAt", static_cast<int64_t>(0));
            track.downloadedAt = item.value("downloadedAt", static_cast<int64_t>(0));
            track.status = static_cast<LockerTrackStatus>(item.value("status", 0));
            tracks_[track.beatportId] = track;
        }

        spdlog::info("BeatportOfflineLockerService: Loaded {} tracks from {}", tracks_.size(), path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("BeatportOfflineLockerService: Load error: {}", e.what());
        return false;
    }
}


void BeatportOfflineLockerService::setDownloadProgressCallback(DownloadProgressCallback callback) {
    progressCallback_ = std::move(callback);
}


std::vector<LockerTrack> BeatportOfflineLockerService::searchLocker(const std::string& query) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> results;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [id, track] : tracks_) {
        std::string lowerTitle = track.title;
        std::string lowerArtist = track.artist;
        std::string lowerLabel = track.label;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
        std::transform(lowerArtist.begin(), lowerArtist.end(), lowerArtist.begin(), ::tolower);
        std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), ::tolower);

        if (lowerTitle.find(lowerQuery) != std::string::npos ||
            lowerArtist.find(lowerQuery) != std::string::npos ||
            lowerLabel.find(lowerQuery) != std::string::npos) {
            results.push_back(track);
        }
    }
    return results;
}

std::vector<LockerTrack> BeatportOfflineLockerService::filterByGenre(const std::string& genre) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> results;
    for (const auto& [id, track] : tracks_) {
        if (track.genre == genre) results.push_back(track);
    }
    return results;
}

std::vector<LockerTrack> BeatportOfflineLockerService::filterByBpmRange(int minBpm, int maxBpm) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> results;
    for (const auto& [id, track] : tracks_) {
        if (track.bpm >= minBpm && track.bpm <= maxBpm) results.push_back(track);
    }
    return results;
}

std::vector<LockerTrack> BeatportOfflineLockerService::filterByKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(lockerMutex_);
    std::vector<LockerTrack> results;
    for (const auto& [id, track] : tracks_) {
        if (track.key == key) results.push_back(track);
    }
    return results;
}


bool BeatportOfflineLockerService::performDownload(int64_t beatportId) {
    LockerTrack track;
    {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        auto it = tracks_.find(beatportId);
        if (it == tracks_.end()) return false;
        if (it->second.downloadUrl.empty()) {
            spdlog::error("BeatportOfflineLockerService: No download URL for track {}", beatportId);
            return false;
        }
        it->second.status = LockerTrackStatus::Downloading;
        it->second.downloadProgress = 0.0;
        activeDownloads_++;
        track = it->second;
    }

    spdlog::info("BeatportOfflineLockerService: Downloading '{}' by {}...", track.title, track.artist);

    std::string localPath = buildLocalPath(track);

    juce::URL url(track.downloadUrl);
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(StreamingServiceBase::kHttpTimeoutMs));

    if (!stream) {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        tracks_[beatportId].status = LockerTrackStatus::DownloadFailed;
        activeDownloads_--;
        spdlog::error("BeatportOfflineLockerService: Download failed for track {}", beatportId);
        if (progressCallback_) progressCallback_(beatportId, 0.0, "failed");
        return false;
    }

    juce::File outFile(localPath);
    outFile.getParentDirectory().createDirectory();
    auto outStream = outFile.createOutputStream();
    if (!outStream) {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        tracks_[beatportId].status = LockerTrackStatus::DownloadFailed;
        activeDownloads_--;
        return false;
    }

    const int chunkSize = 65536;
    std::vector<char> buffer(chunkSize);
    int64_t totalRead = 0;

    while (!stream->isExhausted()) {
        int bytesRead = stream->read(buffer.data(), chunkSize);
        if (bytesRead <= 0) break;
        outStream->write(buffer.data(), bytesRead);
        totalRead += bytesRead;

        double progress = (track.fileSizeBytes > 0)
                              ? static_cast<double>(totalRead) / track.fileSizeBytes
                              : 0.5;

        {
            std::lock_guard<std::mutex> lock(lockerMutex_);
            tracks_[beatportId].downloadProgress = progress;
        }

        if (progressCallback_) progressCallback_(beatportId, progress, "downloading");
    }

    outStream->flush();

    {
        std::lock_guard<std::mutex> lock(lockerMutex_);
        tracks_[beatportId].status = LockerTrackStatus::Downloaded;
        tracks_[beatportId].localFilePath = localPath;
        tracks_[beatportId].downloadProgress = 1.0;
        tracks_[beatportId].downloadedAt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        tracks_[beatportId].fileSizeBytes = static_cast<int>(totalRead);
        activeDownloads_--;
    }

    if (progressCallback_) progressCallback_(beatportId, 1.0, "complete");
    spdlog::info("BeatportOfflineLockerService: Downloaded '{}' ({} bytes)", track.title, totalRead);
    saveLockerState();
    return true;
}

std::string BeatportOfflineLockerService::buildLocalPath(const LockerTrack& track) const {
    std::string filename = track.artist + " - " + track.title;
    for (auto& c : filename) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    std::string ext = track.format.empty() ? preferredFormat_ : track.format;
    return lockerDir_ + "/" + filename + "." + ext;
}

}
