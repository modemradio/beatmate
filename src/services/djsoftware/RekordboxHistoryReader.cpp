#include "RekordboxHistoryReader.h"
#include "rekordbox/RekordboxCipher.h"
#include "rekordbox/RekordboxLiveWatcher.h"
#include "rekordbox/RekordboxProLink.h"
#include "rekordbox/RekordboxWalWatcher.h"
#include "rekordbox/RkbxLinkSubprocess.h"
#include "rekordbox/RekordboxPlaylistsXmlWatcher.h"
#include "rekordbox/RekordboxAnlzWatcher.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <array>
#include <string>

namespace BeatMate::Services::DJSoftware {

namespace {

static constexpr const char* kRekordboxMasterKey =
    "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";

static std::string tonalityToCamelot(int tonality)
{
    static const std::array<const char*, 24> table = {
        "8B","3B","10B","5B","12B","7B","2B","9B","4B","11B","6B","1B",
        "5A","12A","7A","2A","9A","4A","11A","6A","1A","8A","3A","10A"
    };
    int idx = tonality;
    if (idx >= 1 && idx <= 24) idx -= 1;
    if (idx < 0 || idx >= (int) table.size()) return {};
    return table[(size_t) idx];
}

static std::string masterDbPath()
{
#ifdef _WIN32
    auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                       .getParentDirectory()   // AppData\Local -> AppData
                       .getChildFile("Roaming");
#else
    auto appData = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library");
#endif
    juce::File base = appData.getChildFile("Pioneer");
    if (!base.isDirectory()) {
        base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Pioneer");
    }
    auto candidate = base.getChildFile("rekordbox6").getChildFile("master.db");
    if (!candidate.existsAsFile()) {
        candidate = base.getChildFile("rekordbox").getChildFile("master.db");
    }
    return candidate.getFullPathName().toStdString();
}

static std::string colStr(sqlite3_stmt* st, int i) {
    const unsigned char* txt = sqlite3_column_text(st, i);
    return txt ? reinterpret_cast<const char*>(txt) : std::string{};
}

static std::string copyMasterDbToTemp(const std::string& srcPath)
{
    juce::File src(srcPath);
    if (!src.existsAsFile()) return srcPath;
    juce::File dst = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("beatmate_master_live.db");
    dst.deleteFile();
    src.copyFileTo(dst);
    juce::File walSrc(srcPath + "-wal");
    juce::File shmSrc(srcPath + "-shm");
    if (walSrc.existsAsFile()) {
        juce::File walDst(dst.getFullPathName() + "-wal");
        walDst.deleteFile();
        walSrc.copyFileTo(walDst);
    }
    if (shmSrc.existsAsFile()) {
        juce::File shmDst(dst.getFullPathName() + "-shm");
        shmDst.deleteFile();
        shmSrc.copyFileTo(shmDst);
    }
    return dst.getFullPathName().toStdString();
}

static sqlite3* openMasterDb()
{
    const std::string origPath = masterDbPath();
    juce::File dbFile(origPath);
    if (!dbFile.existsAsFile()) {
        spdlog::debug("[RB] master.db not found at {}", origPath);
        return nullptr;
    }
    spdlog::info("[RB] master.db candidate at {}", origPath);

    const std::string path = copyMasterDbToTemp(origPath);

    sqlite3* plain = nullptr;
    std::string plainUri = "file:" + path + "?mode=ro";
    if (sqlite3_open_v2(plainUri.c_str(), &plain,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) == SQLITE_OK) {
        sqlite3_stmt* st = nullptr;
        int rc = sqlite3_prepare_v2(plain, "SELECT count(*) FROM sqlite_master;",
                                    -1, &st, nullptr);
        if (rc == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
            sqlite3_finalize(st);
            spdlog::info("[RB] master.db opened plaintext (unencrypted)");
            return plain;
        }
        if (st) sqlite3_finalize(st);
        sqlite3_close(plain);
    }

    sqlite3* enc = nullptr;
    int rc = Rekordbox::RekordboxCipher::openEncrypted(path, kRekordboxMasterKey, &enc);
    if (rc != SQLITE_OK || !enc) {
        spdlog::warn("[RB] could not unlock master.db (final rc={}) at {}", rc, path);
        return nullptr;
    }
    return enc;
}

static double normaliseBpm(double raw) {
    if (raw > 1000.0) return raw / 100.0;
    return raw;
}

static bool tableExists(sqlite3* db, const char* name)
{
    const char* sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    bool found = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return found;
}

static std::vector<PlayedTrack> queryHistory(sqlite3* db, int limit)
{
    std::vector<PlayedTrack> result;
    const char* historyTable = "djmdSongHistory";
    if (!tableExists(db, historyTable)) {
        spdlog::warn("[RekordboxHistoryReader] djmdSongHistory not found; "
                     "schema may have changed. Searching for alternate history table...");
        sqlite3_stmt* st = nullptr;
        const char* q = "SELECT name FROM sqlite_master WHERE type='table' "
                        "AND name LIKE 'djmd%%History%%' LIMIT 1;";
        if (sqlite3_prepare_v2(db, q, -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                auto txt = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
                if (txt) {
                    static thread_local std::string alt; alt = txt;
                    historyTable = alt.c_str();
                    spdlog::info("[RekordboxHistoryReader] using alternate history table '{}'",
                                 historyTable);
                }
            }
            sqlite3_finalize(st);
        }
    }

    std::string sqlStr =
        "SELECT c.FolderPath, c.Title, a.Name, c.BPM, c.Rating, c.Length, "
        "       sh.created_at "
        "FROM " + std::string(historyTable) + " sh "
        "LEFT JOIN djmdContent c ON sh.ContentID = c.ID "
        "LEFT JOIN djmdArtist  a ON c.ArtistID  = a.ID "
        "WHERE sh.rb_local_deleted = 0 "
        "ORDER BY sh.created_at DESC "
        "LIMIT ?;";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sqlStr.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        std::string sqlNoDel =
            "SELECT c.FolderPath, c.Title, a.Name, c.BPM, c.Rating, c.Length, "
            "       sh.created_at "
            "FROM " + std::string(historyTable) + " sh "
            "LEFT JOIN djmdContent c ON sh.ContentID = c.ID "
            "LEFT JOIN djmdArtist  a ON c.ArtistID  = a.ID "
            "ORDER BY sh.created_at DESC "
            "LIMIT ?;";
        if (sqlite3_prepare_v2(db, sqlNoDel.c_str(), -1, &st, nullptr) != SQLITE_OK) {
            spdlog::warn("[RekordboxHistoryReader] prepare history failed: {}",
                         sqlite3_errmsg(db));
            return {};
        }
        spdlog::info("[RekordboxHistoryReader] falling back to schema without rb_local_deleted");
    }
    sqlite3_bind_int(st, 1, limit > 0 ? limit : 500);

