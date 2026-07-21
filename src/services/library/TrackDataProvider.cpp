#include "TrackDataProvider.h"
#include "TrackDatabase.h"
#include "SmartPlaylistEngine.h"
#include "PlaylistSuggestionService.h"
#include "../export/PlaylistExportService.h"
#include "../djsoftware/SendToDJRouter.h"

#include <juce_events/juce_events.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <set>

namespace BeatMate::Services::Library {

TrackDataProvider::TrackDataProvider(TrackDatabase& db)
    : db_(db)
{
    spdlog::info("TrackDataProvider: Initialized");
}

std::vector<Models::Track> TrackDataProvider::getAllTracks()
{
    {
        std::lock_guard<std::mutex> lock(allTracksCacheMutex_);
        if (allTracksCacheValid_) return allTracksCache_;
    }
    auto rows = db_.getAllTracks();
    {
        std::lock_guard<std::mutex> lock(allTracksCacheMutex_);
        allTracksCache_ = rows;
        allTracksCacheValid_ = true;
    }
    return rows;
}

std::vector<Models::Track> TrackDataProvider::searchTracks(const std::string& query)
{
    if (query.empty()) return db_.getAllTracks();

    auto results = db_.searchFTS(query, 50000);
    if (!results.empty()) return results;

    std::string likeQuery = "%" + query + "%";
    return db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE title LIKE ? OR artist LIKE ? OR album LIKE ? OR genre LIKE ? ORDER BY date_added DESC",
        {likeQuery, likeQuery, likeQuery, likeQuery});
}

std::vector<Models::Track> TrackDataProvider::getTracksBySource(Models::TrackSource source)
{
    std::string sql = "SELECT * FROM tracks WHERE source = ? ORDER BY artist ASC, title ASC";
    std::vector<std::string> params;
    params.push_back(std::to_string(static_cast<int>(source)));
    return db_.getTracksByQuery(sql, params);
}

std::vector<Models::Track> TrackDataProvider::getTracksByFilter(
    float bpmMin, float bpmMax,
    const std::string& key,
    const std::string& genre,
    float energyMin, float energyMax,
    int ratingMin)
{
    std::string sql = "SELECT * FROM tracks WHERE 1=1";
    std::vector<std::string> params;

    if (bpmMin > 60 || bpmMax < 200)
    {
        sql += " AND (bpm = 0 OR bpm IS NULL OR (bpm >= ? AND bpm <= ?))";
        params.push_back(std::to_string(bpmMin));
        params.push_back(std::to_string(bpmMax));
    }

    if (!key.empty())
    {
        sql += " AND (key = ? OR camelot_key = ? OR key LIKE ? OR camelot_key LIKE ?)";
        params.push_back(key);
        params.push_back(key);
        params.push_back(key + "%");
        params.push_back(key + "%");
    }

    if (!genre.empty())
    {
        sql += " AND genre LIKE ? COLLATE NOCASE";
        params.push_back("%" + genre + "%");
    }

    if (energyMin > 1 || energyMax < 10)
    {
        sql += " AND (energy = 0 OR energy IS NULL OR (energy >= ? AND energy <= ?))";
        params.push_back(std::to_string(energyMin));
        params.push_back(std::to_string(energyMax));
    }

    if (ratingMin > 0)
    {
        sql += " AND rating >= ?";
        params.push_back(std::to_string(ratingMin));
    }

    sql += " ORDER BY artist ASC, title ASC";

    return db_.getTracksByQuery(sql, params);
}

