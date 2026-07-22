#include "MissingFileManager.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace BeatMate::Services::Library {

MissingFileManager::MissingFileManager(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
#ifdef _WIN32
    std::string userProfile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
    if (!userProfile.empty()) {
        searchPaths_.push_back(userProfile + "/Music");
        searchPaths_.push_back(userProfile + "/Downloads");
        searchPaths_.push_back(userProfile + "/Desktop");
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        searchPaths_.push_back(home + "/Music");
        searchPaths_.push_back(home + "/Downloads");
    }
#endif
}

MissingScanResult MissingFileManager::scanForMissing(MissingScanProgressCallback progressCb) {
    MissingScanResult result;

    if (!database_) {
        spdlog::error("MissingFileManager: No database available");
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();
    auto allTracks = database_->getAllTracks();
    result.totalTracks = static_cast<int>(allTracks.size());

    std::lock_guard<std::mutex> lock(mutex_);
    missingFiles_.clear();

    for (size_t i = 0; i < allTracks.size(); ++i) {
        const auto& track = allTracks[i];

        if (progressCb && i % 100 == 0) {
            progressCb(static_cast<int>(i), result.totalTracks, track.filePath);
        }

        if (!fs::exists(track.filePath)) {
            MissingFileInfo info;
            info.trackId = track.id;
            info.originalPath = track.filePath;
            info.title = track.title;
            info.artist = track.artist;
            info.fileFormat = track.fileFormat;
            info.fileSize = track.fileSize;
            info.lastSeen = track.lastModified;

            missingFiles_.push_back(info);
            result.missingFiles++;
        }
    }

    result.missingInfos = missingFiles_;
    result.unresolvedFiles = result.missingFiles;

    auto endTime = std::chrono::steady_clock::now();
    result.scanDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    spdlog::info("MissingFileManager: Scan complete - {} total, {} missing in {:.1f}ms",
                 result.totalTracks, result.missingFiles, result.scanDurationMs);

    return result;
}

MissingScanResult MissingFileManager::quickScan(const std::vector<int64_t>& trackIds) {
    MissingScanResult result;
    if (!database_) return result;

    result.totalTracks = static_cast<int>(trackIds.size());

    for (auto id : trackIds) {
        auto trackOpt = database_->getTrack(id);
        if (!trackOpt.has_value()) continue;

        if (!fs::exists(trackOpt->filePath)) {
            MissingFileInfo info;
            info.trackId = trackOpt->id;
            info.originalPath = trackOpt->filePath;
            info.title = trackOpt->title;
            info.artist = trackOpt->artist;
            info.fileFormat = trackOpt->fileFormat;
            info.fileSize = trackOpt->fileSize;
            result.missingInfos.push_back(info);
            result.missingFiles++;
        }
    }

    result.unresolvedFiles = result.missingFiles;
    return result;
}

std::vector<MissingFileInfo> MissingFileManager::getMissingFiles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return missingFiles_;
}

int MissingFileManager::getMissingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(missingFiles_.size());
}

bool MissingFileManager::isFileMissing(int64_t trackId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(missingFiles_.begin(), missingFiles_.end(),
                       [trackId](const auto& f) { return f.trackId == trackId; });
}

RelocationResult MissingFileManager::relocateFile(int64_t trackId, const std::string& newPath) {
    RelocationResult result;
    result.trackId = trackId;

    if (!database_) {
        result.error = "Database not available";
        return result;
    }

    if (!fs::exists(newPath)) {
        result.error = "New path does not exist: " + newPath;
        return result;
    }

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) {
        result.error = "Track not found in database";
        return result;
    }

    result.oldPath = trackOpt->filePath;
    result.newPath = newPath;

    Models::Track track = trackOpt.value();
    track.filePath = newPath;

    try {
        track.fileSize = static_cast<int64_t>(fs::file_size(newPath));
        auto ftime = fs::last_write_time(newPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()));
        track.lastModified = sctp.time_since_epoch().count();
    } catch (...) {}

    if (database_->updateTrack(track)) {
        result.success = true;

        std::lock_guard<std::mutex> lock(mutex_);
        missingFiles_.erase(
            std::remove_if(missingFiles_.begin(), missingFiles_.end(),
                           [trackId](const auto& f) { return f.trackId == trackId; }),
            missingFiles_.end());

        spdlog::info("MissingFileManager: Relocated track id={} to '{}'", trackId, newPath);
    } else {
        result.error = "Failed to update database";
    }

    return result;
}

std::vector<RelocationResult> MissingFileManager::relocateFiles(const std::map<int64_t, std::string>& relocations) {
    std::vector<RelocationResult> results;
    for (const auto& [trackId, newPath] : relocations) {
        results.push_back(relocateFile(trackId, newPath));
    }
    return results;
}

RelocationResult MissingFileManager::autoRelocate(int64_t trackId) {
    RelocationResult result;
    result.trackId = trackId;

    MissingFileInfo info;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(missingFiles_.begin(), missingFiles_.end(),
                               [trackId](const auto& f) { return f.trackId == trackId; });
        if (it == missingFiles_.end()) {
            if (database_) {
                auto trackOpt = database_->getTrack(trackId);
                if (trackOpt.has_value()) {
                    info.trackId = trackOpt->id;
                    info.originalPath = trackOpt->filePath;
                    info.title = trackOpt->title;
                    info.artist = trackOpt->artist;
                    info.fileFormat = trackOpt->fileFormat;
                    info.fileSize = trackOpt->fileSize;
                } else {
                    result.error = "Track not found";
                    return result;
                }
            }
        } else {
            info = *it;
        }
    }

    auto candidates = findCandidates(info);
    if (candidates.empty()) {
        result.error = "No candidates found";
        return result;
    }

    return relocateFile(trackId, candidates[0]);
}