    while (sqlite3_step(st) == SQLITE_ROW) {
        PlayedTrack pt;
        pt.filePath   = colStr(st, 0);
        pt.title      = colStr(st, 1);
        pt.artist     = colStr(st, 2);
        pt.bpm        = normaliseBpm(sqlite3_column_double(st, 3));
        pt.rating     = sqlite3_column_int(st, 4);
        pt.durationSec = sqlite3_column_double(st, 5);
        std::string when = colStr(st, 6);
        if (when.size() > 10 && when[10] == ' ') when[10] = 'T';
        auto plusPos = when.find_first_of("+-", 19);
        if (plusPos != std::string::npos && plusPos > 10) when.resize(plusPos);
        auto dotPos = when.find('.');
        if (dotPos != std::string::npos) when.resize(dotPos);
        juce::Time t = juce::Time::fromISO8601(juce::String(when));
        pt.playedAtUnix = t.toMilliseconds() > 0 ? t.toMilliseconds() / 1000 : 0;
        pt.source = "Rekordbox";
        result.push_back(std::move(pt));
    }
    sqlite3_finalize(st);
    return result;
}

static Rekordbox::RekordboxWalWatcher& sharedWalWatcher()
{
    static Rekordbox::RekordboxWalWatcher w;
    static bool started = false;
    if (!started) { started = w.start(); }
    return w;
}

static Rekordbox::RekordboxPlaylistsXmlWatcher& sharedPlaylistsXmlWatcher()
{
    static Rekordbox::RekordboxPlaylistsXmlWatcher w;
    static bool started = false;
    if (!started) { started = w.start(); }
    return w;
}

static Rekordbox::RekordboxAnlzWatcher& sharedAnlzWatcher()
{
    static Rekordbox::RekordboxAnlzWatcher w;
    static bool started = false;
    if (!started) { started = w.start(); }
    return w;
}

namespace {
struct PlaylistsXmlEagerStart {
    PlaylistsXmlEagerStart() {
        sharedPlaylistsXmlWatcher();
        sharedAnlzWatcher();
    }
};
static PlaylistsXmlEagerStart _rekordboxPlaylistsXmlEagerStart;
}

namespace {
struct WalWatcherEagerStart {
    WalWatcherEagerStart() { sharedWalWatcher(); }
};
static WalWatcherEagerStart _rekordboxWalWatcherEagerStart;
}

static Rekordbox::RkbxLinkSubprocess& sharedRkbxLink()
{
    static Rekordbox::RkbxLinkSubprocess r;
    static bool started = false;
    if (!started) { started = r.start(9000); }
    return r;
}

} // namespace