std::vector<Models::Track> TrackDataProvider::getTracksByFilterWithSearch(
    const std::string& searchText,
    float bpmMin, float bpmMax,
    const std::string& key,
    const std::string& genre,
    const std::string& artist,
    float energyMin, float energyMax,
    int ratingMin)
{
    std::string sql = "SELECT * FROM tracks WHERE 1=1";
    std::vector<std::string> params;

    if (!searchText.empty())
    {
        std::string escaped;
        escaped.reserve(searchText.size());
        for (char c : searchText) {
            if (c == '%' || c == '_' || c == '\\') escaped += '\\';
            escaped += c;
        }

        sql += " AND (title LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR artist LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR album LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR genre LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR comment LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR label LIKE ? ESCAPE '\\' COLLATE NOCASE"
               " OR file_path LIKE ? ESCAPE '\\' COLLATE NOCASE)";
        std::string like = "%" + escaped + "%";
        for (int i = 0; i < 7; i++) params.push_back(like);
    }

    if (bpmMin > 60 || bpmMax < 200)
    {
        sql += " AND (bpm = 0 OR bpm IS NULL OR (bpm >= ? AND bpm <= ?))";
        params.push_back(std::to_string(bpmMin));
        params.push_back(std::to_string(bpmMax));
    }

    if (!key.empty())
    {
        sql += " AND (key = ? COLLATE NOCASE OR camelot_key = ? COLLATE NOCASE OR key LIKE ? COLLATE NOCASE)";
        params.push_back(key);
        params.push_back(key);
        params.push_back(key + "%");
    }

    if (!genre.empty())
    {
        sql += " AND genre LIKE ? COLLATE NOCASE";
        params.push_back("%" + genre + "%");
    }

    if (!artist.empty())
    {
        sql += " AND artist LIKE ? COLLATE NOCASE";
        params.push_back("%" + artist + "%");
    }

    if (energyMin > 1 || energyMax < 10)
    {
        sql += " AND (energy = 0 OR energy IS NULL OR (energy >= ? AND energy <= ?))";
        params.push_back(std::to_string(energyMin));
        params.push_back(std::to_string(energyMax));
    }

    if (ratingMin > 0)
    {
        sql += " AND rating >= ?";
        params.push_back(std::to_string(ratingMin));
    }

    sql += " ORDER BY artist COLLATE NOCASE ASC, title COLLATE NOCASE ASC";

    return db_.getTracksByQuery(sql, params);
}

Models::Track TrackDataProvider::getTrack(int64_t id)
{
    auto opt = db_.getTrack(id);
    return opt.value_or(Models::Track{});
}

std::vector<std::string> TrackDataProvider::getTrackTags(int64_t trackId)
{
    return db_.getTrackTags(trackId);
}

bool TrackDataProvider::setTrackTags(int64_t trackId, const std::vector<std::string>& tags)
{
    return db_.setTrackTags(trackId, tags);
}

std::vector<std::string> TrackDataProvider::getAllTags()
{
    return db_.getAllTags();
}

int64_t TrackDataProvider::addTrack(const Models::Track& track)
{
    auto id = db_.addTrack(track);
    if (id >= 0 && !batchMode_.load()) notifyDataChanged();
    return id;
}

void TrackDataProvider::updateTrack(const Models::Track& track)
{
    if (db_.updateTrack(track)) {
        if (!batchMode_.load())
            notifyDataChanged();
    }
}

int64_t TrackDataProvider::getTrackCount()
{
    return db_.getTrackCount();
}

void TrackDataProvider::beginBatch()
{
    batchMode_.store(true);
}

void TrackDataProvider::endBatch()
{
    batchMode_.store(false);
    notifyDataChanged();
}

void TrackDataProvider::deleteTrack(int64_t id)
{
    if (db_.deleteTrack(id)) notifyDataChanged();
}

int TrackDataProvider::getTotalTracks()
{
    return static_cast<int>(db_.getTrackCount());
}

int TrackDataProvider::getAnalyzedTracks()
{
    auto tracks = db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE analyzed = 1", {});
    return static_cast<int>(tracks.size());
}

std::vector<std::pair<std::string, int>> TrackDataProvider::getGenreDistribution()
{
    auto allTracks = db_.getAllTracks();
    std::map<std::string, int> genreMap;
    for (auto& t : allTracks)
    {
        if (!t.genre.empty())
            genreMap[t.genre]++;
    }

    std::vector<std::pair<std::string, int>> result(genreMap.begin(), genreMap.end());
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

std::vector<std::pair<std::string, int>> TrackDataProvider::getArtistDistribution()
{
    auto allTracks = db_.getAllTracks();
    std::map<std::string, int> artistMap;
    for (auto& t : allTracks)
    {
        if (!t.artist.empty())
            artistMap[t.artist]++;
    }

    std::vector<std::pair<std::string, int>> result(artistMap.begin(), artistMap.end());
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (result.size() > 100) result.resize(100);
    return result;
}

std::vector<Models::Track> TrackDataProvider::getRecentlyAdded(int limit)
{
    return db_.getTracksByQuery(
        "SELECT * FROM tracks ORDER BY date_added DESC LIMIT ?",
        {std::to_string(limit)});
}

std::vector<Models::Track> TrackDataProvider::getRecentlyPlayed(int limit)
{
    return db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT ?",
        {std::to_string(limit)});
}

