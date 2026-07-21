#include "TagEditorService.h"
#include "TrackDatabase.h"
#include "TrackMetadata.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace BeatMate::Services::Library {

TagEditorService::TagEditorService(std::shared_ptr<TrackDatabase> database,
                                     std::shared_ptr<TrackMetadata> metadata)
    : database_(std::move(database))
    , metadata_(std::move(metadata)) {
}

TagEditResult TagEditorService::editTrack(int64_t trackId,
                                            const std::map<MetadataField, std::string>& changes,
                                            bool writeToFile) {
    TagEditResult result;
    result.trackId = trackId;

    if (!database_) {
        result.error = "Database not available";
        return result;
    }

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) {
        result.error = "Track not found";
        return result;
    }

    Models::Track track = trackOpt.value();
    result.filePath = track.filePath;

    // Apply changes
    for (const auto& [field, value] : changes) {
        MetadataChange change;
        change.field = field;
        change.oldValue = getFieldFromTrack(track, field);
        change.newValue = value;
        applyFieldToTrack(track, field, value);
        result.changes.push_back(change);
    }

    // Update database
    if (database_->updateTrack(track)) {
        result.databaseUpdated = true;
    } else {
        result.error = "Failed to update database";
        return result;
    }

    // Write to file
    if (writeToFile && metadata_ && !track.filePath.empty()) {
        if (metadata_->writeMetadata(track.filePath, track)) {
            result.fileWritten = true;
        } else {
            spdlog::warn("TagEditorService: Failed to write metadata to file: {}", track.filePath);
        }
    }

    result.success = result.databaseUpdated;
    recordHistory(result);

    spdlog::debug("TagEditorService: Edited track id={}, {} changes applied", trackId, changes.size());
    return result;
}

TagEditResult TagEditorService::editTrackByPath(const std::string& filePath,
                                                   const std::map<MetadataField, std::string>& changes,
                                                   bool writeToFile) {
    if (!database_) {
        TagEditResult result;
        result.filePath = filePath;
        result.error = "Database not available";
        return result;
    }

    auto trackOpt = database_->getTrackByPath(filePath);
    if (!trackOpt.has_value()) {
        TagEditResult result;
        result.filePath = filePath;
        result.error = "Track not found in database";
        return result;
    }

    return editTrack(trackOpt->id, changes, writeToFile);
}

std::vector<TagEditResult> TagEditorService::batchEdit(const BatchEditSpec& spec,
                                                         BatchEditProgressCallback progressCb) {
    std::vector<TagEditResult> results;
    BatchEditProgress progress;
    progress.total = static_cast<int>(spec.trackIds.size());

    for (auto trackId : spec.trackIds) {
        progress.processed++;

        auto result = editTrack(trackId, spec.fieldsToSet, spec.writeToFile);
        if (result.success) {
            progress.succeeded++;
        } else {
            progress.failed++;
        }

        results.push_back(result);

        if (progressCb && progress.processed % 10 == 0) {
            progressCb(progress);
        }
    }

    if (progressCb) progressCb(progress);

    spdlog::info("TagEditorService: Batch edit complete - {} succeeded, {} failed",
                 progress.succeeded, progress.failed);
    return results;
}

// Convenience methods
bool TagEditorService::setTitle(int64_t trackId, const std::string& title, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Title, title}}, writeToFile).success;
}

bool TagEditorService::setArtist(int64_t trackId, const std::string& artist, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Artist, artist}}, writeToFile).success;
}

bool TagEditorService::setAlbum(int64_t trackId, const std::string& album, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Album, album}}, writeToFile).success;
}

bool TagEditorService::setGenre(int64_t trackId, const std::string& genre, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Genre, genre}}, writeToFile).success;
}

bool TagEditorService::setYear(int64_t trackId, int year, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Year, std::to_string(year)}}, writeToFile).success;
}

bool TagEditorService::setBPM(int64_t trackId, double bpm, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::BPM, std::to_string(bpm)}}, writeToFile).success;
}

bool TagEditorService::setKey(int64_t trackId, const std::string& key, bool writeToFile) {
    return editTrack(trackId, {{MetadataField::Key, key}}, writeToFile).success;
}

