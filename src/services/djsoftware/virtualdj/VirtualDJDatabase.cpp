#include "VirtualDJDatabase.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::VirtualDJ {

bool VirtualDJDatabase::open(const std::string& path) {
    dbPath_ = path.empty() ? findDefaultPath() : path;

    if (dbPath_.empty() || !fs::exists(dbPath_)) {
        spdlog::error("VirtualDJDatabase: Database not found at {}", dbPath_);
        return false;
    }

    if (fs::path(dbPath_).extension() == ".xml") {
        isOpen_ = true;
    } else {
        isOpen_ = true; // SQLite
    }

    spdlog::info("VirtualDJDatabase: Opened {}", dbPath_);
    return isOpen_;
}

void VirtualDJDatabase::close() {
    isOpen_ = false;
}

std::vector<Models::VirtualDJTrack> VirtualDJDatabase::readAllTracks() {
    if (!isOpen_) return {};

    tracks_.clear();

    const auto ext = fs::path(dbPath_).extension().string();
    if (ext == ".xml") {
        parseXmlDatabase(dbPath_);
    } else if (ext == ".db" || ext == ".vdb") {
        parseSqliteDatabase(dbPath_);
    } else {
        spdlog::warn("VirtualDJDatabase: unknown extension '{}' for {}", ext, dbPath_);
    }

    spdlog::info("VirtualDJDatabase: read {} tracks", tracks_.size());
    return tracks_;
}

bool VirtualDJDatabase::parseXmlDatabase(const std::string& path) {
    juce::File xmlFile{juce::String(path)};
    if (!xmlFile.existsAsFile()) {
        spdlog::error("VirtualDJDatabase: Cannot open {}", path);
        return false;
    }

    auto xmlDoc = juce::XmlDocument::parse(xmlFile);
    if (xmlDoc == nullptr) {
        spdlog::error("VirtualDJDatabase: XML parse error in {}", path);
        return false;
    }

    int count = 0;
    for (auto* songElem = xmlDoc->getChildByName("Song");
         songElem != nullptr;
         songElem = songElem->getNextElementWithTagName("Song")) {

        Models::VirtualDJTrack track;
        track.source = Models::TrackSource::VirtualDJ;

        if (songElem->hasAttribute("FilePath"))
            track.externalPath = songElem->getStringAttribute("FilePath").toStdString();
        if (songElem->hasAttribute("Flag"))
            track.flag = songElem->getStringAttribute("Flag").toStdString();

        // <Tags> = métadonnées type ID3, stockées dans track.tags
        if (auto* tagsElem = songElem->getChildByName("Tags")) {
            for (int i = 0; i < tagsElem->getNumAttributes(); ++i) {
                track.tags[tagsElem->getAttributeName(i).toStdString()] =
                    tagsElem->getAttributeValue(i).toStdString();
            }
        }

        // <Infos> — play counts, dates, bitrate, cover art ref.
        if (auto* infoElem = songElem->getChildByName("Infos")) {
            if (infoElem->hasAttribute("PlayCount"))
                track.playCount = infoElem->getIntAttribute("PlayCount");
            if (infoElem->hasAttribute("FirstSeen"))
                track.firstSeen = infoElem->getStringAttribute("FirstSeen").toStdString();
            if (infoElem->hasAttribute("LastPlay"))
                track.lastPlay = infoElem->getStringAttribute("LastPlay").toStdString();
            // Everything else also goes in tags for later inspection.
            for (int i = 0; i < infoElem->getNumAttributes(); ++i) {
                track.tags["Infos." + infoElem->getAttributeName(i).toStdString()] =
                    infoElem->getAttributeValue(i).toStdString();
            }
        }

        // <Scan> — VDJ's BPM + Key + gain scan output.
        if (auto* scanElem = songElem->getChildByName("Scan")) {
            if (scanElem->hasAttribute("Bpm"))
                track.scanData.bpmScan = scanElem->getStringAttribute("Bpm").toStdString();
            if (scanElem->hasAttribute("Volume")) {
                track.scanData.gainScan = scanElem->getStringAttribute("Volume").toStdString();
                track.gain = scanElem->getDoubleAttribute("Volume");
            }
            track.scanData.scanComplete = scanElem->hasAttribute("Bpm");
            // Key in scan: store in tags so the downstream sync knows about it.
            if (scanElem->hasAttribute("Key"))
                track.tags["Scan.Key"] = scanElem->getStringAttribute("Key").toStdString();
            if (scanElem->hasAttribute("AltKey"))
                track.tags["Scan.AltKey"] = scanElem->getStringAttribute("AltKey").toStdString();
        }

        // <POI> — zero or more points of interest (hot cues, loops, etc.).
        for (auto* poiElem = songElem->getChildByName("POI");
             poiElem != nullptr;
             poiElem = poiElem->getNextElementWithTagName("POI")) {
            juce::String serialized;
            for (int i = 0; i < poiElem->getNumAttributes(); ++i) {
                if (i > 0) serialized << "|";
                serialized << poiElem->getAttributeName(i) << "="
                           << poiElem->getAttributeValue(i);
            }
            track.pois.push_back(serialized.toStdString());
        }

        tracks_.push_back(std::move(track));
        ++count;
    }

    spdlog::info("VirtualDJDatabase: parsed {} tracks from XML {}", count, path);
    return count > 0;
}