std::vector<Models::Track> TrackDataProvider::getMostPlayed(int limit)
{
    return db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count > 0 ORDER BY play_count DESC LIMIT ?",
        {std::to_string(limit)});
}

std::vector<Models::Track> TrackDataProvider::getSuggestionsFor(const Models::Track& current, int limit)
{
    float bpmLow = current.bpm * 0.94f;
    float bpmHigh = current.bpm * 1.06f;

    auto candidates = db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE id != ? AND bpm BETWEEN ? AND ? ORDER BY play_count DESC LIMIT ?",
        {std::to_string(current.id),
         std::to_string(bpmLow),
         std::to_string(bpmHigh),
         std::to_string(limit * 3)});

    struct Scored { Models::Track track; float score; };
    std::vector<Scored> scored;

    for (auto& t : candidates)
    {
        float bpmScore = 1.0f - std::abs(static_cast<float>(t.bpm - current.bpm)) / (current.bpm * 0.06f);

        float keyScore = 0.3f;
        std::string refKey = current.camelotKey.empty() ? current.key : current.camelotKey;
        std::string candKey = t.camelotKey.empty() ? t.key : t.camelotKey;
        if (!refKey.empty() && !candKey.empty()) {
            if (refKey == candKey) {
                keyScore = 1.0f;
            } else if (refKey.size() >= 2 && candKey.size() >= 2) {
                try {
                    int n1 = std::stoi(refKey.substr(0, refKey.size() - 1));
                    int n2 = std::stoi(candKey.substr(0, candKey.size() - 1));
                    char l1 = refKey.back(), l2 = candKey.back();
                    if (n1 == n2 && l1 != l2) keyScore = 0.9f;           // Relative major/minor
                    else {
                        int diff = ((n2 - n1) + 12) % 12;
                        if ((diff == 1 || diff == 11) && l1 == l2) keyScore = 0.85f; // Adjacent on wheel
                        else if (diff == 7 && l1 == l2) keyScore = 0.7f;  // Dominant (energy boost)
                        else if (diff == 2 || diff == 10) keyScore = 0.5f; // 2-step jump
                    }
                } catch (...) {}
            }
        }

        float energyScore = 1.0f - std::abs(t.energy - current.energy) / 10.0f;
        float total = bpmScore * 0.35f + keyScore * 0.35f + energyScore * 0.3f;
        scored.push_back({t, total});
    }

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::vector<Models::Track> result;
    for (int i = 0; i < limit && i < static_cast<int>(scored.size()); ++i)
        result.push_back(scored[i].track);

    return result;
}

std::vector<Models::Track> TrackDataProvider::getCompatibleTracks(float bpm, const std::string& key, int limit)
{
    float bpmLow = bpm * 0.94f;
    float bpmHigh = bpm * 1.06f;

    if (!key.empty())
    {
        return db_.getTracksByQuery(
            "SELECT * FROM tracks WHERE bpm BETWEEN ? AND ? AND (key = ? OR camelot_key = ?) ORDER BY bpm LIMIT ?",
            {std::to_string(bpmLow), std::to_string(bpmHigh), key, key, std::to_string(limit)});
    }
    else
    {
        return db_.getTracksByQuery(
            "SELECT * FROM tracks WHERE bpm BETWEEN ? AND ? ORDER BY bpm LIMIT ?",
            {std::to_string(bpmLow), std::to_string(bpmHigh), std::to_string(limit)});
    }
}


