#include "MultiSourceSearchService.h"
#include "TrackDatabase.h"
#include "../djsoftware/serato/SeratoDatabase.h"
#include "../djsoftware/traktor/TraktorCollectionParser.h"
#include "../djsoftware/rekordbox/RekordboxDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <set>
#include <utility>
#include <sqlite3.h>
#include <juce_core/juce_core.h>

namespace fs = std::filesystem;

namespace {
    std::string toLowerAscii (const std::string& s) {
        std::string o = s;
        std::transform (o.begin(), o.end(), o.begin(),
                        [] (unsigned char c) { return (char) std::tolower (c); });
        return o;
    }
    bool containsCI (const std::string& hay, const std::string& lowerNeedle) {
        if (lowerNeedle.empty()) return true;
        return toLowerAscii (hay).find (lowerNeedle) != std::string::npos;
    }
    void plistDictPairs (const juce::XmlElement* dict,
                         std::vector<std::pair<juce::String, const juce::XmlElement*>>& out) {
        if (dict == nullptr) return;
        for (const juce::XmlElement* k = dict->getFirstChildElement(); k != nullptr; ) {
            if (k->hasTagName ("key")) {
                const juce::XmlElement* v = k->getNextElement();
                if (v != nullptr) { out.push_back ({ k->getAllSubText().trim(), v }); k = v->getNextElement(); continue; }
            }
            k = k->getNextElement();
        }
    }
}

namespace BeatMate::Services::Library {

MultiSourceSearchService::MultiSourceSearchService(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
    SearchSourceConfig local;
    local.source = SearchSource::Local;
    local.name = "BeatMate Collection";
    local.enabled = true;
    local.priority = 100;
    sources_.push_back(local);
}

void MultiSourceSearchService::addSource(const SearchSourceConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                   [&](const auto& s) { return s.source == config.source; }),
                   sources_.end());
    sources_.push_back(config);

    std::sort(sources_.begin(), sources_.end(),
              [](const auto& a, const auto& b) { return a.priority > b.priority; });

    spdlog::info("MultiSourceSearch: Added source '{}' (priority={})", config.name, config.priority);
}

void MultiSourceSearchService::removeSource(SearchSource source) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                   [&](const auto& s) { return s.source == source; }),
                   sources_.end());
}

void MultiSourceSearchService::enableSource(SearchSource source, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : sources_) {
        if (s.source == source) {
            s.enabled = enabled;
            spdlog::info("MultiSourceSearch: Source '{}' {}", s.name, enabled ? "enabled" : "disabled");
            break;
        }
    }
}

std::vector<SearchSourceConfig> MultiSourceSearchService::getSources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

MultiSourceSearchResults MultiSourceSearchService::searchAll(const std::string& query, int limit,
                                                               MultiSourceProgressCallback progressCb) {
    MultiSourceSearchResults allResults;
    auto startTime = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& source : sources_) {
        if (!source.enabled) continue;

        try {
            auto results = searchSource(source.source, query, limit);
            allResults.resultCountBySource[source.source] = static_cast<int>(results.size());

            for (auto& r : results) {
                allResults.results.push_back(std::move(r));
            }

            if (progressCb) {
                progressCb(source.source, static_cast<int>(results.size()));
            }
        } catch (const std::exception& e) {
            allResults.errors.push_back(source.name + ": " + e.what());
            spdlog::error("MultiSourceSearch: Error searching {}: {}", source.name, e.what());
        }
    }

    deduplicateResults(allResults.results);

    std::sort(allResults.results.begin(), allResults.results.end(),
              [](const auto& a, const auto& b) { return a.relevance > b.relevance; });

    if (static_cast<int>(allResults.results.size()) > limit) {
        allResults.results.resize(static_cast<size_t>(limit));
    }

    auto endTime = std::chrono::steady_clock::now();
    allResults.searchDurationMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    spdlog::info("MultiSourceSearch: '{}' returned {} results from {} sources in {:.1f}ms",
                 query, allResults.results.size(), allResults.resultCountBySource.size(),
                 allResults.searchDurationMs);

    return allResults;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchSource(SearchSource source,
                                                                        const std::string& query, int limit) {
    switch (source) {
        case SearchSource::Local:     return searchLocal(query, limit);
        case SearchSource::VirtualDJ: return searchVirtualDJ(query, limit);
        case SearchSource::Rekordbox:  return searchRekordbox(query, limit);
        case SearchSource::Serato:     return searchSerato(query, limit);
        case SearchSource::Traktor:    return searchTraktor(query, limit);
        case SearchSource::EngineDJ:   return searchEngineDJ(query, limit);
        case SearchSource::ITunes:     return searchITunes(query, limit);
        default: return {};
    }
}

