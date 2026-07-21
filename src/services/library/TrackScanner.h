#pragma once
#include <juce_events/juce_events.h>

#include <juce_core/juce_core.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>

namespace BeatMate::Services::Library {

class TrackScanner : private juce::Timer {
public:
    TrackScanner();
    ~TrackScanner() override;

    std::vector<std::string> scanFolder(const std::string& path, bool recursive = true);

    bool watchFolder(const std::string& path, bool recursive = false);
    bool unwatchFolder(const std::string& path);
    void unwatchAll();
    std::vector<std::string> getWatchedFolders() const;

    static bool isSupportedFile(const std::string& filePath);
    static std::vector<std::string> getSupportedExtensions();

    using FileCallback = std::function<void(const std::string&)>;
    void setOnFileAdded(FileCallback cb) { fileAddedCb_ = std::move(cb); }
    void setOnFileRemoved(FileCallback cb) { fileRemovedCb_ = std::move(cb); }
    void setOnFileModified(FileCallback cb) { fileModifiedCb_ = std::move(cb); }
    void setOnDirectoryChanged(FileCallback cb) { dirChangedCb_ = std::move(cb); }

private:
    void timerCallback() override;

    void checkDirectoryChanges(const std::string& dirPath);

    std::map<std::string, std::vector<std::string>> watchedContents_;
    std::map<std::string, bool> watchedRecursive_;
    int pollIntervalMs_ = 2000;

    FileCallback fileAddedCb_;
    FileCallback fileRemovedCb_;
    FileCallback fileModifiedCb_;
    FileCallback dirChangedCb_;
};

}