// Schéma SQLite VDJ (database.db en 2024+) — le schéma exact varie selon les versions
bool VirtualDJDatabase::parseSqliteDatabase(const std::string& path) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("VirtualDJDatabase: Failed to open SQLite database at {}: {}",
                       path, sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    // Find the track table (case-insensitive, check both singular/plural).
    std::string tableName;
    {
        const char* query =
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND (name LIKE 'song%' OR name LIKE 'Song%') LIMIT 1";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 0);
                if (name) tableName = reinterpret_cast<const char*>(name);
            }
            sqlite3_finalize(stmt);
        }
    }

    if (tableName.empty()) {
        spdlog::warn("VirtualDJDatabase: no 'song'/'Songs' table found in {}", path);
        sqlite3_close(db);
        return false;
    }

    // Introspect columns so we only read what exists.
    std::vector<std::string> columns;
    {
        std::string pragma = "PRAGMA table_info(" + tableName + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, pragma.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name) columns.emplace_back(reinterpret_cast<const char*>(name));
            }
            sqlite3_finalize(stmt);
        }
    }
    if (columns.empty()) {
        spdlog::warn("VirtualDJDatabase: table '{}' has no columns", tableName);
        sqlite3_close(db);
        return false;
    }

    auto hasCol = [&](const char* n) {
        for (auto& c : columns) if (c == n) return true;
        return false;
    };

    // SELECT every column; map by name at row time — robust to schema drift.
    std::string sql = "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += "\"" + columns[i] + "\"";
    }
    sql += " FROM " + tableName;

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("VirtualDJDatabase: SELECT failed on {}: {}", tableName, sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Models::VirtualDJTrack track;
        track.source = Models::TrackSource::VirtualDJ;
        for (int i = 0; i < (int)columns.size(); ++i) {
            const unsigned char* raw = sqlite3_column_text(stmt, i);
            if (!raw) continue;
            std::string value = reinterpret_cast<const char*>(raw);
            const auto& col = columns[i];
            if (col == "FilePath" || col == "filepath" || col == "Path") {
                track.externalPath = value;
            } else if (col == "Flag" || col == "flag") {
                track.flag = value;
            } else if (col == "PlayCount" || col == "playcount") {
                try { track.playCount = std::stoi(value); } catch (...) {}
            } else if (col == "FirstSeen" || col == "firstseen") {
                track.firstSeen = value;
            } else if (col == "LastPlay" || col == "lastplay") {
                track.lastPlay = value;
            } else if (col == "Bpm" || col == "bpm" || col == "BPM") {
                track.scanData.bpmScan = value;
                track.scanData.scanComplete = true;
            } else if (col == "Volume" || col == "Gain" || col == "gain") {
                track.scanData.gainScan = value;
                try { track.gain = std::stod(value); } catch (...) {}
            } else {
                // Preserve unknown columns as tags for later use.
                track.tags[col] = value;
            }
        }
        if (!track.externalPath.empty()) {
            tracks_.push_back(std::move(track));
            ++count;
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    (void)hasCol; // may be useful for future column-presence guards.
    spdlog::info("VirtualDJDatabase: parsed {} tracks from SQLite {} (table '{}')",
                  count, path, tableName);
    return count > 0;
}

std::string VirtualDJDatabase::findDefaultPath() const {
#ifdef _WIN32
    std::vector<std::string> roots;
    char buf[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, buf) == S_OK)
        roots.push_back(std::string(buf) + "/VirtualDJ");
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buf) == S_OK)
        roots.push_back(std::string(buf) + "/VirtualDJ");
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, buf) == S_OK)
        roots.push_back(std::string(buf) + "/VirtualDJ");

    for (const auto& root : roots) {
        for (const char* f : { "/database.xml", "/database.vdb", "/database.db" }) {
            std::string p = root + f;
            std::error_code ec;
            if (fs::exists(p, ec)) {
                spdlog::info("VirtualDJDatabase: trouve {}", p);
                return p;
            }
        }
    }
    spdlog::warn("VirtualDJDatabase: database introuvable (roots essayes = {})", roots.size());
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        std::vector<std::string> roots = {
            h + "/Library/Application Support/VirtualDJ",
            h + "/Documents/VirtualDJ",
        };
        for (const auto& root : roots) {
            for (const char* f : { "/database.xml", "/database.vdb", "/database.db" }) {
                std::string p = root + f;
                std::error_code ec;
                if (fs::exists(p, ec)) {
                    spdlog::info("VirtualDJDatabase: trouve {}", p);
                    return p;
                }
            }
        }
    }
#endif
    return "";
}

} // namespace BeatMate::Services::VirtualDJ
