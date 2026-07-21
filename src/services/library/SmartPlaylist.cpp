#include "SmartPlaylist.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>

namespace BeatMate::Services::Library {

SmartPlaylist::SmartPlaylist(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

int64_t SmartPlaylist::createSmartPlaylist(const std::string& name, const Models::SmartPlaylistRuleGroup& rules) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("SmartPlaylist: No database connection for createSmartPlaylist");
        return -1;
    }

    nlohmann::json rulesJson = rules;
    std::string rulesStr = rulesJson.dump();

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!database_->executeWrite(
            "INSERT INTO playlists (name, description, is_smart, created_at, modified_at, smart_rules) "
            "VALUES (?, '', 1, ?, ?, ?)",
            { name, std::to_string(now), std::to_string(now), rulesStr })) {
        spdlog::error("SmartPlaylist: createSmartPlaylist INSERT failed for '{}'", name);
        return -1;
    }

    int64_t playlistId = database_->getLastInsertRowId();
    if (playlistId <= 0) {
        spdlog::error("SmartPlaylist: createSmartPlaylist last_insert_rowid returned {}", playlistId);
        return -1;
    }

    auto tracks = evaluateRules(rules);
    for (size_t i = 0; i < tracks.size(); ++i) {
        database_->executeWrite(
            "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) "
            "VALUES (?, ?, ?, ?)",
            { std::to_string(playlistId), std::to_string(tracks[i].id),
              std::to_string(i), std::to_string(now) });
    }

    spdlog::info("SmartPlaylist: Created '{}' (id={}) with {} matching tracks",
                 name, playlistId, tracks.size());
    return playlistId;
}

bool SmartPlaylist::updateRules(int64_t playlistId, const Models::SmartPlaylistRuleGroup& rules) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("SmartPlaylist: No database connection for updateRules");
        return false;
    }

    nlohmann::json rulesJson = rules;
    std::string rulesStr = rulesJson.dump();

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    bool ok = database_->executeWrite(
        "UPDATE playlists SET smart_rules = ?, is_smart = 1, modified_at = ? WHERE id = ?",
        { rulesStr, std::to_string(now), std::to_string(playlistId) });

    if (!ok) {
        spdlog::error("SmartPlaylist: updateRules UPDATE failed for playlist {}", playlistId);
        return false;
    }

    auto tracks = evaluateRules(rules);
    database_->executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ?",
        { std::to_string(playlistId) });
    for (size_t i = 0; i < tracks.size(); ++i) {
        database_->executeWrite(
            "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) "
            "VALUES (?, ?, ?, ?)",
            { std::to_string(playlistId), std::to_string(tracks[i].id),
              std::to_string(i), std::to_string(now) });
    }

    spdlog::info("SmartPlaylist: Updated rules for playlist {} ({} tracks)", playlistId, tracks.size());
    return true;
}

std::vector<Models::Track> SmartPlaylist::evaluateRules(const Models::SmartPlaylistRuleGroup& rules) {
    if (!database_) {
        spdlog::debug("SmartPlaylist: No database, skipping evaluateRules");
        return {};
    }
    std::vector<std::string> params;
    std::string sql = generateSQL(rules, params);
    spdlog::debug("SmartPlaylist: Generated SQL: {} ({} params)", sql, params.size());
    return database_->getTracksByQuery(sql, params);
}

std::vector<Models::Track> SmartPlaylist::refreshPlaylist(int64_t playlistId) {
    if (!database_ || !database_->isOpen()) {
        spdlog::error("SmartPlaylist: No database connection for refreshPlaylist");
        return {};
    }

    std::string rulesStr;
    database_->executeRead(
        "SELECT smart_rules FROM playlists WHERE id = ? AND is_smart = 1",
        { std::to_string(playlistId) },
        [&](sqlite3_stmt* stmt) {
            if (auto t = sqlite3_column_text(stmt, 0))
                rulesStr = reinterpret_cast<const char*>(t);
        });

    if (rulesStr.empty()) {
        spdlog::warn("SmartPlaylist: refreshPlaylist {} has no stored rules", playlistId);
        return {};
    }

    Models::SmartPlaylistRuleGroup rules;
    try {
        rules = nlohmann::json::parse(rulesStr).get<Models::SmartPlaylistRuleGroup>();
    } catch (const std::exception& e) {
        spdlog::error("SmartPlaylist: refreshPlaylist {} rules parse failed: {}", playlistId, e.what());
        return {};
    }

    auto tracks = evaluateRules(rules);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    database_->executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ?",
        { std::to_string(playlistId) });
    for (size_t i = 0; i < tracks.size(); ++i) {
        database_->executeWrite(
            "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) "
            "VALUES (?, ?, ?, ?)",
            { std::to_string(playlistId), std::to_string(tracks[i].id),
              std::to_string(i), std::to_string(now) });
    }
    database_->executeWrite(
        "UPDATE playlists SET modified_at = ? WHERE id = ?",
        { std::to_string(now), std::to_string(playlistId) });

    spdlog::info("SmartPlaylist: Refreshed playlist {} ({} tracks)", playlistId, tracks.size());
    return tracks;
}

