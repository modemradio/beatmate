#include "SessionHistoryRecorder.h"

#include "../../models/Track.h"
#include "../djsoftware/UnifiedDJHistory.h"

#include <sqlite3.h>
#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace BeatMate::Services::History {

namespace {

int64_t nowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool extractJsonString(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    std::string value;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            switch (next) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '"': value.push_back('"');  break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/');  break;
                default: value.push_back(next);  break;
            }
            pos += 2;
        } else {
            value.push_back(json[pos]);
            ++pos;
        }
    }
    out = std::move(value);
    return true;
}

} // namespace

SessionHistoryRecorder::SessionHistoryRecorder(
    std::shared_ptr<Services::Library::TrackDatabase> db)
    : m_db(std::move(db)) {}

SessionHistoryRecorder::~SessionHistoryRecorder() {
    if (m_importThread.joinable()) m_importThread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentSession.has_value()) {
        m_currentSession->endedAt = nowUnixSeconds();
        m_currentSession.reset();
    }
}

std::string SessionHistoryRecorder::jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

std::string SessionHistoryRecorder::buildContextJson(const std::string& userContext) const {
    // m_mutex must already be held by caller (or called single-threadedly).
    std::ostringstream oss;
    oss << "{";
    if (m_currentSession.has_value()) {
        oss << "\"session\":\"" << jsonEscape(m_currentSession->sessionId) << "\","
            << "\"name\":\""    << jsonEscape(m_currentSession->sessionName) << "\","
            << "\"venue\":\""   << jsonEscape(m_currentSession->venue) << "\"";
    }
    if (!userContext.empty()) {
        if (m_currentSession.has_value()) oss << ",";
        const bool looksLikeObject =
            !userContext.empty() && userContext.front() == '{' && userContext.back() == '}';
        if (looksLikeObject && userContext.size() >= 2) {
            oss << userContext.substr(1, userContext.size() - 2);
        } else {
            oss << "\"meta\":\"" << jsonEscape(userContext) << "\"";
        }
    }
    oss << "}";
    return oss.str();
}

bool SessionHistoryRecorder::parseSessionFromContext(const std::string& json,
                                                     std::string& outSessionId,
                                                     std::string& outName,
                                                     std::string& outVenue) {
    outSessionId.clear();
    outName.clear();
    outVenue.clear();
    if (!extractJsonString(json, "session", outSessionId) || outSessionId.empty()) {
        return false;
    }
    extractJsonString(json, "name",  outName);
    extractJsonString(json, "venue", outVenue);
    return true;
}

std::string SessionHistoryRecorder::startSession(const std::string& name,
                                                 const std::string& venue) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SessionInfo info;
    info.sessionId   = juce::Uuid().toString().toStdString();
    info.sessionName = name;
    info.venue       = venue;
    info.startedAt   = nowUnixSeconds();
    info.endedAt     = 0;

    m_currentSession = info;
    return info.sessionId;
}

void SessionHistoryRecorder::endSession() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentSession.has_value()) {
        m_currentSession->endedAt = nowUnixSeconds();
        m_currentSession.reset();
    }
}

std::optional<SessionInfo> SessionHistoryRecorder::currentSession() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentSession;
}