std::vector<PlayedTrack> RekordboxHistoryReader::readRecentHistory(int maxTracks)
{
    (void) sharedWalWatcher();

    sqlite3* db = openMasterDb();
    if (!db) return {};

    auto result = queryHistory(db, maxTracks > 0 ? maxTracks : 500);
    sqlite3_close(db);
    spdlog::info("[RekordboxHistoryReader] read {} history rows from master.db",
                 result.size());
    return result;
}

std::optional<PlayedTrack> RekordboxHistoryReader::readNowPlaying()
{
    // Stratégie A00 (temps réel, mémoire) : sous-processus rkbx_link.
    Rekordbox::RkbxLinkSubprocess& rkbx = sharedRkbxLink();
    if (rkbx.isHealthy()) {
        if (auto pt = rkbx.readNowPlaying()) {
            spdlog::info("[RekordboxHistoryReader] now playing (rkbx_link): '{}' - '{}' ({:.2f} BPM)",
                         pt->artist, pt->title, pt->bpm);
            return pt;
        }
    }

    // Stratégie A0a (temps réel) : watcher fichiers USBANLZ.
    {
        auto& anlzW = sharedAnlzWatcher();
        if (auto pt = anlzW.readNowPlaying()) {
            spdlog::info("[RekordboxHistoryReader] now playing (anlz): '{}' - '{}' ({:.2f} BPM)",
                         pt->artist, pt->title, pt->bpm);
            return pt;
        }
    }

    // Stratégie A0b (temps réel) : watcher masterPlaylists6.xml.
    {
        auto& xmlW = sharedPlaylistsXmlWatcher();
        if (auto pt = xmlW.readNowPlaying()) {
            spdlog::info("[RekordboxHistoryReader] now playing (xml): '{}' - '{}' ({:.2f} BPM)",
                         pt->artist, pt->title, pt->bpm);
            return pt;
        }
    }

    // Stratégie A0 (temps réel) : watcher WAL.
    Rekordbox::RekordboxWalWatcher& walWatcher = sharedWalWatcher();
    if (auto pt = walWatcher.readNowPlaying()) {
        spdlog::info("[RekordboxHistoryReader] now playing (WAL watcher): '{}' - '{}' ({:.2f} BPM)",
                     pt->artist, pt->title, pt->bpm);
        return pt;
    }

    // Stratégie A (temps réel) : écoute passive PRO DJ LINK.
    static Rekordbox::RekordboxProLink prolink;
    static bool prolinkStarted = false;
    if (!prolinkStarted) { prolinkStarted = prolink.start(); }
    if (auto pt = prolink.readNowPlaying()) {
        if (sqlite3* db = openMasterDb()) {
            int64_t tid = 0;
            try { tid = std::stoll(pt->title.substr(pt->title.find('#') + 1)); } catch (...) {}
            if (tid > 0) {
                sqlite3_stmt* st = nullptr;
                if (sqlite3_prepare_v2(db,
                        "SELECT c.Title, a.Name, c.BPM FROM djmdContent c "
                        "LEFT JOIN djmdArtist a ON c.ArtistID=a.ID WHERE c.ID=?;",
                        -1, &st, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(st, 1, tid);
                    if (sqlite3_step(st) == SQLITE_ROW) {
                        const unsigned char* title  = sqlite3_column_text(st, 0);
                        const unsigned char* artist = sqlite3_column_text(st, 1);
                        if (title)  pt->title  = reinterpret_cast<const char*>(title);
                        if (artist) pt->artist = reinterpret_cast<const char*>(artist);
                    }
                    sqlite3_finalize(st);
                }
            }
            sqlite3_close(db);
        }
        spdlog::info("[RekordboxHistoryReader] now playing (ProDJ Link): '{}' - '{}' ({:.2f} BPM)",
                     pt->artist, pt->title, pt->bpm);
        return pt;
    }

    if (sqlite3* db = openMasterDb()) {
        auto latest = queryHistory(db, 1);
        sqlite3_close(db);
        if (!latest.empty()) {
            const auto& t = latest.front();
            const int64_t now = static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            const int64_t ageSec = (t.playedAtUnix > 0) ? (now - t.playedAtUnix) : 99999;
            if (ageSec < 90) {
                spdlog::info("[RekordboxHistoryReader] now playing (master.db, age {}s): '{}' - '{}' ({:.2f} BPM)",
                             ageSec, t.artist, t.title, t.bpm);
                return t;
            }
            spdlog::debug("[RekordboxHistoryReader] master.db latest row too old ({}s), ignoring", ageSec);
        }
    } else {
        spdlog::info("[RekordboxHistoryReader] master.db unavailable - live watcher only");
    }

    static thread_local Rekordbox::RekordboxLiveWatcher watcher;
    if (auto pt = watcher.readNowPlaying()) {
        spdlog::info("[RekordboxHistoryReader] now playing (live watcher): '{}' - '{}'",
                     pt->artist, pt->title);
        return pt;
    }

    static bool s_seededOnce = false;
    if (!s_seededOnce) {
        if (sqlite3* db = openMasterDb()) {
            auto latest = queryHistory(db, 1);
            sqlite3_close(db);
            if (!latest.empty()) {
                s_seededOnce = true;
                const auto& t = latest.front();
                spdlog::info("[RekordboxHistoryReader] now playing (seed master.db latest): '{}' - '{}' ({:.2f} BPM)",
                             t.artist, t.title, t.bpm);
                return t;
            }
        }
    }

    spdlog::debug("[RekordboxHistoryReader] readNowPlaying: no signal from DB nor watcher");
    return std::nullopt;
}

namespace {

static uint32_t parseCueColor(sqlite3_stmt* st, int col)
{
    if (sqlite3_column_type(st, col) == SQLITE_INTEGER) {
        return static_cast<uint32_t>(sqlite3_column_int64(st, col)) & 0x00FFFFFFu;
    }
    const unsigned char* t = sqlite3_column_text(st, col);
    if (!t) return 0;
    juce::String s(reinterpret_cast<const char*>(t));
    s = s.replace("#", "");
    if (s.isEmpty()) return 0;
    return static_cast<uint32_t>(s.getHexValue32()) & 0x00FFFFFFu;
}

static juce::String colJStr(sqlite3_stmt* st, int i) {
    const unsigned char* t = sqlite3_column_text(st, i);
    return t ? juce::String(juce::CharPointer_UTF8(reinterpret_cast<const char*>(t)))
             : juce::String();
}

static std::vector<RekordboxCue> queryCues(sqlite3* db, int64_t contentId)
{
    std::vector<RekordboxCue> out;
    if (!tableExists(db, "djmdCue")) return out;
    const char* sql =
        "SELECT Kind, InMsec, OutMsec, Comment, ColorID "
        "FROM djmdCue WHERE ContentID = ? "
        "ORDER BY Kind, InMsec;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        spdlog::warn("[RB] prepare cues failed: {}", sqlite3_errmsg(db));
        return out;
    }
    std::string idStr = std::to_string(contentId);
    sqlite3_bind_text(st, 1, idStr.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        RekordboxCue c;
        c.kind     = sqlite3_column_int(st, 0);
        double inMs  = sqlite3_column_double(st, 1);
        double outMs = sqlite3_column_double(st, 2);
        c.startSec = inMs / 1000.0;
        c.endSec   = (outMs > 0.0) ? outMs / 1000.0 : -1.0;
        c.comment  = colJStr(st, 3);
        c.color    = parseCueColor(st, 4);
        out.push_back(std::move(c));
    }
    sqlite3_finalize(st);
    return out;
}

static std::vector<RekordboxPlaylist> queryPlaylists(sqlite3* db)
{
    std::vector<RekordboxPlaylist> out;
    if (!tableExists(db, "djmdPlaylist")) return out;

    const char* sql =
        "SELECT ID, ParentID, Name, Attribute, Seq FROM djmdPlaylist "
        "ORDER BY Seq;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        spdlog::warn("[RB] prepare playlists failed: {}", sqlite3_errmsg(db));
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        RekordboxPlaylist p;
        juce::String idStr     = colJStr(st, 0);
        juce::String parentStr = colJStr(st, 1);
        p.id        = idStr.getLargeIntValue();
        p.parentId  = parentStr.getLargeIntValue();
        p.name      = colJStr(st, 2);
        p.attribute = sqlite3_column_int(st, 3);
        out.push_back(std::move(p));
    }
    sqlite3_finalize(st);

    if (tableExists(db, "djmdSongPlaylist")) {
        const char* sql2 =
            "SELECT ContentID, TrackNo FROM djmdSongPlaylist "
            "WHERE PlaylistID = ? ORDER BY TrackNo;";
        sqlite3_stmt* st2 = nullptr;
        if (sqlite3_prepare_v2(db, sql2, -1, &st2, nullptr) == SQLITE_OK) {
            for (auto& p : out) {
                sqlite3_reset(st2);
                std::string pid = std::to_string(p.id);
                sqlite3_bind_text(st2, 1, pid.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(st2) == SQLITE_ROW) {
                    juce::String cid = colJStr(st2, 0);
                    p.trackIds.push_back(cid.getLargeIntValue());
                }
                p.trackCount = static_cast<int>(p.trackIds.size());
            }
            sqlite3_finalize(st2);
        }
    }
    return out;
}