bool TagEditorService::setRating(int64_t trackId, int rating) {
    return editTrack(trackId, {{MetadataField::Rating, std::to_string(rating)}}, false).success;
}

bool TagEditorService::setEnergy(int64_t trackId, float energy) {
    return editTrack(trackId, {{MetadataField::Energy, std::to_string(energy)}}, false).success;
}

bool TagEditorService::setColor(int64_t trackId, const std::string& color) {
    return editTrack(trackId, {{MetadataField::Color, color}}, false).success;
}

bool TagEditorService::setMood(int64_t trackId, const std::string& mood) {
    return editTrack(trackId, {{MetadataField::Mood, mood}}, false).success;
}

bool TagEditorService::setAlbumArt(int64_t trackId, const std::vector<uint8_t>& imageData) {
    if (!database_ || !metadata_) return false;

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) return false;

    return metadata_->writeAlbumArt(trackOpt->filePath, imageData);
}

bool TagEditorService::removeAlbumArt(int64_t trackId) {
    return setAlbumArt(trackId, {});
}

std::vector<uint8_t> TagEditorService::getAlbumArt(int64_t trackId) {
    if (!database_ || !metadata_) return {};

    auto trackOpt = database_->getTrack(trackId);
    if (!trackOpt.has_value()) return {};

    return metadata_->readAlbumArt(trackOpt->filePath);
}

// Batch convenience
bool TagEditorService::batchSetGenre(const std::vector<int64_t>& trackIds, const std::string& genre, bool writeToFile) {
    BatchEditSpec spec;
    spec.trackIds = trackIds;
    spec.fieldsToSet[MetadataField::Genre] = genre;
    spec.writeToFile = writeToFile;
    auto results = batchEdit(spec);
    return std::all_of(results.begin(), results.end(), [](const auto& r) { return r.success; });
}

bool TagEditorService::batchSetRating(const std::vector<int64_t>& trackIds, int rating) {
    BatchEditSpec spec;
    spec.trackIds = trackIds;
    spec.fieldsToSet[MetadataField::Rating] = std::to_string(rating);
    spec.writeToFile = false;
    auto results = batchEdit(spec);
    return std::all_of(results.begin(), results.end(), [](const auto& r) { return r.success; });
}

bool TagEditorService::batchSetColor(const std::vector<int64_t>& trackIds, const std::string& color) {
    BatchEditSpec spec;
    spec.trackIds = trackIds;
    spec.fieldsToSet[MetadataField::Color] = color;
    spec.writeToFile = false;
    auto results = batchEdit(spec);
    return std::all_of(results.begin(), results.end(), [](const auto& r) { return r.success; });
}

bool TagEditorService::batchSetLabel(const std::vector<int64_t>& trackIds, const std::string& label) {
    BatchEditSpec spec;
    spec.trackIds = trackIds;
    spec.fieldsToSet[MetadataField::Label] = label;
    spec.writeToFile = false;
    auto results = batchEdit(spec);
    return std::all_of(results.begin(), results.end(), [](const auto& r) { return r.success; });
}

bool TagEditorService::batchSetEnergy(const std::vector<int64_t>& trackIds, float energy) {
    BatchEditSpec spec;
    spec.trackIds = trackIds;
    spec.fieldsToSet[MetadataField::Energy] = std::to_string(energy);
    spec.writeToFile = false;
    auto results = batchEdit(spec);
    return std::all_of(results.begin(), results.end(), [](const auto& r) { return r.success; });
}

// Auto-formatting
std::string TagEditorService::formatArtistName(const std::string& artist) {
    return capitalizeWords(artist);
}

std::string TagEditorService::formatTitle(const std::string& title) {
    std::string result = title;
    // Remove leading/trailing whitespace
    auto start = result.find_first_not_of(" \t\n\r");
    auto end = result.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    result = result.substr(start, end - start + 1);
    return result;
}

std::string TagEditorService::capitalizeWords(const std::string& text) {
    std::string result = text;
    bool capitalizeNext = true;
    for (size_t i = 0; i < result.size(); ++i) {
        if (capitalizeNext && std::isalpha(static_cast<unsigned char>(result[i]))) {
            result[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[i])));
            capitalizeNext = false;
        } else if (result[i] == ' ' || result[i] == '-' || result[i] == '_') {
            capitalizeNext = true;
        }
    }
    return result;
}