std::vector<SearchSourceConfig> MultiSourceSearchService::detectInstalledSources() {
    std::vector<SearchSourceConfig> detected;

    auto vdjPath = detectVirtualDJPath();
    if (!vdjPath.empty()) {
        SearchSourceConfig cfg;
        cfg.source = SearchSource::VirtualDJ;
        cfg.name = "VirtualDJ";
        cfg.databasePath = vdjPath;
        cfg.enabled = true;
        cfg.priority = 50;
        detected.push_back(cfg);
    }

    auto rbPath = detectRekordboxPath();
    if (!rbPath.empty()) {
        SearchSourceConfig cfg;
        cfg.source = SearchSource::Rekordbox;
        cfg.name = "Rekordbox";
        cfg.databasePath = rbPath;
        cfg.enabled = true;
        cfg.priority = 50;
        detected.push_back(cfg);
    }

    auto seratoPath = detectSeratoPath();
    if (!seratoPath.empty()) {
        SearchSourceConfig cfg;
        cfg.source = SearchSource::Serato;
        cfg.name = "Serato DJ";
        cfg.databasePath = seratoPath;
        cfg.enabled = true;
        cfg.priority = 50;
        detected.push_back(cfg);
    }

    auto traktorPath = detectTraktorPath();
    if (!traktorPath.empty()) {
        SearchSourceConfig cfg;
        cfg.source = SearchSource::Traktor;
        cfg.name = "Traktor Pro";
        cfg.databasePath = traktorPath;
        cfg.enabled = true;
        cfg.priority = 50;
        detected.push_back(cfg);
    }

    auto enginePath = detectEngineDJPath();
    if (!enginePath.empty()) {
        SearchSourceConfig cfg;
        cfg.source = SearchSource::EngineDJ;
        cfg.name = "Engine DJ";
        cfg.databasePath = enginePath;
        cfg.enabled = true;
        cfg.priority = 50;
        detected.push_back(cfg);
    }

    spdlog::info("MultiSourceSearch: Detected {} installed DJ sources", detected.size());
    return detected;
}

int64_t MultiSourceSearchService::importFromSource(const MultiSourceResult& result) {
    if (!database_) return -1;

    Models::Track track = result.track;
    track.source = static_cast<Models::TrackSource>(static_cast<int>(result.source));

    return database_->addTrack(track);
}

std::vector<int64_t> MultiSourceSearchService::importFromSource(const std::vector<MultiSourceResult>& results) {
    std::vector<int64_t> ids;
    for (const auto& result : results) {
        ids.push_back(importFromSource(result));
    }
    return ids;
}

bool MultiSourceSearchService::isSourceAvailable(SearchSource source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : sources_) {
        if (s.source == source) {
            if (source == SearchSource::Local) return true;
            return !s.databasePath.empty() && fs::exists(s.databasePath);
        }
    }
    return false;
}

std::string MultiSourceSearchService::getSourceDatabasePath(SearchSource source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : sources_) {
        if (s.source == source) return s.databasePath;
    }
    return "";
}