static std::vector<RekordboxHistorySession> queryHistorySessions(sqlite3* db)
{
    std::vector<RekordboxHistorySession> out;
    if (!tableExists(db, "djmdHistory")) return out;
    const char* sql =
        "SELECT ID, Name, DateCreated FROM djmdHistory ORDER BY DateCreated DESC;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        spdlog::warn("[RB] prepare history sessions failed: {}", sqlite3_errmsg(db));
        return out;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        RekordboxHistorySession h;
        juce::String idStr = colJStr(st, 0);
        h.id = idStr.getLargeIntValue();
        h.name = colJStr(st, 1);
        h.dateCreated = colJStr(st, 2);
        out.push_back(std::move(h));
    }
    sqlite3_finalize(st);

    if (tableExists(db, "djmdSongHistory")) {
        const char* sql2 =
            "SELECT ContentID, TrackNo FROM djmdSongHistory "
            "WHERE HistoryID = ? ORDER BY TrackNo;";
        sqlite3_stmt* st2 = nullptr;
        if (sqlite3_prepare_v2(db, sql2, -1, &st2, nullptr) == SQLITE_OK) {
            for (auto& h : out) {
                sqlite3_reset(st2);
                std::string hid = std::to_string(h.id);
                sqlite3_bind_text(st2, 1, hid.c_str(), -1, SQLITE_TRANSIENT);
                while (sqlite3_step(st2) == SQLITE_ROW) {
                    juce::String cid = colJStr(st2, 0);
                    h.trackIds.push_back(cid.getLargeIntValue());
                }
            }
            sqlite3_finalize(st2);
        }
    }
    return out;
}

