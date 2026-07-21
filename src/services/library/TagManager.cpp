#include "TagManager.h"
#include "TrackDatabase.h"

#include <algorithm>
#include <set>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Library {

TagManager::TagManager(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

bool TagManager::addTag(int64_t trackId, const std::string& tag) {
    if (!database_ || tag.empty()) return false;

    int64_t tagId = getOrCreateTagId(tag);
    if (tagId < 0) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR IGNORE INTO track_tags (track_id, tag_id) VALUES (?, ?)";
    if (sqlite3_prepare_v2(database_->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::addTag prepare failed: {}", sqlite3_errmsg(database_->db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, trackId);
    sqlite3_bind_int64(stmt, 2, tagId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool TagManager::removeTag(int64_t trackId, const std::string& tag) {
    if (!database_ || tag.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "DELETE FROM track_tags WHERE track_id=? AND tag_id=("
        "SELECT id FROM tags WHERE name=?)";
    if (sqlite3_prepare_v2(database_->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::removeTag prepare failed: {}", sqlite3_errmsg(database_->db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, trackId);
    sqlite3_bind_text(stmt, 2, tag.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<std::string> TagManager::getTagsForTrack(int64_t trackId) {
    std::vector<std::string> tags;
    if (!database_) return tags;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT t.name FROM tags t "
        "JOIN track_tags tt ON t.id = tt.tag_id "
        "WHERE tt.track_id = ? ORDER BY t.name";
    if (sqlite3_prepare_v2(database_->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::getTagsForTrack prepare failed: {}", sqlite3_errmsg(database_->db_));
        return tags;
    }
    sqlite3_bind_int64(stmt, 1, trackId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 0);
        if (name) tags.emplace_back(reinterpret_cast<const char*>(name));
    }
    sqlite3_finalize(stmt);
    return tags;
}

std::vector<Models::Track> TagManager::getTracksByTag(const std::string& tag) {
    return database_->getTracksByQuery(
        "SELECT tr.* FROM tracks tr "
        "JOIN track_tags tt ON tr.id = tt.track_id "
        "JOIN tags t ON t.id = tt.tag_id "
        "WHERE t.name = ?",
        {tag}
    );
}

bool TagManager::addTagToTracks(const std::vector<int64_t>& trackIds, const std::string& tag) {
    if (!database_) return false;
    database_->beginTransaction();
    for (auto id : trackIds) {
        if (!addTag(id, tag)) {
            database_->rollbackTransaction();
            return false;
        }
    }
    database_->commitTransaction();
    spdlog::info("TagManager: Added tag '{}' to {} tracks", tag, trackIds.size());
    return true;
}

bool TagManager::removeTagFromTracks(const std::vector<int64_t>& trackIds, const std::string& tag) {
    if (!database_) return false;
    database_->beginTransaction();
    for (auto id : trackIds) {
        if (!removeTag(id, tag)) {
            database_->rollbackTransaction();
            return false;
        }
    }
    database_->commitTransaction();
    return true;
}

bool TagManager::setTagsForTrack(int64_t trackId, const std::vector<std::string>& tags) {
    if (!database_) return false;

    auto current = getTagsForTrack(trackId);
    std::set<std::string> currentSet(current.begin(), current.end());
    std::set<std::string> targetSet(tags.begin(), tags.end());

    database_->beginTransaction();
    for (const auto& t : targetSet) {
        if (!currentSet.count(t) && !addTag(trackId, t)) {
            database_->rollbackTransaction();
            return false;
        }
    }
    for (const auto& t : currentSet) {
        if (!targetSet.count(t) && !removeTag(trackId, t)) {
            database_->rollbackTransaction();
            return false;
        }
    }
    database_->commitTransaction();
    return true;
}

std::vector<std::string> TagManager::getAllTags() {
    std::vector<std::string> tags;
    if (!database_) return tags;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database_->db_, "SELECT name FROM tags ORDER BY name",
                            -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::getAllTags prepare failed: {}", sqlite3_errmsg(database_->db_));
        return tags;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 0);
        if (name) tags.emplace_back(reinterpret_cast<const char*>(name));
    }
    sqlite3_finalize(stmt);
    return tags;
}

bool TagManager::renameTag(const std::string& oldName, const std::string& newName) {
    if (!database_ || oldName.empty() || newName.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database_->db_, "UPDATE tags SET name=? WHERE name=?",
                            -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::renameTag prepare failed: {}", sqlite3_errmsg(database_->db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, newName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, oldName.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (ok) spdlog::info("TagManager: Renamed tag '{}' -> '{}'", oldName, newName);
    return ok;
}

bool TagManager::deleteTag(const std::string& tag) {
    if (!database_ || tag.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database_->db_, "DELETE FROM tags WHERE name=?",
                            -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("TagManager::deleteTag prepare failed: {}", sqlite3_errmsg(database_->db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (ok) spdlog::info("TagManager: Deleted tag '{}'", tag);
    return ok;
}

int TagManager::getTagCount(const std::string& tag) {
    if (!database_ || tag.empty()) return 0;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT COUNT(*) FROM track_tags tt "
        "JOIN tags t ON t.id = tt.tag_id WHERE t.name = ?";
    if (sqlite3_prepare_v2(database_->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int64_t TagManager::getOrCreateTagId(const std::string& tag) {
    if (!database_ || tag.empty()) return -1;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database_->db_, "INSERT OR IGNORE INTO tags (name) VALUES (?)",
                            -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(database_->db_, "SELECT id FROM tags WHERE name=?",
                            -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

} // namespace BeatMate::Services::Library
