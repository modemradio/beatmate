#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <optional>
#include <mutex>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;
class TrackMetadata;

enum class MetadataField {
    Title,
    Artist,
    Album,
    Genre,
    Year,
    Comment,
    BPM,
    Key,
    Energy,
    Rating,
    Color,
    Label,
    Grouping,
    Mood,
    Danceability,
    CamelotKey,
    OpenKey
};

struct MetadataChange {
    MetadataField field;
    std::string oldValue;
    std::string newValue;
};

struct TagEditResult {
    int64_t trackId = 0;
    std::string filePath;
    bool success = false;
    bool fileWritten = false;
    bool databaseUpdated = false;
    std::string error;
    std::vector<MetadataChange> changes;
};

struct BatchEditSpec {
    std::vector<int64_t> trackIds;
    std::map<MetadataField, std::string> fieldsToSet;
    bool writeToFile = true;
    bool updateDatabase = true;
};

struct BatchEditProgress {
    int total = 0;
    int processed = 0;
    int succeeded = 0;
    int failed = 0;
    std::string currentTrack;
    float percentage() const { return total > 0 ? (static_cast<float>(processed) / total) * 100.0f : 0.0f; }
};

using BatchEditProgressCallback = std::function<void(const BatchEditProgress&)>;

class TagEditorService {
public:
    TagEditorService(std::shared_ptr<TrackDatabase> database,
                     std::shared_ptr<TrackMetadata> metadata);
    ~TagEditorService() = default;

    TagEditResult editTrack(int64_t trackId, const std::map<MetadataField, std::string>& changes,
                             bool writeToFile = true);
    TagEditResult editTrackByPath(const std::string& filePath, const std::map<MetadataField, std::string>& changes,
                                   bool writeToFile = true);

    std::vector<TagEditResult> batchEdit(const BatchEditSpec& spec,
                                          BatchEditProgressCallback progressCb = nullptr);

    bool setTitle(int64_t trackId, const std::string& title, bool writeToFile = true);
    bool setArtist(int64_t trackId, const std::string& artist, bool writeToFile = true);
    bool setAlbum(int64_t trackId, const std::string& album, bool writeToFile = true);
    bool setGenre(int64_t trackId, const std::string& genre, bool writeToFile = true);
    bool setYear(int64_t trackId, int year, bool writeToFile = true);
    bool setBPM(int64_t trackId, double bpm, bool writeToFile = true);
    bool setKey(int64_t trackId, const std::string& key, bool writeToFile = true);
    bool setRating(int64_t trackId, int rating);
    bool setEnergy(int64_t trackId, float energy);
    bool setColor(int64_t trackId, const std::string& color);
    bool setMood(int64_t trackId, const std::string& mood);

    bool setAlbumArt(int64_t trackId, const std::vector<uint8_t>& imageData);
    bool removeAlbumArt(int64_t trackId);
    std::vector<uint8_t> getAlbumArt(int64_t trackId);

    bool batchSetGenre(const std::vector<int64_t>& trackIds, const std::string& genre, bool writeToFile = true);
    bool batchSetRating(const std::vector<int64_t>& trackIds, int rating);
    bool batchSetColor(const std::vector<int64_t>& trackIds, const std::string& color);
    bool batchSetLabel(const std::vector<int64_t>& trackIds, const std::string& label);
    bool batchSetEnergy(const std::vector<int64_t>& trackIds, float energy);

    static std::string formatArtistName(const std::string& artist);
    static std::string formatTitle(const std::string& title);
    static std::string capitalizeWords(const std::string& text);
    static std::string cleanFilename(const std::string& filename);

    bool canUndo() const;
    bool undo();
    void clearHistory();

private:
    void applyFieldToTrack(Models::Track& track, MetadataField field, const std::string& value);
    std::string getFieldFromTrack(const Models::Track& track, MetadataField field) const;
    void recordHistory(const TagEditResult& result);

    std::shared_ptr<TrackDatabase> database_;
    std::shared_ptr<TrackMetadata> metadata_;
    std::vector<TagEditResult> editHistory_;
    mutable std::mutex mutex_;
    static constexpr int MAX_HISTORY = 100;
};

} // namespace BeatMate::Services::Library