std::vector<Models::Playlist> TrackDataProvider::getAllPlaylists()
{
    // Lecture par index de colonne (executeRead) : trackFromStatement mappe par
    std::vector<Models::Playlist> playlists;
    db_.executeRead(
        "SELECT id, name, description, is_smart FROM playlists "
        "WHERE parent_folder_id < 0 OR parent_folder_id IS NULL ORDER BY name",
        {},
        [&](sqlite3_stmt* stmt) {
            Models::Playlist p;
            p.id = sqlite3_column_int64(stmt, 0);
            if (auto t = sqlite3_column_text(stmt, 1)) p.name = reinterpret_cast<const char*>(t);
            if (auto t = sqlite3_column_text(stmt, 2)) p.description = reinterpret_cast<const char*>(t);
            p.isSmartPlaylist = sqlite3_column_int(stmt, 3) != 0;
            playlists.push_back(std::move(p));
        });
    return playlists;
}

std::vector<Models::Track> TrackDataProvider::getPlaylistTracks(int64_t playlistId)
{
    return db_.getTracksByQuery(
        "SELECT t.* FROM tracks t "
        "JOIN playlist_tracks pt ON t.id = pt.track_id "
        "WHERE pt.playlist_id = ? "
        "ORDER BY pt.position",
        {std::to_string(playlistId)});
}

int64_t TrackDataProvider::createPlaylist(const std::string& name)
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!db_.executeWrite(
            "INSERT INTO playlists (name, description, is_smart, created_at, modified_at) "
            "VALUES (?, '', 0, ?, ?)",
            {name, std::to_string(now), std::to_string(now)})) {
        spdlog::error("TrackDataProvider: createPlaylist INSERT failed for '{}'", name);
        return -1;
    }

    int64_t newId = db_.getLastInsertRowId();
    spdlog::info("TrackDataProvider: Created playlist '{}' with id={}", name, newId);
    notifyDataChanged();
    return newId;
}

bool TrackDataProvider::renamePlaylist(int64_t playlistId, const std::string& name)
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    bool ok = db_.executeWrite(
        "UPDATE playlists SET name = ?, modified_at = ? WHERE id = ?",
        {name, std::to_string(now), std::to_string(playlistId)});
    if (ok) notifyDataChanged();
    return ok;
}

bool TrackDataProvider::deletePlaylist(int64_t playlistId)
{
    bool ok = db_.executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ?", {std::to_string(playlistId)});
    ok = db_.executeWrite(
        "DELETE FROM playlists WHERE id = ?", {std::to_string(playlistId)}) && ok;
    spdlog::info("TrackDataProvider: Deleted playlist {} (ok={})", playlistId, ok);
    if (ok) notifyDataChanged();
    return ok;
}

bool TrackDataProvider::reorderPlaylist(int64_t playlistId, const std::vector<int64_t>& trackIds)
{
    bool ok = true;
    for (size_t i = 0; i < trackIds.size(); ++i) {
        ok = db_.executeWrite(
            "UPDATE playlist_tracks SET position = ? WHERE playlist_id = ? AND track_id = ?",
            {std::to_string(i), std::to_string(playlistId), std::to_string(trackIds[i])}) && ok;
    }
    if (ok) notifyDataChanged();
    return ok;
}

bool TrackDataProvider::setPlaylistTracks(int64_t playlistId, const std::vector<int64_t>& trackIds)
{
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    bool ok = db_.executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ?", {std::to_string(playlistId)});
    for (size_t i = 0; i < trackIds.size(); ++i) {
        ok = db_.executeWrite(
            "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) "
            "VALUES (?, ?, ?, ?)",
            {std::to_string(playlistId), std::to_string(trackIds[i]),
             std::to_string(i), std::to_string(now)}) && ok;
    }
    ok = db_.executeWrite(
        "UPDATE playlists SET modified_at = ? WHERE id = ?",
        {std::to_string(now), std::to_string(playlistId)}) && ok;
    if (ok) notifyDataChanged();
    return ok;
}

void TrackDataProvider::addToPlaylist(int64_t playlistId, int64_t trackId)
{
    int position = 0;
    db_.executeRead(
        "SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = ?",
        {std::to_string(playlistId)},
        [&](sqlite3_stmt* stmt) { position = sqlite3_column_int(stmt, 0); });

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_.executeWrite(
        "INSERT OR IGNORE INTO playlist_tracks (playlist_id, track_id, position, added_at) VALUES (?, ?, ?, ?)",
        {std::to_string(playlistId), std::to_string(trackId),
         std::to_string(position), std::to_string(now)});

    spdlog::info("TrackDataProvider: Added track {} to playlist {} at position {}", trackId, playlistId, position);
    notifyDataChanged();
}

