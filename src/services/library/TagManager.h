#pragma once
#include <cstdint>

#include <string>
#include <vector>
#include <memory>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

class TagManager {
public:
    explicit TagManager(std::shared_ptr<TrackDatabase> database);
    ~TagManager() = default;

    bool addTag(int64_t trackId, const std::string& tag);
    bool removeTag(int64_t trackId, const std::string& tag);
    std::vector<std::string> getTagsForTrack(int64_t trackId);
    std::vector<Models::Track> getTracksByTag(const std::string& tag);

    bool addTagToTracks(const std::vector<int64_t>& trackIds, const std::string& tag);
    bool removeTagFromTracks(const std::vector<int64_t>& trackIds, const std::string& tag);

    bool setTagsForTrack(int64_t trackId, const std::vector<std::string>& tags);

    std::vector<std::string> getAllTags();
    bool renameTag(const std::string& oldName, const std::string& newName);
    bool deleteTag(const std::string& tag);
    int getTagCount(const std::string& tag);

private:
    int64_t getOrCreateTagId(const std::string& tag);
    std::shared_ptr<TrackDatabase> database_;
};

}