bool SessionHistoryRecorder::recordPlay(int64_t trackId, const std::string& context) {
    std::function<void(int64_t)> cb;
    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_currentSession.has_value()) return false;
    if (!m_db)                          return false;
    if (trackId <= 0)                   return false;

    const std::string ctxJson = buildContextJson(context);
    const int64_t playedAt    = nowUnixSeconds();

    int rc = SQLITE_ERROR;
    {
        std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
        if (!m_db->isOpen_ || !m_db->db_) return false;

        const char* sql =
            "INSERT INTO play_history (track_id, played_at, context) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_int64(stmt, 1, trackId);
        sqlite3_bind_int64(stmt, 2, playedAt);
        sqlite3_bind_text(stmt, 3, ctxJson.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    const bool ok = (rc == SQLITE_DONE);
    if (ok) cb = m_onPlayRecorded;
    lock.unlock();
    if (ok && cb) {
        try { cb(trackId); }
        catch (...) { /* swallow: callback must not break the recorder */ }
    }
    return ok;
}

namespace {

std::string normalizePathKey(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\\') c = '/';
        o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return o;
}

std::string normalizeTextKey(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    bool space = false;
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u == ' ' || u == '\t') {
            if (!o.empty() && !space) o.push_back(' ');
            space = true;
        } else {
            o.push_back(static_cast<char>(std::tolower(u)));
            space = false;
        }
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

} // namespace

int SessionHistoryRecorder::importExternalPlayHistory(
    const std::vector<Services::DJSoftware::PlayedTrack>& played) {
    if (!m_db || played.empty()) return 0;

    std::vector<Models::Track> all;
    try { all = m_db->getAllTracks(); }
    catch (...) { return 0; }
    if (all.empty()) return 0;

    std::unordered_map<std::string, int64_t> byPath;
    std::unordered_map<std::string, int64_t> byArtistTitle;
    byPath.reserve(all.size());
    byArtistTitle.reserve(all.size());
    for (const auto& t : all) {
        if (t.id <= 0) continue;
        if (!t.filePath.empty())
            byPath.emplace(normalizePathKey(t.filePath), t.id);
        if (!t.artist.empty() && !t.title.empty())
            byArtistTitle.emplace(
                normalizeTextKey(t.artist) + "\x1f" + normalizeTextKey(t.title), t.id);
    }

    std::unordered_set<std::string> existing;
    {
        std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
        if (!m_db->isOpen_ || !m_db->db_) return 0;
        const char* q =
            "SELECT track_id, played_at FROM play_history WHERE context LIKE 'import:%';";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(m_db->db_, q, -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                existing.insert(std::to_string(sqlite3_column_int64(st, 0)) + "|" +
                                std::to_string(sqlite3_column_int64(st, 1)));
            }
            sqlite3_finalize(st);
        }
    }

    int inserted = 0;
    {
        std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
        if (!m_db->isOpen_ || !m_db->db_) return 0;

        sqlite3_exec(m_db->db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        const char* ins =
            "INSERT INTO play_history (track_id, played_at, context) VALUES (?, ?, ?);";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(m_db->db_, ins, -1, &st, nullptr) == SQLITE_OK) {
            for (const auto& p : played) {
                if (p.playedAtUnix <= 0) continue;

                int64_t id = 0;
                if (!p.filePath.empty()) {
                    auto it = byPath.find(normalizePathKey(p.filePath));
                    if (it != byPath.end()) id = it->second;
                }
                if (id <= 0 && !p.artist.empty() && !p.title.empty()) {
                    auto it = byArtistTitle.find(
                        normalizeTextKey(p.artist) + "\x1f" + normalizeTextKey(p.title));
                    if (it != byArtistTitle.end()) id = it->second;
                }
                if (id <= 0) continue;

                std::string key = std::to_string(id) + "|" + std::to_string(p.playedAtUnix);
                if (!existing.insert(key).second) continue;

                const std::string ctx =
                    "import:" + (p.source.empty() ? std::string("dj") : p.source);
                sqlite3_reset(st);
                sqlite3_clear_bindings(st);
                sqlite3_bind_int64(st, 1, id);
                sqlite3_bind_int64(st, 2, p.playedAtUnix);
                sqlite3_bind_text(st, 3, ctx.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) == SQLITE_DONE) ++inserted;
            }
            sqlite3_finalize(st);
        }
        sqlite3_exec(m_db->db_, "COMMIT;", nullptr, nullptr, nullptr);
    }

    if (inserted > 0)
        spdlog::info("[SessionHistory] seeded {} external play(s) into play_history", inserted);
    else
        spdlog::info("[SessionHistory] external history: nothing new to seed");
    return inserted;
}