static std::vector<RekordboxDbTrack> queryAllTracks(sqlite3* db)
{
    std::vector<RekordboxDbTrack> out;
    if (!tableExists(db, "djmdContent")) return out;

    juce::String keyCol = "NULL";
    sqlite3_stmt* probe = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(djmdKey);", -1, &probe, nullptr) == SQLITE_OK) {
        while (sqlite3_step(probe) == SQLITE_ROW) {
            const unsigned char* n = sqlite3_column_text(probe, 1);
            if (!n) continue;
            juce::String col = juce::String(reinterpret_cast<const char*>(n));
            if (col == "Name")      { keyCol = "k.Name"; break; }
            if (col == "ScaleName") { keyCol = "k.ScaleName"; break; }
        }
        sqlite3_finalize(probe);
    }

    const std::string sqlStr =
        "SELECT c.ID, c.Title, a.Name, al.Name, g.Name, lb.Name, "
        "       " + keyCol.toStdString() + ", " + keyCol.toStdString() + ", "
        "       c.FolderPath, c.FileNameL, c.ImagePath, c.AnalysisDataPath, "
        "       c.BPM, c.Length, c.Rating, c.ReleaseYear, c.created_at "
        "FROM djmdContent c "
        "LEFT JOIN djmdArtist a  ON c.ArtistID = a.ID "
        "LEFT JOIN djmdAlbum  al ON c.AlbumID  = al.ID "
        "LEFT JOIN djmdGenre  g  ON c.GenreID  = g.ID "
        "LEFT JOIN djmdLabel  lb ON c.LabelID  = lb.ID "
        "LEFT JOIN djmdKey    k  ON c.KeyID    = k.ID;";
    const char* sql = sqlStr.c_str();
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        spdlog::warn("[RB] prepare all tracks failed: {} - trying minimal fallback",
                     sqlite3_errmsg(db));
        const char* sqlMin =
            "SELECT c.ID, c.Title, NULL, NULL, NULL, NULL, NULL, NULL, "
            "       c.FolderPath, c.FileNameL, NULL, NULL, "
            "       c.BPM, c.Length, c.Rating, NULL, NULL "
            "FROM djmdContent c;";
        if (sqlite3_prepare_v2(db, sqlMin, -1, &st, nullptr) != SQLITE_OK) {
            return out;
        }
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        RekordboxDbTrack t;
        juce::String idStr = colJStr(st, 0);
        t.id               = idStr.getLargeIntValue();
        t.title            = colJStr(st, 1);
        t.artist           = colJStr(st, 2);
        t.album            = colJStr(st, 3);
        t.genre            = colJStr(st, 4);
        t.label            = colJStr(st, 5);
        juce::String scale = colJStr(st, 6);
        juce::String kname = colJStr(st, 7);
        t.keyName          = kname;
        t.camelot          = scale + kname;
        t.folderPath       = colJStr(st, 8);
        t.fileName         = colJStr(st, 9);
        t.imagePath        = colJStr(st, 10);
        t.analysisDataPath = colJStr(st, 11);
        t.bpm              = normaliseBpm(sqlite3_column_double(st, 12));
        t.lengthSec        = sqlite3_column_double(st, 13);
        t.rating           = sqlite3_column_int(st, 14);
        t.year             = sqlite3_column_int(st, 15);
        t.dateAdded        = colJStr(st, 16);
        out.push_back(std::move(t));
    }
    sqlite3_finalize(st);
    return out;
}

} // namespace