std::string TagEditorService::cleanFilename(const std::string& filename) {
    std::string result;
    for (char c : filename) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*') {
            result += '_';
        } else {
            result += c;
        }
    }
    return result;
}

bool TagEditorService::canUndo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !editHistory_.empty();
}

bool TagEditorService::undo() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (editHistory_.empty()) return false;

    auto lastEdit = editHistory_.back();
    editHistory_.pop_back();

    // Reverse the changes
    std::map<MetadataField, std::string> reverseChanges;
    for (const auto& change : lastEdit.changes) {
        reverseChanges[change.field] = change.oldValue;
    }

    // Apply without recording history
    if (!database_) return false;
    auto trackOpt = database_->getTrack(lastEdit.trackId);
    if (!trackOpt.has_value()) return false;

    Models::Track track = trackOpt.value();
    for (const auto& [field, value] : reverseChanges) {
        applyFieldToTrack(track, field, value);
    }

    bool success = database_->updateTrack(track);
    if (success && lastEdit.fileWritten && metadata_) {
        metadata_->writeMetadata(track.filePath, track);
    }

    spdlog::info("TagEditorService: Undone edit on track id={}", lastEdit.trackId);
    return success;
}

void TagEditorService::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    editHistory_.clear();
}

void TagEditorService::applyFieldToTrack(Models::Track& track, MetadataField field, const std::string& value) {
    switch (field) {
        case MetadataField::Title:       track.title = value; break;
        case MetadataField::Artist:      track.artist = value; break;
        case MetadataField::Album:       track.album = value; break;
        case MetadataField::Genre:       track.genre = value; break;
        case MetadataField::Year:        try { track.year = std::stoi(value); } catch (...) {} break;
        case MetadataField::Comment:     track.comment = value; break;
        case MetadataField::BPM:         try { track.bpm = std::stod(value); } catch (...) {} break;
        case MetadataField::Key:         track.key = value; break;
        case MetadataField::Energy:      try { track.energy = std::stof(value); } catch (...) {} break;
        case MetadataField::Rating:      try { track.rating = std::stoi(value); } catch (...) {} break;
        case MetadataField::Color:       track.color = value; break;
        case MetadataField::Label:       track.label = value; break;
        case MetadataField::Grouping:    track.grouping = value; break;
        case MetadataField::Mood:        track.mood = value; break;
        case MetadataField::Danceability: try { track.danceability = std::stof(value); } catch (...) {} break;
        case MetadataField::CamelotKey:  track.camelotKey = value; break;
        case MetadataField::OpenKey:     track.openKey = value; break;
    }
}

std::string TagEditorService::getFieldFromTrack(const Models::Track& track, MetadataField field) const {
    switch (field) {
        case MetadataField::Title:       return track.title;
        case MetadataField::Artist:      return track.artist;
        case MetadataField::Album:       return track.album;
        case MetadataField::Genre:       return track.genre;
        case MetadataField::Year:        return std::to_string(track.year);
        case MetadataField::Comment:     return track.comment;
        case MetadataField::BPM:         return std::to_string(track.bpm);
        case MetadataField::Key:         return track.key;
        case MetadataField::Energy:      return std::to_string(track.energy);
        case MetadataField::Rating:      return std::to_string(track.rating);
        case MetadataField::Color:       return track.color;
        case MetadataField::Label:       return track.label;
        case MetadataField::Grouping:    return track.grouping;
        case MetadataField::Mood:        return track.mood;
        case MetadataField::Danceability: return std::to_string(track.danceability);
        case MetadataField::CamelotKey:  return track.camelotKey;
        case MetadataField::OpenKey:     return track.openKey;
        default: return "";
    }
}

void TagEditorService::recordHistory(const TagEditResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    editHistory_.push_back(result);
    if (static_cast<int>(editHistory_.size()) > MAX_HISTORY) {
        editHistory_.erase(editHistory_.begin());
    }
}

} // namespace BeatMate::Services::Library