void SessionHistoryRecorder::importExternalHistoryAsync(
    Services::DJSoftware::UnifiedDJHistory& unified, std::function<void(int)> onDone) {
    if (m_importThread.joinable()) m_importThread.join();
    m_importThread = std::thread([this, &unified, cb = std::move(onDone)]() {
        int n = 0;
        try {
            auto played = unified.getRecent(8000);
            n = importExternalPlayHistory(played);
        } catch (const std::exception& e) {
            spdlog::warn("[SessionHistory] async import threw: {}", e.what());
        } catch (...) {
            spdlog::warn("[SessionHistory] async import threw (unknown)");
        }
        if (cb) { try { cb(n); } catch (...) {} }
    });
}

void SessionHistoryRecorder::setOnPlayRecorded(std::function<void(int64_t)> cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onPlayRecorded = std::move(cb);
}

std::vector<PlayEvent> SessionHistoryRecorder::getSessionEvents(
    const std::string& sessionId) const {
    std::vector<PlayEvent> out;
    if (!m_db || sessionId.empty()) return out;

    std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
    if (!m_db->isOpen_ || !m_db->db_) return out;

    const char* sql =
        "SELECT track_id, played_at, context FROM play_history "
        "WHERE context LIKE ? ORDER BY played_at ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }

    const std::string like = std::string("%\"session\":\"") + sessionId + "\"%";
    sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayEvent ev;
        ev.trackId  = sqlite3_column_int64(stmt, 0);
        ev.playedAt = sqlite3_column_int64(stmt, 1);
        const unsigned char* ctx = sqlite3_column_text(stmt, 2);
        ev.context  = ctx ? reinterpret_cast<const char*>(ctx) : "";
        ev.sessionId = sessionId;
        out.push_back(std::move(ev));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<SessionInfo> SessionHistoryRecorder::listSessions() const {
    std::vector<SessionInfo> out;
    if (!m_db) return out;

    std::unordered_map<std::string, SessionInfo> byId;

    {
        std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
        if (!m_db->isOpen_ || !m_db->db_) return out;

        const char* sql =
            "SELECT played_at, context FROM play_history "
            "WHERE context LIKE '%\"session\"%' ORDER BY played_at ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return out;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int64_t playedAt = sqlite3_column_int64(stmt, 0);
            const unsigned char* ctx = sqlite3_column_text(stmt, 1);
            if (!ctx) continue;
            const std::string json = reinterpret_cast<const char*>(ctx);

            std::string sid, name, venue;
            if (!parseSessionFromContext(json, sid, name, venue)) continue;

            auto it = byId.find(sid);
            if (it == byId.end()) {
                SessionInfo info;
                info.sessionId   = sid;
                info.sessionName = name;
                info.venue       = venue;
                info.startedAt   = playedAt;
                info.endedAt     = playedAt;
                byId.emplace(sid, std::move(info));
            } else {
                it->second.endedAt = playedAt;
                if (it->second.sessionName.empty()) it->second.sessionName = name;
                if (it->second.venue.empty())       it->second.venue = venue;
            }
        }
        sqlite3_finalize(stmt);
    }

    out.reserve(byId.size());
    for (auto& kv : byId) out.push_back(std::move(kv.second));

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentSession.has_value()) {
            const auto& cur = *m_currentSession;
            auto it = std::find_if(out.begin(), out.end(),
                [&](const SessionInfo& s) { return s.sessionId == cur.sessionId; });
            if (it == out.end()) {
                out.push_back(cur);
            } else {
                if (cur.startedAt > 0) it->startedAt = std::min(it->startedAt, cur.startedAt);
                if (it->sessionName.empty()) it->sessionName = cur.sessionName;
                if (it->venue.empty())       it->venue = cur.venue;
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.startedAt < b.startedAt;
              });
    return out;
}

std::vector<PlayEvent> SessionHistoryRecorder::currentSessionEvents() const {
    std::string sid;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_currentSession.has_value()) return {};
        sid = m_currentSession->sessionId;
    }
    return getSessionEvents(sid);
}