std::vector<MultiSourceResult> MultiSourceSearchService::searchLocal(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    if (!database_) return results;

    auto tracks = database_->searchFTS(query, limit);
    float maxRank = static_cast<float>(tracks.size());

    for (size_t i = 0; i < tracks.size(); ++i) {
        MultiSourceResult r;
        r.track = std::move(tracks[i]);
        r.source = SearchSource::Local;
        r.sourceName = "BeatMate Collection";
        r.relevance = maxRank > 0 ? (maxRank - static_cast<float>(i)) / maxRank : 1.0f;
        r.isLocal = true;
        results.push_back(std::move(r));
    }

    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchVirtualDJ(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto dbPath = detectVirtualDJPath();
    if (dbPath.empty() || !fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT filepath, title, artist, album, genre, bpm, key FROM songs "
                      "WHERE title LIKE ? OR artist LIKE ? LIMIT ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MultiSourceResult r;
            auto getText = [&stmt](int col) -> std::string {
                auto txt = sqlite3_column_text(stmt, col);
                return txt ? reinterpret_cast<const char*>(txt) : "";
            };

            r.track.filePath = getText(0);
            r.track.title = getText(1);
            r.track.artist = getText(2);
            r.track.album = getText(3);
            r.track.genre = getText(4);
            r.track.bpm = sqlite3_column_double(stmt, 5);
            r.track.key = getText(6);
            r.source = SearchSource::VirtualDJ;
            r.sourceName = "VirtualDJ";
            r.relevance = 0.8f;
            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchRekordbox(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto dbPath = detectRekordboxPath();
    if (dbPath.empty() || !fs::exists(dbPath)) return results;

    Rekordbox::RekordboxDatabase db;
    if (!db.openDatabase(dbPath)) return results;

    const std::string q = toLowerAscii(query);
    for (auto& rbt : db.readAllTracks()) {
        if (!containsCI(rbt.title, q) && !containsCI(rbt.artist, q)) continue;
        MultiSourceResult r;
        r.track.filePath = rbt.externalPath;
        r.track.title = rbt.title;
        r.track.artist = rbt.artist;
        r.track.album = rbt.album;
        r.track.genre = rbt.genre;
        r.track.bpm = rbt.bpm;
        r.track.key = rbt.tonality;
        r.source = SearchSource::Rekordbox;
        r.sourceName = "Rekordbox";
        r.relevance = 0.7f;
        results.push_back(std::move(r));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchSerato(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto path = detectSeratoPath();
    if (path.empty()) return results;

    Serato::SeratoDatabase db;
    if (!db.open(path)) return results;

    const std::string q = toLowerAscii(query);
    for (auto& st : db.readAllTracks()) {
        if (!containsCI(st.title, q) && !containsCI(st.artist, q)) continue;
        MultiSourceResult r;
        r.track.filePath = st.externalPath;
        r.track.title = st.title;
        r.track.artist = st.artist;
        r.track.album = st.album;
        r.track.genre = st.genre;
        r.track.bpm = st.bpm;
        r.track.key = !st.camelotKey.empty() ? st.camelotKey : st.key;
        r.source = SearchSource::Serato;
        r.sourceName = "Serato DJ";
        r.relevance = 0.8f;
        results.push_back(std::move(r));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchTraktor(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto nml = detectTraktorPath();
    if (nml.empty() || !fs::exists(nml)) return results;

    Traktor::TraktorCollectionParser parser;
    const std::string q = toLowerAscii(query);
    for (auto& tt : parser.parse(nml)) {
        if (!containsCI(tt.title, q) && !containsCI(tt.artist, q)) continue;
        MultiSourceResult r;
        r.track.filePath = !tt.externalPath.empty() ? tt.externalPath : (tt.directory + tt.filename);
        r.track.title = tt.title;
        r.track.artist = tt.artist;
        r.track.album = tt.album;
        r.track.genre = tt.genre;
        r.track.bpm = tt.traktorBpm;
        r.track.key = tt.musicalKey;
        r.source = SearchSource::Traktor;
        r.sourceName = "Traktor Pro";
        r.relevance = 0.75f;
        results.push_back(std::move(r));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchEngineDJ(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto dbPath = detectEngineDJPath();
    if (dbPath.empty() || !fs::exists(dbPath)) return results;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT path, title, artist, album, genre, bpmAnalyzed, keyText "
                      "FROM Track WHERE title LIKE ? OR artist LIKE ? LIMIT ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MultiSourceResult r;
            auto getText = [&stmt](int col) -> std::string {
                auto txt = sqlite3_column_text(stmt, col);
                return txt ? reinterpret_cast<const char*>(txt) : "";
            };

            r.track.filePath = getText(0);
            r.track.title = getText(1);
            r.track.artist = getText(2);
            r.track.album = getText(3);
            r.track.genre = getText(4);
            r.track.bpm = sqlite3_column_double(stmt, 5);
            r.track.key = getText(6);
            r.source = SearchSource::EngineDJ;
            r.sourceName = "Engine DJ";
            r.relevance = 0.7f;
            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return results;
}

std::vector<MultiSourceResult> MultiSourceSearchService::searchITunes(const std::string& query, int limit) {
    std::vector<MultiSourceResult> results;
    auto xmlPath = detectITunesPath();
    if (xmlPath.empty() || !fs::exists(xmlPath)) return results;

    const juce::File xmlFile = juce::File(juce::String(xmlPath));
    juce::XmlDocument doc(xmlFile);
    std::unique_ptr<juce::XmlElement> root(doc.getDocumentElement());
    if (root == nullptr) return results;
    const juce::XmlElement* topDict = root->getChildByName("dict");
    if (topDict == nullptr) return results;

    std::vector<std::pair<juce::String, const juce::XmlElement*>> topPairs;
    plistDictPairs(topDict, topPairs);
    const juce::XmlElement* tracksDict = nullptr;
    for (auto& p : topPairs)
        if (p.first == "Tracks" && p.second->hasTagName("dict")) { tracksDict = p.second; break; }
    if (tracksDict == nullptr) return results;

    const std::string q = toLowerAscii(query);
    std::vector<std::pair<juce::String, const juce::XmlElement*>> entries;
    plistDictPairs(tracksDict, entries);
    for (auto& te : entries) {
        if (!te.second->hasTagName("dict")) continue;
        std::vector<std::pair<juce::String, const juce::XmlElement*>> f;
        plistDictPairs(te.second, f);
        auto field = [&f](const char* key) -> juce::String {
            for (auto& kv : f) if (kv.first == key) return kv.second->getAllSubText().trim();
            return {};
        };
        const std::string title = field("Name").toStdString();
        const std::string artist = field("Artist").toStdString();
        if (!containsCI(title, q) && !containsCI(artist, q)) continue;

        MultiSourceResult r;
        const juce::String loc = field("Location");
        if (loc.isNotEmpty())
            r.track.filePath = juce::URL(loc).getLocalFile().getFullPathName().toStdString();
        r.track.title = title;
        r.track.artist = artist;
        r.track.album = field("Album").toStdString();
        r.track.genre = field("Genre").toStdString();
        r.track.bpm = field("BPM").getDoubleValue();
        r.source = SearchSource::ITunes;
        r.sourceName = "iTunes";
        r.relevance = 0.6f;
        results.push_back(std::move(r));
        if (static_cast<int>(results.size()) >= limit) break;
    }
    return results;
}


std::string MultiSourceSearchService::detectVirtualDJPath() const {
#ifdef _WIN32
    std::string appData = std::getenv("APPDATA") ? std::getenv("APPDATA") : "";
    if (!appData.empty()) {
        std::string path = appData + "/VirtualDJ/database.xml";
        if (fs::exists(path)) return path;
        path = appData + "/VirtualDJ/database3.dat";
        if (fs::exists(path)) return path;
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        std::string path = home + "/Documents/VirtualDJ/database.xml";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string MultiSourceSearchService::detectRekordboxPath() const {
#ifdef _WIN32
    std::string appData = std::getenv("APPDATA") ? std::getenv("APPDATA") : "";
    if (!appData.empty()) {
        std::string path = appData + "/Pioneer/rekordbox/master.db";
        if (fs::exists(path)) return path;
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        std::string path = home + "/Library/Pioneer/rekordbox/master.db";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string MultiSourceSearchService::detectSeratoPath() const {
#ifdef _WIN32
    std::string userProfile = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
    if (!userProfile.empty()) {
        std::string path = userProfile + "/Music/_Serato_";
        if (fs::exists(path)) return path;
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        std::string path = home + "/Music/_Serato_";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string MultiSourceSearchService::detectTraktorPath() const {
#ifdef _WIN32
    std::string localAppData = std::getenv("LOCALAPPDATA") ? std::getenv("LOCALAPPDATA") : "";
    if (!localAppData.empty()) {
        std::string path = localAppData + "/Native Instruments/Traktor Pro 3/collection.nml";
        if (fs::exists(path)) return path;
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        std::string path = home + "/Documents/Native Instruments/Traktor Pro 3/collection.nml";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string MultiSourceSearchService::detectEngineDJPath() const {
#ifdef _WIN32
    std::string appData = std::getenv("APPDATA") ? std::getenv("APPDATA") : "";
    if (!appData.empty()) {
        std::string path = appData + "/Engine Library/Database2/m.db";
        if (fs::exists(path)) return path;
    }
#else
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    if (!home.empty()) {
        std::string path = home + "/Music/Engine Library/Database2/m.db";
        if (fs::exists(path)) return path;
    }
#endif
    return "";
}

std::string MultiSourceSearchService::detectITunesPath() const {
    const char* names[] = { "/Music/iTunes/iTunes Music Library.xml",
                            "/Music/iTunes/iTunes Library.xml" };
#ifdef _WIN32
    std::string base = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "";
#else
    std::string base = std::getenv("HOME") ? std::getenv("HOME") : "";
#endif
    if (!base.empty())
        for (const char* n : names) {
            std::string path = base + n;
            if (fs::exists(path)) return path;
        }
    return "";
}

void MultiSourceSearchService::deduplicateResults(std::vector<MultiSourceResult>& results) const {
    std::set<std::string> seen;
    auto it = std::remove_if(results.begin(), results.end(), [&seen](const MultiSourceResult& r) {
        if (r.track.filePath.empty()) return false;
        if (seen.count(r.track.filePath) > 0) return true;
        seen.insert(r.track.filePath);
        return false;
    });
    results.erase(it, results.end());
}

} // namespace BeatMate::Services::Library