void TrackDataProvider::removeFromPlaylist(int64_t playlistId, int64_t trackId)
{
    db_.executeWrite(
        "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?",
        {std::to_string(playlistId), std::to_string(trackId)});

    spdlog::info("TrackDataProvider: Removed track {} from playlist {}", trackId, playlistId);
    notifyDataChanged();
}

int64_t TrackDataProvider::createSmartPlaylist(const std::string& name,
                                               const Models::SmartPlaylistRuleGroup& rules)
{
    nlohmann::json rulesJson = rules;
    std::string rulesStr = rulesJson.dump();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!db_.executeWrite(
            "INSERT INTO playlists (name, description, is_smart, created_at, modified_at, smart_rules) "
            "VALUES (?, '', 1, ?, ?, ?)",
            {name, std::to_string(now), std::to_string(now), rulesStr})) {
        spdlog::error("TrackDataProvider: createSmartPlaylist INSERT failed for '{}'", name);
        return -1;
    }
    int64_t id = db_.getLastInsertRowId();
    if (id <= 0) return -1;

    SmartPlaylistEngine engine;
    auto matches = engine.evaluate(db_.getAllTracks(), rules);
    setPlaylistTracks(id, [&] {
        std::vector<int64_t> ids;
        for (auto& t : matches) ids.push_back(t.id);
        return ids;
    }());

    spdlog::info("TrackDataProvider: Created smart playlist '{}' (id={}, {} tracks)",
                 name, id, matches.size());
    notifyDataChanged();
    return id;
}

bool TrackDataProvider::updateSmartPlaylistRules(int64_t playlistId,
                                                 const Models::SmartPlaylistRuleGroup& rules)
{
    nlohmann::json rulesJson = rules;
    std::string rulesStr = rulesJson.dump();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    bool ok = db_.executeWrite(
        "UPDATE playlists SET smart_rules = ?, is_smart = 1, modified_at = ? WHERE id = ?",
        {rulesStr, std::to_string(now), std::to_string(playlistId)});
    if (!ok) return false;

    SmartPlaylistEngine engine;
    auto matches = engine.evaluate(db_.getAllTracks(), rules);
    std::vector<int64_t> ids;
    for (auto& t : matches) ids.push_back(t.id);
    setPlaylistTracks(playlistId, ids);
    notifyDataChanged();
    return true;
}

Models::SmartPlaylistRuleGroup TrackDataProvider::getSmartPlaylistRules(int64_t playlistId)
{
    std::string rulesStr;
    db_.executeRead(
        "SELECT smart_rules FROM playlists WHERE id = ? AND is_smart = 1",
        {std::to_string(playlistId)},
        [&](sqlite3_stmt* stmt) {
            if (auto t = sqlite3_column_text(stmt, 0))
                rulesStr = reinterpret_cast<const char*>(t);
        });

    Models::SmartPlaylistRuleGroup rules;
    if (rulesStr.empty()) return rules;
    try {
        rules = nlohmann::json::parse(rulesStr).get<Models::SmartPlaylistRuleGroup>();
    } catch (const std::exception& e) {
        spdlog::error("TrackDataProvider: getSmartPlaylistRules {} parse failed: {}", playlistId, e.what());
    }
    return rules;
}

std::vector<Models::Track> TrackDataProvider::refreshSmartPlaylist(int64_t playlistId)
{
    auto rules = getSmartPlaylistRules(playlistId);
    SmartPlaylistEngine engine;
    auto matches = engine.evaluate(db_.getAllTracks(), rules);
    std::vector<int64_t> ids;
    for (auto& t : matches) ids.push_back(t.id);
    setPlaylistTracks(playlistId, ids);
    notifyDataChanged();
    return matches;
}

int TrackDataProvider::countSmartMatches(const Models::SmartPlaylistRuleGroup& rules)
{
    SmartPlaylistEngine engine;
    return static_cast<int>(engine.evaluate(db_.getAllTracks(), rules).size());
}

namespace {
// shared_ptr non-proprietaire sur la TrackDatabase detenue par reference : le
std::shared_ptr<TrackDatabase> nonOwning(TrackDatabase& db) {
    return std::shared_ptr<TrackDatabase>(&db, [](TrackDatabase*) {});
}
}

