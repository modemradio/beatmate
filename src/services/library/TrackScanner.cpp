#include "TrackScanner.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>

namespace BeatMate::Services::Library {

TrackScanner::TrackScanner() = default;

TrackScanner::~TrackScanner() {
    unwatchAll();
}

std::vector<std::string> TrackScanner::scanFolder(const std::string& path, bool recursive) {
    std::vector<std::string> files;

    const juce::File root{juce::String(path)};
    if (!root.isDirectory()) {
        spdlog::error("TrackScanner: Invalid directory: {}", path);
        return files;
    }

    for (const auto& entry : juce::RangedDirectoryIterator(
             root, recursive, "*", juce::File::findFiles)) {
        const std::string p = entry.getFile().getFullPathName().toStdString();
        if (isSupportedFile(p))
            files.push_back(p);
    }

    spdlog::info("TrackScanner: Found {} audio files in {}", files.size(), path);
    return files;
}

bool TrackScanner::watchFolder(const std::string& path, bool recursive) {
    if (!juce::File{juce::String(path)}.isDirectory()) {
        spdlog::error("TrackScanner: Cannot watch non-existent directory: {}", path);
        return false;
    }

    watchedRecursive_[path] = recursive;
    watchedContents_[path] = scanFolder(path, recursive);

    if (!isTimerRunning()) {
        startTimer(pollIntervalMs_);
    }

    spdlog::info("TrackScanner: Watching folder: {}", path);
    return true;
}

bool TrackScanner::unwatchFolder(const std::string& path) {
    auto it = watchedContents_.find(path);
    if (it != watchedContents_.end()) {
        watchedContents_.erase(it);
        watchedRecursive_.erase(path);
        spdlog::info("TrackScanner: Stopped watching: {}", path);

        if (watchedContents_.empty()) {
            stopTimer();
        }
        return true;
    }
    return false;
}

void TrackScanner::unwatchAll() {
    watchedContents_.clear();
    watchedRecursive_.clear();
    stopTimer();
    spdlog::info("TrackScanner: Stopped all watches");
}

std::vector<std::string> TrackScanner::getWatchedFolders() const {
    std::vector<std::string> folders;
    for (const auto& [dir, _] : watchedContents_) {
        folders.push_back(dir);
    }
    return folders;
}

void TrackScanner::timerCallback() {
    std::vector<std::string> dirs;
    for (const auto& [dir, _] : watchedContents_) {
        dirs.push_back(dir);
    }

    for (const auto& dir : dirs) {
        checkDirectoryChanges(dir);
    }
}

void TrackScanner::checkDirectoryChanges(const std::string& dirPath) {
    if (!juce::File{juce::String(dirPath)}.isDirectory()) return;

    const auto recIt = watchedRecursive_.find(dirPath);
    const bool recursive = recIt != watchedRecursive_.end() && recIt->second;
    auto currentFiles = scanFolder(dirPath, recursive);

    auto it = watchedContents_.find(dirPath);
    if (it != watchedContents_.end()) {
        std::set<std::string> oldSet(it->second.begin(), it->second.end());
        std::set<std::string> newSet(currentFiles.begin(), currentFiles.end());

        bool changed = false;

        for (const auto& file : newSet) {
            if (oldSet.find(file) == oldSet.end()) {
                if (fileAddedCb_) fileAddedCb_(file);
                spdlog::info("TrackScanner: File added: {}", file);
                changed = true;
            }
        }

        for (const auto& file : oldSet) {
            if (newSet.find(file) == newSet.end()) {
                if (fileRemovedCb_) fileRemovedCb_(file);
                spdlog::info("TrackScanner: File removed: {}", file);
                changed = true;
            }
        }

        if (changed && dirChangedCb_) {
            dirChangedCb_(dirPath);
        }
    }

    watchedContents_[dirPath] = currentFiles;
}

bool TrackScanner::isSupportedFile(const std::string& filePath) {
    auto exts = getSupportedExtensions();
    const auto dot = filePath.find_last_of('.');
    std::string ext = dot == std::string::npos ? std::string() : filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::string> TrackScanner::getSupportedExtensions() {
    return {"mp3", "flac", "wav", "ogg", "aac", "m4a", "aiff", "aif", "wma", "opus", "mp4"};
}

}