std::vector<RelocationResult> MissingFileManager::autoRelocateAll() {
    std::vector<RelocationResult> results;
    std::vector<MissingFileInfo> missing;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        missing = missingFiles_;
    }

    for (const auto& info : missing) {
        results.push_back(autoRelocate(info.trackId));
    }

    int relocated = 0;
    for (const auto& r : results) {
        if (r.success) relocated++;
    }

    spdlog::info("MissingFileManager: Auto-relocated {} of {} missing files", relocated, missing.size());
    return results;
}

std::vector<std::string> MissingFileManager::findCandidates(const MissingFileInfo& info,
                                                              const std::vector<std::string>& searchPaths) {
    std::vector<std::pair<std::string, float>> scored;

    std::vector<std::string> allPaths = searchPaths;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        allPaths.insert(allPaths.end(), searchPaths_.begin(), searchPaths_.end());
    }

    auto parentDir = fs::path(info.originalPath).parent_path().parent_path().string();
    if (!parentDir.empty() && fs::exists(parentDir)) {
        allPaths.push_back(parentDir);
    }

    std::string targetFilename = normalizeFilename(extractFilename(info.originalPath));

    for (const auto& searchPath : allPaths) {
        if (!fs::exists(searchPath) || !fs::is_directory(searchPath)) continue;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(searchPath,
                 fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;

                std::string candidatePath = entry.path().string();
                float score = calculateMatchScore(info, candidatePath);

                if (score > 0.5f) {
                    scored.emplace_back(candidatePath, score);
                }
            }
        } catch (const std::exception& e) {
            spdlog::debug("MissingFileManager: Error searching {}: {}", searchPath, e.what());
        }
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::string> result;
    for (size_t i = 0; i < std::min(scored.size(), size_t(10)); ++i) {
        result.push_back(scored[i].first);
    }

    return result;
}

void MissingFileManager::addSearchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(searchPaths_.begin(), searchPaths_.end(), path) == searchPaths_.end()) {
        searchPaths_.push_back(path);
    }
}

void MissingFileManager::removeSearchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    searchPaths_.erase(std::remove(searchPaths_.begin(), searchPaths_.end(), path), searchPaths_.end());
}

std::vector<std::string> MissingFileManager::getSearchPaths() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return searchPaths_;
}

int MissingFileManager::removeAllMissing() {
    if (!database_) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    int removed = 0;
    for (const auto& info : missingFiles_) {
        if (database_->deleteTrack(info.trackId)) removed++;
    }
    missingFiles_.clear();
    spdlog::info("MissingFileManager: Removed {} missing tracks from database", removed);
    return removed;
}

int MissingFileManager::removeMissing(int64_t trackId) {
    if (!database_) return 0;

    if (database_->deleteTrack(trackId)) {
        std::lock_guard<std::mutex> lock(mutex_);
        missingFiles_.erase(
            std::remove_if(missingFiles_.begin(), missingFiles_.end(),
                           [trackId](const auto& f) { return f.trackId == trackId; }),
            missingFiles_.end());
        return 1;
    }
    return 0;
}

bool MissingFileManager::verifyFile(int64_t trackId) const {
    if (!database_) return false;
    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) return false;
    return fs::exists(trackOpt->filePath) && fs::is_regular_file(trackOpt->filePath);
}

bool MissingFileManager::verifyFile(const std::string& filePath) const {
    return fs::exists(filePath) && fs::is_regular_file(filePath);
}

float MissingFileManager::calculateMatchScore(const MissingFileInfo& info, const std::string& candidatePath) const {
    float score = 0.0f;

    std::string origFilename = normalizeFilename(extractFilename(info.originalPath));
    std::string candFilename = normalizeFilename(extractFilename(candidatePath));

    if (origFilename == candFilename) {
        score += 0.6f;
    } else {
        std::string candLower = candFilename;
        std::transform(candLower.begin(), candLower.end(), candLower.begin(), ::tolower);

        std::string titleLower = info.title;
        std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);

        std::string artistLower = info.artist;
        std::transform(artistLower.begin(), artistLower.end(), artistLower.begin(), ::tolower);

        if (!titleLower.empty() && candLower.find(titleLower) != std::string::npos) {
            score += 0.3f;
        }
        if (!artistLower.empty() && candLower.find(artistLower) != std::string::npos) {
            score += 0.2f;
        }
    }

    std::string origExt = fs::path(info.originalPath).extension().string();
    std::string candExt = fs::path(candidatePath).extension().string();
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::tolower);
    std::transform(candExt.begin(), candExt.end(), candExt.begin(), ::tolower);
    if (origExt == candExt) score += 0.2f;

    if (info.fileSize > 0) {
        try {
            auto candSize = static_cast<int64_t>(fs::file_size(candidatePath));
            double ratio = static_cast<double>(candSize) / static_cast<double>(info.fileSize);
            if (ratio > 0.9 && ratio < 1.1) {
                score += 0.2f;
            }
        } catch (...) {}
    }

    return score;
}

std::string MissingFileManager::extractFilename(const std::string& path) const {
    return fs::path(path).filename().string();
}

std::string MissingFileManager::normalizeFilename(const std::string& filename) const {
    std::string result = filename;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

} // namespace BeatMate::Services::Library