std::vector<Models::Track> TrackDataProvider::suggestForPlaylist(
    const std::vector<int64_t>& playlistTrackIds, int maxSuggestions)
{
    PlaylistSuggestionService svc(nonOwning(db_));
    SuggestionCriteria criteria;
    criteria.excludeTrackIds = playlistTrackIds;
    criteria.maxSuggestions = maxSuggestions;
    criteria.minimumScore = 0.2f;

    std::vector<Models::Track> out;
    for (auto& s : svc.suggestForPlaylist(playlistTrackIds, criteria))
        out.push_back(s.track);
    return out;
}

std::vector<Models::Track> TrackDataProvider::suggestFillToDuration(
    const std::vector<int64_t>& playlistTrackIds, double targetMinutes)
{
    PlaylistSuggestionService svc(nonOwning(db_));
    std::vector<Models::Track> out;
    for (auto& s : svc.suggestFillToDuration(playlistTrackIds, targetMinutes))
        out.push_back(s.track);
    return out;
}

bool TrackDataProvider::exportPlaylistToFile(const std::vector<Models::Track>& tracks,
                                             const std::string& name,
                                             const std::string& outputPath,
                                             const std::string& format)
{
    Export::PlaylistExportService svc;
    Models::Playlist pl;
    pl.name = name;

    Export::PlaylistFormat fmt = Export::PlaylistFormat::M3U;
    if (format == "M3U8")      fmt = Export::PlaylistFormat::M3U8;
    else if (format == "PLS")  fmt = Export::PlaylistFormat::PLS;
    else if (format == "XSPF") fmt = Export::PlaylistFormat::XSPF;
    else                       fmt = Export::PlaylistFormat::M3U;

    return svc.exportPlaylist(pl, tracks, fmt, outputPath, false);
}

bool TrackDataProvider::sendTracksToDJ(const std::vector<Models::Track>& tracks,
                                       std::string& outMessage)
{
    if (tracks.empty()) {
        outMessage = "Aucun morceau a envoyer.";
        return false;
    }
    DJSoftware::SendToDJRouter router;
    auto res = router.sendTracks(tracks, DJSoftware::DJTarget::Auto);
    outMessage = res.message;
    return res.ok;
}


std::vector<Models::Track> TrackDataProvider::getUnanalyzedTracks()
{
    return db_.getTracksByQuery(
        "SELECT * FROM tracks WHERE analyzed = 0 ORDER BY date_added DESC", {});
}

void TrackDataProvider::saveAnalysis(int64_t trackId, const Models::TrackAnalysis& analysis)
{
    auto opt = db_.getTrack(trackId);
    if (!opt.has_value()) return;

    auto track = opt.value();
    track.analyzed = true;
    track.analyzedDate = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    track.bpm = analysis.bpm;
    track.key = analysis.key;
    track.setEnergy(analysis.energy * 10.0f);
    if (!analysis.mood.empty())
        track.mood = analysis.mood;
    if (analysis.loudness != 0.0f)
        track.lufs = analysis.loudness;
    if (!analysis.energySegments.empty())
        track.energySegments = nlohmann::json(analysis.energySegments).dump();
    if (analysis.bpmConfidence > 0.0f)
        track.bpmConfidence = analysis.bpmConfidence;
    if (analysis.keyConfidence > 0.0f)
        track.keyConfidence = analysis.keyConfidence;
    if (analysis.peakLevel != 0.0f)
        track.truePeak = analysis.peakLevel;
    if (analysis.loudnessRange > 0.0f)
        track.loudnessRange = analysis.loudnessRange;
    if (!analysis.sections.empty())
        track.sections = nlohmann::json(analysis.sections).dump();

    bool saved = db_.updateTrack(track);
    // BeatMate's own analysis is authoritative: clear the DJ-software
    if (saved) db_.setAnalysisSource(trackId, "");
    spdlog::info("saveAnalysis: id={} BPM={:.1f} Key='{}' Energy={:.1f} saved={}",
                 trackId, track.bpm, track.key, track.energy, saved);
    if (saved) notifyDataChanged();
}


std::vector<Models::CuePoint> TrackDataProvider::getCuePoints(int64_t trackId)
{
    return db_.getCuePoints(trackId);
}