std::vector<RekordboxDbTrack> RekordboxHistoryReader::getAllTracks()
{
    sqlite3* db = openMasterDb();
    if (!db) return {};
    auto r = queryAllTracks(db);
    sqlite3_close(db);
    return r;
}

std::vector<RekordboxCue> RekordboxHistoryReader::getCuesForTrack(int64_t contentId)
{
    sqlite3* db = openMasterDb();
    if (!db) return {};
    auto r = queryCues(db, contentId);
    sqlite3_close(db);
    return r;
}

std::vector<RekordboxCue> RekordboxHistoryReader::getCuesForNowPlaying()
{
    static Rekordbox::RekordboxProLink prolink;
    static bool started = false;
    if (!started) { started = prolink.start(); }
    uint32_t id = prolink.currentMasterTrackId();
    if (id == 0) return {};
    return getCuesForTrack(static_cast<int64_t>(id));
}

std::vector<RekordboxPlaylist> RekordboxHistoryReader::getPlaylistTree()
{
    sqlite3* db = openMasterDb();
    if (!db) return {};
    auto r = queryPlaylists(db);
    sqlite3_close(db);
    return r;
}

std::vector<RekordboxPlaylist> RekordboxHistoryReader::getAllPlaylistsFlat()
{
    return getPlaylistTree();
}

std::vector<RekordboxHistorySession> RekordboxHistoryReader::getHistorySessions()
{
    sqlite3* db = openMasterDb();
    if (!db) return {};
    auto r = queryHistorySessions(db);
    sqlite3_close(db);
    return r;
}

void RekordboxHistoryReader::logStartupSummary()
{
    sqlite3* db = openMasterDb();
    if (!db) {
        spdlog::debug("[RB] startup summary skipped (master.db unavailable)");
        return;
    }
    auto tracks    = queryAllTracks(db);
    auto playlists = queryPlaylists(db);
    auto sessions  = queryHistorySessions(db);
    int cueCount = 0;
    if (tableExists(db, "djmdCue")) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM djmdCue;", -1, &st, nullptr)
                == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) cueCount = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }
    }
    sqlite3_close(db);
    spdlog::info("[RB] {} tracks, {} playlists, {} history sessions, {} cues",
                 tracks.size(), playlists.size(), sessions.size(), cueCount);
}

bool RekordboxHistoryReader::isRekordboxPresent()
{
    juce::File db(masterDbPath());
    if (!db.existsAsFile()) return false;
    auto& w = sharedWalWatcher();
    return w.isActive();
}

} // namespace BeatMate::Services::DJSoftware