SessionHistoryRecorder::SessionSummary
SessionHistoryRecorder::summary(const std::string& sessionId) const {
    SessionSummary s;
    s.sessionId = sessionId;

    auto events = getSessionEvents(sessionId);
    if (events.empty()) return s;

    std::unordered_map<int64_t, Models::Track> trackById;
    if (m_db && m_db->isOpen_) {
        std::string ids;
        ids.reserve(events.size() * 8);
        for (size_t i = 0; i < events.size(); ++i) {
            if (i) ids.push_back(',');
            ids += std::to_string(events[i].trackId);
        }
        try {
            auto rows = m_db->getTracksByQuery(
                "SELECT * FROM tracks WHERE id IN (" + ids + ")");
            for (auto& t : rows) trackById.emplace(t.id, std::move(t));
        } catch (...) {
            // Fall through with whatever we have — summary degrades gracefully.
        }
    }

    s.trackCount  = static_cast<int>(events.size());
    s.durationSec = std::max<int64_t>(0, events.back().playedAt - events.front().playedAt);

    {
        std::string sid2, name, venue;
        if (parseSessionFromContext(events.front().context, sid2, name, venue))
            s.sessionName = name;
    }

    auto parseCamelotPair = [](const std::string& key, int& num, int& mode) -> bool {
        if (key.size() < 2) return false;
        try {
            num  = std::stoi(key.substr(0, key.size() - 1));
            char c = (char) std::toupper((unsigned char) key.back());
            if (c != 'A' && c != 'B') return false;
            mode = (c == 'B') ? 1 : 0;
            return num >= 1 && num <= 12;
        } catch (...) { return false; }
    };

    double bpmSum = 0.0, energySum = 0.0;
    int bpmCount = 0, energyCount = 0;
    std::unordered_map<std::string, int> genreCounts;
    std::unordered_map<std::string, int> artistCounts;

    double prevBpm = 0.0;
    int prevNum = -1, prevMode = -1;

    for (const auto& ev : events) {
        auto it = trackById.find(ev.trackId);
        if (it == trackById.end()) {
            s.camelotJourney.push_back("");
            continue;
        }
        const auto& t = it->second;

        if (t.bpm > 0.0)    { bpmSum += t.bpm; ++bpmCount; }
        if (t.energy > 0.0f){ energySum += t.energy; ++energyCount; }
        if (!t.genre.empty())  genreCounts[t.genre]++;
        if (!t.artist.empty()) artistCounts[t.artist]++;

        const std::string camelot = !t.camelotKey.empty() ? t.camelotKey : t.key;
        s.camelotJourney.push_back(camelot);

        if (prevBpm > 0.0 && t.bpm > 0.0)
            s.biggestBpmJump = std::max(s.biggestBpmJump, std::abs(t.bpm - prevBpm));
        if (t.bpm > 0.0) prevBpm = t.bpm;

        int n = 0, m = 0;
        if (parseCamelotPair(camelot, n, m)) {
            if (prevNum >= 0) {
                int d = std::abs(prevNum - n);
                if (d > 6) d = 12 - d;
                if (prevMode != m) d += 1;
                s.biggestKeyJump = std::max(s.biggestKeyJump, d);
            }
            prevNum = n; prevMode = m;
        }
    }

    s.avgBpm    = bpmCount    > 0 ? bpmSum    / bpmCount    : 0.0;
    s.avgEnergy = energyCount > 0 ? energySum / energyCount : 0.0;

    int bestGenre = 0;
    for (const auto& kv : genreCounts)
        if (kv.second > bestGenre) { bestGenre = kv.second; s.dominantGenre = kv.first; }

    s.topArtists.assign(artistCounts.begin(), artistCounts.end());
    std::sort(s.topArtists.begin(), s.topArtists.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (s.topArtists.size() > 10) s.topArtists.resize(10);

    return s;
}

std::vector<int64_t> SessionHistoryRecorder::mostPlayed(int topN) const {
    std::vector<int64_t> out;
    if (!m_db || topN <= 0) return out;

    std::lock_guard<std::recursive_mutex> dbLock(m_db->mutex_);
    if (!m_db->isOpen_ || !m_db->db_) return out;

    const char* sql =
        "SELECT track_id, COUNT(*) AS plays FROM play_history "
        "GROUP BY track_id ORDER BY plays DESC, MAX(played_at) DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int(stmt, 1, topN);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return out;
}

} // namespace BeatMate::Services::History