std::map<int64_t, int> TrackDataProvider::getCueCounts()
{
    return db_.getCueCounts();
}

int64_t TrackDataProvider::saveCuePoint(const Models::CuePoint& cue)
{
    int64_t id;
    if (cue.id > 0)
    {
        db_.updateCuePoint(cue);
        id = cue.id;
    }
    else
    {
        id = db_.addCuePoint(cue);
    }
    // Cue changes are lightweight - no need to reload full track list
    notifyLightChanged();
    return id;
}

void TrackDataProvider::deleteCuePoint(int64_t cueId)
{
    db_.deleteCuePoint(cueId);
    notifyLightChanged();
}


void TrackDataProvider::recordPlay(int64_t trackId)
{
    auto opt = db_.getTrack(trackId);
    if (!opt.has_value()) return;

    auto track = opt.value();
    track.playCount++;
    track.lastPlayed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // Stat-only update: no notifyDataChanged() here. The global fan-out made
    db_.updateTrack(track);
}

void TrackDataProvider::recordPlayByPath(const std::string& filePath)
{
    auto opt = db_.getTrackByPath(filePath);
    if (!opt.has_value()) return;
    recordPlay(opt->id);
}


std::vector<Models::Track> TrackDataProvider::getTracksForSet(const std::string& genre, float bpmTarget, int limit)
{
    if (!genre.empty() && bpmTarget > 0)
    {
        float bpmLow = bpmTarget * 0.90f;
        float bpmHigh = bpmTarget * 1.10f;
        return db_.getTracksByQuery(
            "SELECT * FROM tracks WHERE genre LIKE ? AND bpm BETWEEN ? AND ? ORDER BY bpm LIMIT ?",
            {"%" + genre + "%", std::to_string(bpmLow), std::to_string(bpmHigh), std::to_string(limit)});
    }
    else if (!genre.empty())
    {
        return db_.getTracksByQuery(
            "SELECT * FROM tracks WHERE genre LIKE ? ORDER BY bpm LIMIT ?",
            {"%" + genre + "%", std::to_string(limit)});
    }
    else if (bpmTarget > 0)
    {
        float bpmLow = bpmTarget * 0.90f;
        float bpmHigh = bpmTarget * 1.10f;
        return db_.getTracksByQuery(
            "SELECT * FROM tracks WHERE bpm BETWEEN ? AND ? ORDER BY bpm LIMIT ?",
            {std::to_string(bpmLow), std::to_string(bpmHigh), std::to_string(limit)});
    }
    else
    {
        return db_.getTracksByQuery(
            "SELECT * FROM tracks ORDER BY bpm LIMIT ?",
            {std::to_string(limit)});
    }
}


void TrackDataProvider::invalidateAllTracksCache()
{
    std::lock_guard<std::mutex> lock(allTracksCacheMutex_);
    allTracksCacheValid_ = false;
    allTracksCache_.clear();
}

void TrackDataProvider::onDataChanged(DataChangedCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbacks_.push_back(std::move(cb));
}

void TrackDataProvider::notifyDataChanged()
{
    {
        std::lock_guard<std::mutex> lock(allTracksCacheMutex_);
        allTracksCacheValid_ = false;
    }
    std::vector<DataChangedCallback> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacksCopy = callbacks_;
    }
    // Always dispatch callbacks on the message thread to prevent
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        for (auto& cb : callbacksCopy) {
            if (cb) cb();
        }
    } else {
        juce::MessageManager::callAsync([callbacksCopy]() {
            for (auto& cb : callbacksCopy) {
                if (cb) cb();
            }
        });
    }
}

void TrackDataProvider::onLightChanged(LightChangedCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    lightCallbacks_.push_back(std::move(cb));
}

void TrackDataProvider::notifyLightChanged()
{
    std::vector<LightChangedCallback> callbacksCopy;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callbacksCopy = lightCallbacks_;
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        for (auto& cb : callbacksCopy) {
            if (cb) cb();
        }
    } else {
        juce::MessageManager::callAsync([callbacksCopy]() {
            for (auto& cb : callbacksCopy) {
                if (cb) cb();
            }
        });
    }
}

} // namespace BeatMate::Services::Library