std::string SmartPlaylist::generateSQL(const Models::SmartPlaylistRuleGroup& rules,
                                        std::vector<std::string>& params) const {
    std::ostringstream sql;
    sql << "SELECT * FROM tracks WHERE ";

    std::string conjunction = (rules.conjunction == Models::RuleConjunction::AND) ? " AND " : " OR ";

    bool first = true;
    for (const auto& rule : rules.rules) {
        if (!first) sql << conjunction;
        sql << ruleToSQL(rule, params);
        first = false;
    }

    for (const auto& subGroup : rules.subGroups) {
        if (!first) sql << conjunction;
        std::string sub = generateSQL(subGroup, params);
        sql << "(" << sub.substr(std::string("SELECT * FROM tracks WHERE ").size()) << ")";
        first = false;
    }

    if (first) {
        sql << "1=1";
    }

    if (rules.maxResults > 0) {
        sql << " LIMIT " << rules.maxResults;
    }

    return sql.str();
}

std::string SmartPlaylist::ruleToSQL(const Models::SmartPlaylistRule& rule,
                                      std::vector<std::string>& params) const {
    std::string column = fieldToColumn(rule.field);
    return operatorToSQL(rule.operator_, column, rule.value, rule.value2, params);
}

std::string SmartPlaylist::fieldToColumn(Models::SmartPlaylistField field) const {
    switch (field) {
        case Models::SmartPlaylistField::BPM: return "bpm";
        case Models::SmartPlaylistField::Key: return "key";
        case Models::SmartPlaylistField::Genre: return "genre";
        case Models::SmartPlaylistField::Energy: return "energy";
        case Models::SmartPlaylistField::Rating: return "rating";
        case Models::SmartPlaylistField::Year: return "year";
        case Models::SmartPlaylistField::Artist: return "artist";
        case Models::SmartPlaylistField::Title: return "title";
        case Models::SmartPlaylistField::Album: return "album";
        case Models::SmartPlaylistField::Duration: return "duration";
        case Models::SmartPlaylistField::PlayCount: return "play_count";
        case Models::SmartPlaylistField::DateAdded: return "date_added";
        case Models::SmartPlaylistField::LastPlayed: return "last_played";
        case Models::SmartPlaylistField::Comment: return "comment";
        case Models::SmartPlaylistField::Label: return "label";
        case Models::SmartPlaylistField::Mood: return "mood";
        case Models::SmartPlaylistField::Danceability: return "danceability";
        case Models::SmartPlaylistField::Color: return "color";
        case Models::SmartPlaylistField::Source: return "source";
        case Models::SmartPlaylistField::FileFormat: return "file_format";
        case Models::SmartPlaylistField::BitRate: return "bit_rate";
        case Models::SmartPlaylistField::SampleRate: return "sample_rate";
        case Models::SmartPlaylistField::Grouping: return "grouping";
        case Models::SmartPlaylistField::CamelotKey: return "camelot_key";
        case Models::SmartPlaylistField::OpenKey: return "open_key";
        default: return "title";
    }
}

std::string SmartPlaylist::operatorToSQL(Models::SmartPlaylistOperator op, const std::string& column,
                                          const std::string& value, const std::string& value2,
                                          std::vector<std::string>& params) const {
    switch (op) {
        case Models::SmartPlaylistOperator::Equals:
            params.push_back(value);
            return column + " = ?";
        case Models::SmartPlaylistOperator::NotEquals:
            params.push_back(value);
            return column + " != ?";
        case Models::SmartPlaylistOperator::Contains:
            params.push_back("%" + value + "%");
            return column + " LIKE ?";
        case Models::SmartPlaylistOperator::NotContains:
            params.push_back("%" + value + "%");
            return column + " NOT LIKE ?";
        case Models::SmartPlaylistOperator::StartsWith:
            params.push_back(value + "%");
            return column + " LIKE ?";
        case Models::SmartPlaylistOperator::EndsWith:
            params.push_back("%" + value);
            return column + " LIKE ?";
        case Models::SmartPlaylistOperator::GreaterThan:
            params.push_back(value);
            return column + " > ?";
        case Models::SmartPlaylistOperator::LessThan:
            params.push_back(value);
            return column + " < ?";
        case Models::SmartPlaylistOperator::GreaterOrEqual:
            params.push_back(value);
            return column + " >= ?";
        case Models::SmartPlaylistOperator::LessOrEqual:
            params.push_back(value);
            return column + " <= ?";
        case Models::SmartPlaylistOperator::Between:
            params.push_back(value);
            params.push_back(value2);
            return column + " BETWEEN ? AND ?";
        case Models::SmartPlaylistOperator::IsEmpty:
            return "(" + column + " IS NULL OR " + column + " = '')";
        case Models::SmartPlaylistOperator::IsNotEmpty:
            return "(" + column + " IS NOT NULL AND " + column + " != '')";
        case Models::SmartPlaylistOperator::InLast: {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t seconds = 0;
            try { seconds = std::stoll(value) * 86400; } catch (...) { seconds = 0; }
            params.push_back(std::to_string(now - seconds));
            return column + " >= ?";
        }
        case Models::SmartPlaylistOperator::NotInLast: {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t seconds = 0;
            try { seconds = std::stoll(value) * 86400; } catch (...) { seconds = 0; }
            params.push_back(std::to_string(now - seconds));
            return column + " < ?";
        }
        default:
            params.push_back(value);
            return column + " = ?";
    }
}

} // namespace BeatMate::Services::Library
