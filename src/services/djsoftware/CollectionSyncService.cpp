#include "CollectionSyncService.h"
#include "rekordbox/RekordboxXmlParser.h"
#include "rekordbox/RekordboxEnvironment.h"
#include "rekordbox/RekordboxXmlExporter.h"
#include "rekordbox/RekordboxDatabase.h"
#include "rekordbox/RekordboxCipher.h"
#include "rekordbox/RekordboxAnlzParser.h"
#include "traktor/TraktorNmlExporter.h"
#include "traktor/TraktorCollectionParser.h"
#include "virtualdj/VirtualDJExporter.h"
#include "virtualdj/VirtualDJPlaylistReader.h"
#include "serato/SeratoTagWriter.h"
#include "serato/SeratoDatabase.h"
#include "enginedj/EngineDJService.h"
#include "../../app/Application.h"
#include "../../models/CuePoint.h"
#include "../library/PlaylistManager.h"

#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <regex>
#include <fstream>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::DJSoftware {

namespace {

// Musical-key → Camelot (standard industrie).
std::string musicalKeyToCamelot(std::string k) {
    std::string s;
    for (char c : k) if (c != ' ' && c != '\t') s += c;
    if (s.empty()) return "";

    if (s.size() >= 2 && s.size() <= 3
        && std::isdigit(static_cast<unsigned char>(s[0]))
        && (std::toupper(static_cast<unsigned char>(s.back())) == 'A'
            || std::toupper(static_cast<unsigned char>(s.back())) == 'B')) {
        const int num = std::atoi(s.substr(0, s.size() - 1).c_str());
        if (num >= 1 && num <= 12)
            return std::to_string(num)
                 + static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
    }

    bool minor = false;
    if (s.size() >= 3 && s.substr(s.size() - 3) == "min") {
        minor = true; s.resize(s.size() - 3);
    } else if (s.size() >= 3 && s.substr(s.size() - 3) == "maj") {
        s.resize(s.size() - 3);
    } else if (s.back() == 'm' && s.size() >= 2) {
        minor = true; s.pop_back();
    } else if (std::islower(static_cast<unsigned char>(s[0]))) {
        minor = true;
    }
    if (s.empty()) return "";
    s[0] = (char) std::toupper(static_cast<unsigned char>(s[0]));
    static const std::pair<std::string, std::string> kEnh[] = {
        {"Db","C#"}, {"Eb","D#"}, {"Gb","F#"}, {"Ab","G#"}, {"Bb","A#"}
    };
    for (auto& e : kEnh) if (s == e.first) s = e.second;
    static const std::map<std::string, std::string> kMajor = {
        {"C","8B"},{"C#","3B"},{"D","10B"},{"D#","5B"},{"E","12B"},
        {"F","7B"},{"F#","2B"},{"G","9B"},{"G#","4B"},{"A","11B"},
        {"A#","6B"},{"B","1B"}
    };
    static const std::map<std::string, std::string> kMinor = {
        {"C","5A"},{"C#","12A"},{"D","7A"},{"D#","2A"},{"E","9A"},
        {"F","4A"},{"F#","11A"},{"G","6A"},{"G#","1A"},{"A","8A"},
        {"A#","3A"},{"B","10A"}
    };
    const auto& tab = minor ? kMinor : kMajor;
    auto it = tab.find(s);
    return (it != tab.end()) ? it->second : std::string{};
}

// Traktor NML MUSICAL_KEY VALUE: 0-11 = C..B major, 12-23 = C..B minor.
std::string traktorMusicalKeyToCamelot(int v) {
    if (v < 0 || v > 23) return "";
    static const char* kRoots[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    std::string name = kRoots[v % 12];
    if (v >= 12) name += "m";
    return musicalKeyToCamelot(name);
}

// VirtualDJ database.xml stores tempo as seconds-per-beat (0.46875 = 128 BPM);
double vdjBpmToRealBpm(double raw) {
    if (raw > 0.1 && raw < 3.0)   return 60.0 / raw;
    if (raw >= 20.0 && raw <= 500.0) return raw;
    return 0.0;
}

std::string analysisSourceName(Models::TrackSource s) {
    switch (s) {
        case Models::TrackSource::Rekordbox: return "rekordbox";
        case Models::TrackSource::Serato:    return "serato";
        case Models::TrackSource::Traktor:   return "traktor";
        case Models::TrackSource::VirtualDJ: return "virtualdj";
        case Models::TrackSource::EngineDJ:  return "enginedj";
        default:                             return "";
    }
}

int analysisPriority(const std::string& src) {
    if (src == "rekordbox") return 5;
    if (src == "serato")    return 4;
    if (src == "enginedj")  return 3;
    if (src == "traktor")   return 2;
    if (src == "virtualdj") return 1;
    return 0;
}

// Normalise le chemin (séparateurs mixtes) pour le matching.
std::string normPath(const std::string& p) {
    std::string out; out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        out += (char)std::tolower((unsigned char)c);
    }
    // strip leading file:// prefix (Rekordbox XML uses it)
    const std::string fp = "file://";
    if (out.rfind(fp, 0) == 0) out.erase(0, fp.size());
    return out;
}

std::string lowerBaseName(const std::string& normed) {
    const auto pos = normed.find_last_of('/');
    return pos == std::string::npos ? normed : normed.substr(pos + 1);
}

} // namespace

CollectionSyncService::CollectionSyncService(std::shared_ptr<DJSoftwareManager> manager,
                                               std::shared_ptr<BeatMate::Services::Library::TrackDatabase> trackDb)
    : manager_(std::move(manager)), trackDb_(std::move(trackDb)) {
}

int CollectionSyncService::insertTrackIfNew(const Models::Track& track,
                                              const std::vector<Models::CuePoint>& cuePoints) {
    if (!trackDb_ || !trackDb_->isOpen() || track.filePath.empty()) return 0;
    const std::string incomingSrc = analysisSourceName(track.source);
    const int incomingPrio = analysisPriority(incomingSrc);
    auto targets = resolveExistingTracks(track);
    if (!targets.empty()) {
        bool updated = false;

        const bool hasIncoming = (track.bpm > 0 || !track.key.empty());
        for (const auto& existing : targets) {
            const bool bpmLooksWrong = (existing.bpm > 500.0);
            const bool keyLooksNumeric =
                !existing.key.empty() && std::all_of(existing.key.begin(), existing.key.end(),
                    [](char c){ return std::isdigit(static_cast<unsigned char>(c)); });
            bool shouldUpdate = false;
            if (hasIncoming) {
                if (existing.bpm <= 0 || existing.key.empty() || bpmLooksWrong || keyLooksNumeric) {
                    shouldUpdate = true;
                } else if (incomingPrio > 0) {
                    const std::string currentSrc = trackDb_->getAnalysisSource(existing.id);
                    const int currentPrio = analysisPriority(currentSrc);
                    shouldUpdate = (currentPrio > 0 && incomingPrio > currentPrio);
                }
            }

            if (shouldUpdate) {
                double newBpm = (track.bpm > 0) ? track.bpm : existing.bpm;
                std::string newKey = (!track.key.empty()) ? track.key : existing.key;
                float newEnergy = (track.energy > 0) ? track.energy : existing.energy;
                trackDb_->updateTrackAnalysisFromSync(existing.id, newBpm, newKey, newEnergy, incomingSrc);
                updated = true;
                spdlog::info("CollectionSync: Updated analysis for '{}' from {} (BPM={:.1f}, Key={})",
                    existing.filePath.empty() ? track.title : existing.filePath,
                    incomingSrc.empty() ? "DJ software" : incomingSrc, newBpm, newKey);
            }

            if (!cuePoints.empty()) {
                auto existingCues = trackDb_->getCuePoints(existing.id);
                if (existingCues.empty()) {
                    std::vector<Models::CuePoint> cuesWithTrackId;
                    for (auto cue : cuePoints) {
                        cue.trackId = existing.id;
                        cuesWithTrackId.push_back(cue);
                    }
                    trackDb_->importCuePointsForTrack(existing.id, cuesWithTrackId);
                    updated = true;
                    spdlog::info("CollectionSync: Imported {} cue points for '{}'",
                        cuePoints.size(), track.title.empty() ? track.filePath : track.title);
                }
            }
        }

        return updated ? 1 : 0;
    }
    auto newId = trackDb_->addTrack(track);
    if (newId > 0 && trackIndexBuilt_) {
        const std::string np = normPath(track.filePath);
        pathIndex_.emplace(np, newId);
        nameIndex_.emplace(lowerBaseName(np), newId);
    }

    // Mémorise la source d'analyse initiale (priorités entre logiciels).
    if (newId > 0 && !incomingSrc.empty() && (track.bpm > 0 || !track.key.empty()))
        trackDb_->setAnalysisSource(newId, incomingSrc);

    if (newId > 0 && !cuePoints.empty()) {
        std::vector<Models::CuePoint> cuesWithTrackId;
        for (auto cue : cuePoints) {
            cue.trackId = newId;
            cuesWithTrackId.push_back(cue);
        }
        trackDb_->importCuePointsForTrack(newId, cuesWithTrackId);
        spdlog::info("CollectionSync: Imported {} cue points for new track '{}'",
            cuePoints.size(), track.title.empty() ? track.filePath : track.title);
    }

    return 1;
}

void CollectionSyncService::rebuildTrackIndex() {
    pathIndex_.clear();
    nameIndex_.clear();
    if (!trackDb_ || !trackDb_->isOpen()) return;
    for (const auto& t : trackDb_->getAllTracks()) {
        if (t.filePath.empty()) continue;
        const std::string np = normPath(t.filePath);
        pathIndex_.emplace(np, t.id);
        nameIndex_.emplace(lowerBaseName(np), t.id);
    }
    trackIndexBuilt_ = true;
    spdlog::info("CollectionSync: track index built ({} paths, {} names)",
                 pathIndex_.size(), nameIndex_.size());
}

std::vector<Models::Track> CollectionSyncService::resolveExistingTracks(const Models::Track& incoming) {
    std::vector<Models::Track> out;
    std::set<int64_t> seen;
    auto push = [&](std::optional<Models::Track> t) {
        if (t && t->id > 0 && seen.insert(t->id).second) out.push_back(std::move(*t));
    };

    push(trackDb_->getTrackByPath(incoming.filePath));

    const std::string np = normPath(incoming.filePath);
    if (!trackIndexBuilt_) rebuildTrackIndex();

    auto pit = pathIndex_.find(np);
    if (pit != pathIndex_.end() && !seen.count(pit->second))
        push(trackDb_->getTrack(pit->second));

    const std::string name = lowerBaseName(np);
    if (name.empty()) return out;

    int64_t incomingSize = incoming.fileSize;
    if (incomingSize <= 0 && juce::File::isAbsolutePath(juce::String(incoming.filePath))) {
        const juce::File f{juce::String(incoming.filePath)};
        if (f.existsAsFile()) incomingSize = (int64_t) f.getSize();
    }
    if (incomingSize <= 0) return out;

    auto range = nameIndex_.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
        if (seen.count(it->second)) continue;
        auto t = trackDb_->getTrack(it->second);
        if (!t) continue;
        int64_t candSize = t->fileSize;
        if (candSize <= 0 && juce::File::isAbsolutePath(juce::String(t->filePath))) {
            const juce::File f{juce::String(t->filePath)};
            if (f.existsAsFile()) candSize = (int64_t) f.getSize();
        }
        if (candSize > 0 && candSize == incomingSize)
            push(std::move(t));
    }
    return out;
}

CollectionSyncService::~CollectionSyncService() {
    stop();
}

void CollectionSyncService::start(int intervalSeconds) {
    if (running_) return;
    running_ = true;
    startTimer(intervalSeconds * 1000);
    spdlog::info("CollectionSyncService: Started with {}s interval", intervalSeconds);
}

void CollectionSyncService::stop() {
    running_ = false;
    stopTimer();
    spdlog::info("CollectionSyncService: Stopped");
}

bool CollectionSyncService::isRunning() const {
    return running_;
}

void CollectionSyncService::syncAll() {
    if (!manager_) {
        spdlog::debug("CollectionSyncService: No manager, skipping syncAll");
        return;
    }
    if (syncing_) {
        spdlog::warn("CollectionSyncService: Sync already in progress");
        return;
    }

    auto detected = manager_->getDetectedSoftware();
    for (const auto& info : detected) {
        if (info.isInstalled) {
            syncSoftware(info.type);
        }
    }
}

void CollectionSyncService::syncSoftware(DJSoftwareType type) {
    if (!manager_) {
        spdlog::debug("CollectionSyncService: No manager, skipping syncSoftware");
        return;
    }
    if (syncing_) {
        spdlog::warn("CollectionSyncService: Sync already in progress");
        return;
    }

    if (!manager_->isSoftwareInstalled(type)) {
        spdlog::warn("CollectionSyncService: {} not installed",
                     DJSoftwareManager::softwareTypeName(type));
        return;
    }

    syncing_ = true;
    status_[type] = SyncStatus::InProgress;
    if (syncStartedCallback_) syncStartedCallback_(static_cast<int>(type));

    spdlog::info("CollectionSyncService: Starting sync for {}",
                 DJSoftwareManager::softwareTypeName(type));

    try {
        performSync(type);
        status_[type] = SyncStatus::Synced;
        if (syncCompletedCallback_) syncCompletedCallback_(static_cast<int>(type), true);
        if (completeCallback_) {
            completeCallback_(type, true, "Sync completed successfully");
        }
    }
    catch (const std::exception& e) {
        spdlog::error("CollectionSyncService: Sync failed for {}: {}",
                     DJSoftwareManager::softwareTypeName(type), e.what());
        status_[type] = SyncStatus::Error;
        if (syncCompletedCallback_) syncCompletedCallback_(static_cast<int>(type), false);
        if (completeCallback_) {
            completeCallback_(type, false, std::string("Sync failed: ") + e.what());
        }
    }

    syncing_ = false;
}

std::map<DJSoftwareType, SyncStatus> CollectionSyncService::getStatus() const {
    return status_;
}


namespace {

Models::PlaylistSource toPlaylistSource(DJSoftwareType t) {
    switch (t) {
        case DJSoftwareType::Rekordbox: return Models::PlaylistSource::Rekordbox;
        case DJSoftwareType::VirtualDJ: return Models::PlaylistSource::VirtualDJ;
        case DJSoftwareType::Serato:    return Models::PlaylistSource::Serato;
        case DJSoftwareType::Traktor:   return Models::PlaylistSource::Traktor;
        case DJSoftwareType::EngineDJ:  return Models::PlaylistSource::EngineDJ;
        default:                        return Models::PlaylistSource::Local;
    }
}

// Resolve a list of external track paths to BeatMate local track IDs.
std::vector<int64_t> resolveTrackIds(BeatMate::Services::Library::TrackDatabase& db,
                                     const std::vector<std::string>& paths)
{
    std::vector<int64_t> out;
    out.reserve(paths.size());
    int autoInserted = 0;

    for (const auto& raw : paths) {
        if (raw.empty()) continue;

        auto t = db.getTrackByPath(raw);
        if (!t) {
            // Forward slashes (Rekordbox XML / Serato sometimes store /)
            std::string fwd = raw;
            std::replace(fwd.begin(), fwd.end(), '\\', '/');
            if (fwd != raw) t = db.getTrackByPath(fwd);
        }
        if (!t) {
            // Back slashes (Windows canonical)
            std::string bwd = raw;
            std::replace(bwd.begin(), bwd.end(), '/', '\\');
            if (bwd != raw) t = db.getTrackByPath(bwd);
        }
        if (!t) {
            // file:// URL (Rekordbox XML uses URL-encoded paths)
            try {
                juce::String s(raw);
                if (s.startsWithIgnoreCase("file://")) {
                    auto localPath = juce::URL(s).getLocalFile().getFullPathName().toStdString();
                    if (!localPath.empty() && localPath != raw) {
                        t = db.getTrackByPath(localPath);
                    }
                }
            } catch (...) {}
        }

        if (t && t->id > 0) {
            out.push_back(t->id);
            continue;
        }

        // No match anywhere — insert a stub Track so the playlist is not empty.
        juce::String rawPath(raw);
        juce::File jf{ rawPath };
        std::string pathForDb = raw;
        if (!jf.existsAsFile()) {
            std::string bwd = raw;
            std::replace(bwd.begin(), bwd.end(), '/', '\\');
            juce::String bwdPath(bwd);
            juce::File jf2{ bwdPath };
            if (jf2.existsAsFile()) { jf = jf2; pathForDb = bwd; }
        }
        if (!jf.existsAsFile()) continue; // truly unreachable — skip silently

        Models::Track stub;
        stub.filePath   = pathForDb;
        stub.title      = jf.getFileNameWithoutExtension().toStdString();
        stub.fileFormat = jf.getFileExtension().substring(1).toStdString();
        stub.fileSize   = jf.getSize();
        stub.dateAdded  = juce::Time::currentTimeMillis() / 1000;
        stub.source     = Models::TrackSource::Rekordbox; // best-guess; replaced by next track sync
        auto newId = db.addTrack(stub);
        if (newId > 0) { out.push_back(newId); ++autoInserted; }
    }

    if (autoInserted > 0) {
        spdlog::info("CollectionSync::resolveTrackIds: auto-inserted {} stub tracks "
                     "so DJ playlists are not empty (analysis/metadata will fill later)",
                     autoInserted);
    }
    return out;
}

} // namespace

int CollectionSyncService::syncAllPlaylists() {
    spdlog::info("[SyncAllPlaylists] BEGIN");
    int total = 0;
    for (auto t : { DJSoftwareType::Rekordbox, DJSoftwareType::VirtualDJ,
                    DJSoftwareType::Serato,    DJSoftwareType::Traktor,
                    DJSoftwareType::EngineDJ }) {
        const int n = syncPlaylistsFor(t);
        spdlog::info("[SyncAllPlaylists] {} -> {} playlists upserted",
                     DJSoftwareManager::softwareTypeName(t), n);
        total += n;
    }
    spdlog::info("[SyncAllPlaylists] DONE total={}", total);
    return total;
}

int CollectionSyncService::syncPlaylistsFor(DJSoftwareType type) {
    return syncPlaylistsImpl(type, {});
}

int CollectionSyncService::syncPlaylistsFor(DJSoftwareType type,
                                            const std::set<std::string>& onlyExternalIds) {
    return syncPlaylistsImpl(type, onlyExternalIds);
}

int CollectionSyncService::syncPlaylistsImpl(DJSoftwareType type,
                                             const std::set<std::string>& onlyExternalIds) {
    auto wanted = [&onlyExternalIds](const std::string& externalId) {
        return onlyExternalIds.empty() || onlyExternalIds.count(externalId) > 0;
    };
    if (!playlistManager_ || !trackDb_ || !trackDb_->isOpen()) {
        spdlog::warn("CollectionSyncService::syncPlaylistsFor: "
                     "PlaylistManager or TrackDatabase not available "
                     "(plMgr={}, trackDb={}, open={})",
                     playlistManager_ ? "ok" : "null",
                     trackDb_ ? "ok" : "null",
                     trackDb_ && trackDb_->isOpen() ? "yes" : "no");
        return 0;
    }
    if (!manager_) {
        spdlog::warn("[SyncPlaylistsFor] no DJSoftwareManager");
        return 0;
    }

    int upserts = 0;
    const auto source = toPlaylistSource(type);
    spdlog::info("[SyncPlaylistsFor] type={}",
                 DJSoftwareManager::softwareTypeName(type));

    switch (type) {
    case DJSoftwareType::Rekordbox: {
        // Strategy: master.db first (decrypted sidecar if present), then XML
        std::string masterDb;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Rekordbox && !i.databasePath.empty()) {
                masterDb = i.databasePath;
                break;
            }
        }
        spdlog::info("[SyncPlaylistsFor:Rekordbox] masterDb='{}'", masterDb);
        BeatMate::Services::Rekordbox::RekordboxDatabase db;
        bool opened = false;
        if (!masterDb.empty()) {
            std::string decrypted = masterDb + ".decrypted.db";
            if (std::filesystem::exists(decrypted)) {
                opened = db.openDatabase(decrypted);
            }
            if (!opened) opened = db.openDatabase(masterDb);
        }
        if (opened) {
            auto infos = db.readPlaylistsRich();
            spdlog::info("[SyncPlaylistsFor:Rekordbox] readPlaylistsRich -> {} rows", infos.size());
            for (const auto& info : infos) {
                if (cancelRequested_.load()) break;
                if (info.attribute != 0) continue; // skip folders for now
                if (!wanted(info.externalId)) continue;
                auto contentIds = db.readPlaylistContentIds(info.externalId);
                std::vector<std::string> paths;
                paths.reserve(contentIds.size());
                for (const auto& cid : contentIds) {
                    auto p = db.readContentFilePath(cid);
                    if (!p.empty()) paths.push_back(std::move(p));
                }
                auto trackIds = resolveTrackIds(*trackDb_, paths);
                playlistManager_->upsertExternalPlaylist(source, info.externalId,
                                                        info.name, trackIds);
                ++upserts;
            }
            db.close();
            spdlog::info("[SyncPlaylistsFor:Rekordbox] upserts after master.db = {}", upserts);
        } else {
            spdlog::warn("[SyncPlaylistsFor:Rekordbox] master.db open FAILED, falling back to XML");
            BeatMate::Services::Rekordbox::RekordboxEnvironment env;
            auto xmlPath = env.findXmlPath();
            if (!xmlPath.empty() && std::filesystem::exists(xmlPath)) {
                BeatMate::Services::Rekordbox::RekordboxXmlParser parser;
                if (parser.parseXml(xmlPath)) {
                    auto playlists = parser.getPlaylists();
                    const auto& rbTracks = parser.getTracks();
                    for (auto& pl : playlists) {
                        if (cancelRequested_.load()) break;
                        if (!wanted(std::to_string(pl.id))) continue;
                        std::vector<std::string> paths;
                        for (auto tid : pl.trackIds) {
                            auto it = std::find_if(rbTracks.begin(), rbTracks.end(),
                                [tid](const auto& rt) { return rt.rekordboxId == std::to_string(tid); });
                            if (it != rbTracks.end()) paths.push_back(it->externalPath);
                        }
                        auto localIds = resolveTrackIds(*trackDb_, paths);
                        playlistManager_->upsertExternalPlaylist(source,
                            std::to_string(pl.id), pl.name, localIds);
                        ++upserts;
                    }
                }
            }
        }
        break;
    }

    case DJSoftwareType::Serato: {
        BeatMate::Services::Serato::SeratoDatabase sdb;
        std::string seratoFolder = BeatMate::Services::Serato::SeratoDatabase::findSeratoFolder();
        if (seratoFolder.empty() || !sdb.open(seratoFolder)) break;
        auto crates = sdb.getCrates(); // map<crateName, vector<trackPath>>
        for (const auto& [name, paths] : crates) {
            if (cancelRequested_.load()) break;
            if (!wanted(name)) continue;
            auto ids = resolveTrackIds(*trackDb_, paths);
            playlistManager_->upsertExternalPlaylist(source, name, name, ids);
            ++upserts;
        }
        break;
    }

    case DJSoftwareType::Traktor: {
        std::string nmlPath;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Traktor && !i.databasePath.empty()) {
                nmlPath = i.databasePath;
                break;
            }
        }
        if (nmlPath.empty()) break;
        BeatMate::Services::Traktor::TraktorCollectionParser parser;
        auto infos = parser.parsePlaylists(nmlPath);
        for (const auto& info : infos) {
            if (cancelRequested_.load()) break;
            if (info.isFolder) continue;
            if (!wanted(info.fullPath)) continue;
            std::vector<std::string> paths;
            paths.reserve(info.entries.size());
            for (const auto& e : info.entries) paths.push_back(e.trackPath);
            auto ids = resolveTrackIds(*trackDb_, paths);
            playlistManager_->upsertExternalPlaylist(source, info.fullPath,
                                                    info.name, ids);
            ++upserts;
        }
        break;
    }

    case DJSoftwareType::VirtualDJ: {
        BeatMate::Services::VirtualDJ::VirtualDJPlaylistReader reader;
        auto infos = reader.readPlaylists();
        for (const auto& info : infos) {
            if (cancelRequested_.load()) break;
            if (info.isFolder) continue;
            if (!wanted(info.fullPath)) continue;
            std::vector<std::string> paths;
            paths.reserve(info.entries.size());
            for (const auto& e : info.entries) paths.push_back(e.filePath);
            auto ids = resolveTrackIds(*trackDb_, paths);
            playlistManager_->upsertExternalPlaylist(source, info.fullPath,
                                                    info.name, ids, info.filePath);
            ++upserts;
        }
        break;
    }

    case DJSoftwareType::EngineDJ: {
        BeatMate::Services::EngineDJ::EngineDJService svc;
        if (!svc.initialize()) break;
        auto playlists = svc.readPlaylists();
        for (const auto& pl : playlists) {
            if (cancelRequested_.load()) break;
            if (!wanted(std::to_string(pl.engineId))) continue;
            auto ids = resolveTrackIds(*trackDb_, pl.trackPaths);
            playlistManager_->upsertExternalPlaylist(source, std::to_string(pl.engineId),
                                                    pl.name, ids);
            ++upserts;
        }
        break;
    }

    default:
        break;
    }

    spdlog::info("CollectionSyncService: syncPlaylistsFor({}) upserted {} playlists",
                 (int)type, upserts);
    return upserts;
}

void CollectionSyncService::timerCallback() {
    // juce::Timer = thread de message : syncAll() délégué au pool de fond.
    if (!manager_) return;
    if (syncing_.load()) return;

    auto* pool = BeatMate::getBackgroundPool();
    if (!pool) {
        spdlog::warn("CollectionSyncService: background pool unavailable, tick skipped");
        return;
    }

    pool->addJob([this]() {
        try {
            spdlog::debug("CollectionSyncService: Auto-sync tick (background)");
            syncAll();
        } catch (const std::exception& e) {
            spdlog::error("CollectionSyncService: tick failed: {}", e.what());
        } catch (...) {
            spdlog::error("CollectionSyncService: tick failed (unknown error)");
        }
    });
}

static std::string libraryFileWatermark(const juce::File& f) {
    if (!f.exists()) return {};
    return std::to_string(f.getLastModificationTime().toMilliseconds())
         + ":" + std::to_string(f.getSize());
}

void CollectionSyncService::performSync(DJSoftwareType type) {
    if (!manager_) {
        spdlog::debug("CollectionSyncService: No manager, skipping performSync");
        return;
    }
    cancelRequested_.store(false);
    trackIndexBuilt_ = false;

    auto info = manager_->getSoftwareInfo(type);

    SyncProgress progress;
    progress.software = type;

    spdlog::info("CollectionSyncService: Syncing {} from {}",
                 DJSoftwareManager::softwareTypeName(type), info.databasePath);

    if (info.databasePath.empty()) {
        spdlog::warn("CollectionSyncService: No database path for {}",
                     DJSoftwareManager::softwareTypeName(type));
        progress.total = 0;
        progress.processed = 0;
        progress.currentItem = "No database path configured";
        if (progressCallback_) progressCallback_(progress);
        return;
    }

    if (!std::filesystem::exists(info.databasePath)) {
        spdlog::warn("CollectionSyncService: Database file not found: {}", info.databasePath);
        progress.total = 0;
        progress.processed = 0;
        progress.currentItem = "Database file not found";
        if (progressCallback_) progressCallback_(progress);
        return;
    }

    int tracksAdded = 0;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto sourceFromType = [](DJSoftwareType t) -> Models::TrackSource {
        switch (t) {
            case DJSoftwareType::Rekordbox: return Models::TrackSource::Rekordbox;
            case DJSoftwareType::VirtualDJ: return Models::TrackSource::VirtualDJ;
            case DJSoftwareType::Serato:    return Models::TrackSource::Serato;
            case DJSoftwareType::Traktor:   return Models::TrackSource::Traktor;
            case DJSoftwareType::EngineDJ:  return Models::TrackSource::EngineDJ;
            default: return Models::TrackSource::Local;
        }
    };

    switch (type) {
        case DJSoftwareType::Rekordbox: {
            spdlog::info("CollectionSyncService: Reading Rekordbox database at {}", info.databasePath);

            Rekordbox::RekordboxDatabase rbDbObj;
            std::string primaryPath = info.databasePath;
            std::string decryptedSidecar = primaryPath + ".decrypted.db";
            bool opened = false;
            if (std::filesystem::exists(decryptedSidecar)) {
                opened = rbDbObj.openDatabase(decryptedSidecar);
                if (opened)
                    spdlog::info("CollectionSyncService: Opened Rekordbox decrypted sidecar");
            }
            if (!opened) {
                opened = rbDbObj.openDatabase(primaryPath);
                if (opened)
                    spdlog::info("CollectionSyncService: Opened Rekordbox master.db via SQLCipher");
            }
            sqlite3* djDb = opened ? rbDbObj.rawHandle() : nullptr;
            int rc = opened ? SQLITE_OK : SQLITE_ERROR;
            if (rc != SQLITE_OK) {
                spdlog::error("CollectionSyncService: Cannot open Rekordbox database via SQLCipher");
                if (djDb) sqlite3_close(djDb);
                break;
            }

            {
                sqlite3_stmt* listStmt = nullptr;
                if (sqlite3_prepare_v2(djDb,
                        "SELECT name FROM sqlite_master WHERE type='table' "
                        "ORDER BY name;", -1, &listStmt, nullptr) == SQLITE_OK) {
                    std::string names;
                    while (sqlite3_step(listStmt) == SQLITE_ROW) {
                        const unsigned char* n = sqlite3_column_text(listStmt, 0);
                        if (n) { names += (const char*) n; names += ", "; }
                    }
                    sqlite3_finalize(listStmt);
                    spdlog::info("[RB sync] master.db tables: {}", names);
                }
            }
            {
                sqlite3_stmt* colStmt = nullptr;
                if (sqlite3_prepare_v2(djDb,
                        "PRAGMA table_info(djmdContent);", -1, &colStmt, nullptr) == SQLITE_OK) {
                    std::string cols;
                    while (sqlite3_step(colStmt) == SQLITE_ROW) {
                        const unsigned char* name = sqlite3_column_text(colStmt, 1);
                        const unsigned char* type = sqlite3_column_text(colStmt, 2);
                        if (name) {
                            cols += (const char*) name;
                            cols += ":";
                            cols += type ? (const char*) type : "?";
                            cols += ", ";
                        }
                    }
                    sqlite3_finalize(colStmt);
                    spdlog::info("[RB sync] djmdContent columns: {}", cols);
                }
            }

            // Schéma Rekordbox 6/7 : djmdContent + JOINs Artist/Album/Genre/Key.
            sqlite3_stmt* stmt = nullptr;
            // dc.AnalysisDataPath -> fichier ANLZ .DAT/.EXT (beatgrid).
            bool hasUpdatedAt = false;
            {
                sqlite3_stmt* probe = nullptr;
                if (sqlite3_prepare_v2(djDb, "SELECT updated_at FROM djmdContent LIMIT 1",
                                       -1, &probe, nullptr) == SQLITE_OK)
                    hasUpdatedAt = true;
                if (probe) sqlite3_finalize(probe);
            }
            const std::string rbWatermark = trackDb_ ? trackDb_->getSyncWatermark("rekordbox") : std::string{};
            const int rbSyncCount = trackDb_ ? trackDb_->getSyncCount("rekordbox") : 0;
            const bool pathfixDone = trackDb_
                && !trackDb_->getSyncWatermark("rekordbox_pathfix").empty();
            const bool incremental = hasUpdatedAt && !rbWatermark.empty()
                                     && (rbSyncCount % 20 != 0) && pathfixDone;
            std::string maxUpdatedAt = rbWatermark;
            bool usingMainQuery = false;

            std::string sqlStr =
                "SELECT dc.FolderPath, dc.Title, "
                "       da.Name, dab.Name, dg.Name, "
                "       dc.Length, dc.BPM, dc.Rating, dk.ScaleName, "
                "       dc.AnalysisDataPath";
            if (hasUpdatedAt) sqlStr += ", dc.updated_at";
            sqlStr +=
                " FROM djmdContent dc "
                "LEFT JOIN djmdArtist da  ON dc.ArtistID = da.ID "
                "LEFT JOIN djmdAlbum  dab ON dc.AlbumID  = dab.ID "
                "LEFT JOIN djmdGenre  dg  ON dc.GenreID  = dg.ID "
                "LEFT JOIN djmdKey    dk  ON dc.KeyID    = dk.ID "
                "WHERE dc.FolderPath IS NOT NULL";
            if (incremental) sqlStr += " AND dc.updated_at > '" + rbWatermark + "'";
            const char* sql = sqlStr.c_str();
            rc = sqlite3_prepare_v2(djDb, sql, -1, &stmt, nullptr);
            if (rc == SQLITE_OK) {
                usingMainQuery = true;
                spdlog::info("[RB sync] {} read (watermark='{}')",
                             incremental ? "incremental" : "full", rbWatermark);
            }
            if (rc != SQLITE_OK) {
                spdlog::warn("[RB sync] full-join query failed: {} — retrying"
                             " with no-join fallback", sqlite3_errmsg(djDb));
                const char* altSql =
                    "SELECT FolderPath, Title, NULL, NULL, NULL, "
                    "       Length, BPM, Rating, NULL "
                    "FROM djmdContent WHERE FolderPath IS NOT NULL";
                rc = sqlite3_prepare_v2(djDb, altSql, -1, &stmt, nullptr);
            }
            if (rc != SQLITE_OK) {
                spdlog::warn("[RB sync] no-join fallback also failed: {} —"
                             " dropping to legacy v5 schema", sqlite3_errmsg(djDb));
                const char* altSql =
                    "SELECT FilePath, Name, Artist, Album, Genre, "
                    "       TotalTime, BPM, Rating, Key "
                    "FROM content WHERE FilePath IS NOT NULL";
                rc = sqlite3_prepare_v2(djDb, altSql, -1, &stmt, nullptr);
            }
            if (rc != SQLITE_OK) {
                spdlog::error("[RB sync] all three track-read queries failed: {}",
                              sqlite3_errmsg(djDb));
            }

            if (rc == SQLITE_OK) {
                sqlite3_stmt* countStmt = nullptr;
                sqlite3_prepare_v2(djDb, "SELECT COUNT(*) FROM djmdContent", -1, &countStmt, nullptr);
                if (!countStmt) {
                    sqlite3_prepare_v2(djDb, "SELECT COUNT(*) FROM content", -1, &countStmt, nullptr);
                }
                if (countStmt && sqlite3_step(countStmt) == SQLITE_ROW) {
                    progress.total = sqlite3_column_int(countStmt, 0);
                }
                if (countStmt) sqlite3_finalize(countStmt);

                progress.processed = 0;

                if (trackDb_) trackDb_->beginTransaction();

                // BPM Rekordbox 6/7 = BPM*100 ; clé via djmdKey.

                int anlzStats[4] = { 0, 0, 0, 0 };
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    if (cancelRequested_.load()) break;
                    const char* path   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* title  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                    const char* album  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                    const char* genre  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                    double duration    = sqlite3_column_double(stmt, 5);
                    double bpmRaw      = sqlite3_column_double(stmt, 6);
                    int rating         = sqlite3_column_int(stmt, 7);
                    const char* keyStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

                    if (usingMainQuery && hasUpdatedAt) {
                        if (const auto* up = sqlite3_column_text(stmt, 10)) {
                            const std::string u = reinterpret_cast<const char*>(up);
                            if (u > maxUpdatedAt) maxUpdatedAt = u;
                        }
                    }

                    double bpm = bpmRaw;
                    if (bpm > 500.0) bpm /= 100.0;

                    // Rekordbox master.db stores BPM=0 for tracks not analysed
                    if (bpm <= 0.0) {
                        const char* anlzPath = reinterpret_cast<const char*>(
                            sqlite3_column_text(stmt, 9));
                        if (!anlzPath || !anlzPath[0]) {
                            ++anlzStats[0];
                        } else {
                            juce::File base = juce::File(juce::String(info.databasePath))
                                .getParentDirectory().getChildFile("share");
                            std::string rel = anlzPath;
                            if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);

                            auto tryParse = [](const juce::File& f) -> double {
                                if (!f.existsAsFile()) return 0.0;
                                auto beats = Rekordbox::RekordboxAnlzParser::readBeats(
                                    f.getFullPathName().toStdString());
                                if (!beats.empty() && beats.front().bpm > 0.0)
                                    return beats.front().bpm;
                                return 0.0;
                            };

                            juce::File f = base.getChildFile(juce::String(rel));
                            if (!f.existsAsFile())
                                f = base.getChildFile("PIONEER")
                                        .getChildFile(juce::String(rel));

                            double bFound = tryParse(f);
                            if (bFound <= 0.0) {
                                juce::File ext = f.withFileExtension(".EXT");
                                bFound = tryParse(ext);
                            }
                            if (bFound <= 0.0) {
                                juce::File dir = f.getParentDirectory();
                                if (dir.isDirectory()) {
                                    auto all = dir.findChildFiles(
                                        juce::File::findFiles, false, "ANLZ*.DAT;ANLZ*.EXT");
                                    for (const auto& cand : all) {
                                        bFound = tryParse(cand);
                                        if (bFound > 0.0) break;
                                    }
                                }
                            }

                            if (bFound > 0.0) {
                                bpm = bFound;
                                ++anlzStats[1];
                            } else if (!f.existsAsFile()) {
                                ++anlzStats[3];
                            } else {
                                ++anlzStats[2];
                            }
                        }
                    }
                    if (progress.processed > 0 && progress.processed % 500 == 0) {
                        spdlog::info(
                            "[RB sync] ANLZ stats @ {}: empty-path={} ok={} no-beats={} file-missing={}",
                            progress.processed, anlzStats[0], anlzStats[1],
                            anlzStats[2], anlzStats[3]);
                    }

                    // Key: already a Camelot string if it starts with a digit
                    std::string keyCamelot;
                    if (keyStr && keyStr[0]) {
                        std::string k = keyStr;
                        const bool looksCamelot =
                            k.size() >= 2 && std::isdigit(static_cast<unsigned char>(k[0]))
                            && (k.back() == 'A' || k.back() == 'B' ||
                                k.back() == 'a' || k.back() == 'b');
                        if (looksCamelot) {
                            if (k.back() == 'a') k.back() = 'A';
                            if (k.back() == 'b') k.back() = 'B';
                            keyCamelot = k;
                        } else {
                            keyCamelot = musicalKeyToCamelot(k);
                        }
                    }

                    if (path && passesPathFilter(path)) {
                        progress.processed++;
                        progress.currentItem = title ? title : path;

                        Models::Track track;
                        track.filePath   = path;
                        track.title      = title  ? title  : "";
                        track.artist     = artist ? artist : "";
                        track.album      = album  ? album  : "";
                        track.genre      = genre  ? genre  : "";
                        track.duration   = duration;
                        track.bpm        = bpm;
                        track.rating     = rating;
                        track.key        = keyCamelot;
                        track.camelotKey = keyCamelot;   // engine reads this
                        track.source     = sourceFromType(type);
                        track.dateAdded  = now;

                        std::vector<Models::CuePoint> cuePoints;
                        sqlite3_stmt* cueStmt = nullptr;
                        const char* cueSql =
                            "SELECT Num, InMsec, OutMsec, HotCueName, ColorCode "
                            "FROM djmdCue WHERE ContentID = (SELECT ID FROM djmdContent WHERE FolderPath = ?) "
                            "ORDER BY Num";
                        if (sqlite3_prepare_v2(djDb, cueSql, -1, &cueStmt, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(cueStmt, 1, path, -1, SQLITE_TRANSIENT);
                            while (sqlite3_step(cueStmt) == SQLITE_ROW) {
                                Models::CuePoint cue;
                                cue.number = sqlite3_column_int(cueStmt, 0) + 1;
                                double inMs = sqlite3_column_double(cueStmt, 1);
                                double outMs = sqlite3_column_double(cueStmt, 2);
                                cue.position = inMs / 1000.0;
                                if (outMs > inMs) {
                                    cue.length = (outMs - inMs) / 1000.0;
                                    cue.type = Models::CuePointType::Loop;
                                } else {
                                    cue.type = Models::CuePointType::HotCue;
                                }
                                const char* cueName = reinterpret_cast<const char*>(sqlite3_column_text(cueStmt, 3));
                                if (cueName) cue.name = cueName;
                                cuePoints.push_back(cue);
                            }
                            sqlite3_finalize(cueStmt);
                        }

                        tracksAdded += insertTrackIfNew(track, cuePoints);

                        if (progressCallback_ && (progress.processed % 50 == 0)) {
                            progressCallback_(progress);
                        }
                        if (syncProgressNotifyCallback_ && (progress.processed % 50 == 0)) {
                            syncProgressNotifyCallback_(static_cast<int>(type),
                                                       progress.processed, progress.total);
                        }
                    }
                }
                sqlite3_finalize(stmt);

                if (trackDb_) {
                    trackDb_->commitTransaction();
                    if (usingMainQuery && hasUpdatedAt && !maxUpdatedAt.empty()
                        && pathFilter_.empty() && !cancelRequested_.load()) {
                        trackDb_->setSyncWatermark("rekordbox", maxUpdatedAt);
                        if (!incremental)
                            trackDb_->setSyncWatermark("rekordbox_pathfix", "done");
                        spdlog::info("[RB sync] watermark -> '{}' ({})",
                                     maxUpdatedAt, incremental ? "incremental" : "full");
                    }
                }
            } else {
                spdlog::warn("CollectionSyncService: Could not query Rekordbox tables: {} - trying XML fallback",
                            sqlite3_errmsg(djDb));
                // Ne pas fermer djDb ici : il appartient à rbDbObj.
                djDb = nullptr;

                Rekordbox::RekordboxEnvironment env;
                auto xmlPath = env.findXmlPath();
                if (!xmlPath.empty()) {
                    spdlog::info("CollectionSyncService: Using Rekordbox XML fallback: {}", xmlPath);
                    Rekordbox::RekordboxXmlParser parser;
                    if (parser.parseXml(xmlPath)) {
                        auto rbTracks = parser.getTracks();
                        progress.total = static_cast<int>(rbTracks.size());
                        progress.processed = 0;

                        if (trackDb_) trackDb_->beginTransaction();

                        for (const auto& rbt : rbTracks) {
                            if (cancelRequested_.load()) break;
                            if (!passesPathFilter(rbt.externalPath)) continue;
                            progress.processed++;
                            progress.currentItem = rbt.title.empty() ? rbt.externalPath : rbt.title;

                            Models::Track track;
                            track.filePath = rbt.externalPath;
                            track.title = rbt.title;
                            track.artist = rbt.artist;
                            track.album = rbt.album;
                            track.genre = rbt.genre;
                            track.duration = rbt.duration;
                            track.bpm = rbt.bpm;
                            track.rating = rbt.rating;
                            track.key = rbt.tonality;
                            track.camelotKey = rbt.tonality;
                            track.comment = rbt.comment;
                            track.color = rbt.color;
                            track.source = sourceFromType(type);
                            track.dateAdded = now;

                            std::vector<Models::CuePoint> cuePoints;
                            for (const auto& rbCue : rbt.hotCues) {
                                Models::CuePoint cue;
                                cue.number = rbCue.number + 1;
                                cue.position = rbCue.position;
                                if (rbCue.isLoop && rbCue.length > 0) {
                                    cue.length = rbCue.length;
                                    cue.type = Models::CuePointType::Loop;
                                } else {
                                    cue.type = Models::CuePointType::HotCue;
                                }
                                cue.name = rbCue.name;
                                cue.color = rbCue.color;
                                cuePoints.push_back(cue);
                            }

                            tracksAdded += insertTrackIfNew(track, cuePoints);

                            if (progressCallback_ && (progress.processed % 50 == 0))
                                progressCallback_(progress);
                        }

                        if (trackDb_) trackDb_->commitTransaction();
                        spdlog::info("CollectionSyncService: Imported {} tracks from Rekordbox XML", tracksAdded);
                    }
                } else {
                    spdlog::warn("CollectionSyncService: No Rekordbox XML found either");
                }
                break;
            }

            // djDb appartient à rbDbObj : son destructeur ferme la base.

            try {
                if (std::filesystem::exists(decryptedSidecar)) {
                    std::filesystem::remove(decryptedSidecar);
                }
            } catch (...) {}

            break;
        }

        case DJSoftwareType::VirtualDJ: {
            spdlog::info("CollectionSyncService: Reading VirtualDJ database at {}", info.databasePath);
            if (pathFilter_.empty()) {
                const std::string wm = libraryFileWatermark(juce::File(info.databasePath));
                if (trackDb_ && !wm.empty()
                    && trackDb_->getSyncWatermark("virtualdj") == wm
                    && trackDb_->getSyncCount("virtualdj") % 20 != 0
                    && !trackDb_->getSyncWatermark("virtualdj_pathfix").empty()) {
                    progress.currentItem = "Up to date";
                    spdlog::info("CollectionSyncService: VirtualDJ database unchanged - skipped");
                    if (trackDb_) trackDb_->setSyncWatermark("virtualdj", wm);
                    break;
                }
                if (trackDb_ && !wm.empty()) trackDb_->setSyncWatermark("virtualdj", wm);
                if (trackDb_) trackDb_->setSyncWatermark("virtualdj_pathfix", "done");
            }
            progress.total = 0;
            progress.processed = 0;
            progress.currentItem = "Scanning VirtualDJ XML database...";
            if (progressCallback_) progressCallback_(progress);

            juce::File dbFile(info.databasePath);
            if (dbFile.existsAsFile()) {
                auto xml = juce::parseXML(dbFile);
                if (xml) {
                    int count = 0;
                    for (auto* child = xml->getFirstChildElement(); child; child = child->getNextElement()) {
                        if (child->hasTagName("Song")) count++;
                    }
                    progress.total = count;

                    if (trackDb_) trackDb_->beginTransaction();

                    for (auto* child = xml->getFirstChildElement(); child; child = child->getNextElement()) {
                        if (cancelRequested_.load()) break;
                        if (child->hasTagName("Song")) {
                            auto filePath = child->getStringAttribute("FilePath").toStdString();
                            if (!passesPathFilter(filePath)) continue;
                            progress.processed++;
                            progress.currentItem = filePath;

                            Models::Track track;
                            track.filePath = filePath;
                            track.source   = sourceFromType(type);
                            track.dateAdded = now;

                            // VirtualDJ stores metadata on child elements:
                            auto* tags  = child->getChildByName("Tags");
                            auto* infos = child->getChildByName("Infos");
                            auto* scan  = child->getChildByName("Scan");

                            if (tags) {
                                track.title  = tags->getStringAttribute("Title", "").toStdString();
                                track.artist = tags->getStringAttribute("Author", "").toStdString();
                                track.album  = tags->getStringAttribute("Album", "").toStdString();
                                track.genre  = tags->getStringAttribute("Genre", "").toStdString();
                            } else {
                                track.title  = child->getStringAttribute("Title", "").toStdString();
                                track.artist = child->getStringAttribute("Artist", "").toStdString();
                                track.album  = child->getStringAttribute("Album", "").toStdString();
                                track.genre  = child->getStringAttribute("Genre", "").toStdString();
                            }

                            double vdjBpmRaw = scan ? scan->getDoubleAttribute("Bpm", 0.0) : 0.0;
                            if (vdjBpmRaw <= 0.0 && tags)
                                vdjBpmRaw = tags->getDoubleAttribute("Bpm", 0.0);
                            if (vdjBpmRaw <= 0.0)
                                vdjBpmRaw = child->getDoubleAttribute("BPM", 0.0);
                            track.bpm = vdjBpmToRealBpm(vdjBpmRaw);

                            std::string vdjKey = scan ? scan->getStringAttribute("Key", "").toStdString() : "";
                            if (vdjKey.empty() && tags)
                                vdjKey = tags->getStringAttribute("Key", "").toStdString();
                            if (vdjKey.empty())
                                vdjKey = child->getStringAttribute("Key", "").toStdString();
                            track.key        = musicalKeyToCamelot(vdjKey);
                            track.camelotKey = track.key;

                            auto songLen = infos
                                ? infos->getStringAttribute("SongLength", "0").toStdString()
                                : child->getStringAttribute("SongLength", "0").toStdString();
                            track.duration = std::atof(songLen.c_str());

                            // Extract VirtualDJ POIs (Points of Interest = cue points)
                            std::vector<Models::CuePoint> cuePoints;
                            int cueNum = 1;
                            for (auto* poi = child->getChildByName("Poi"); poi; poi = poi->getNextElementWithTagName("Poi")) {
                                auto poiType = poi->getStringAttribute("Type", "").toStdString();
                                if (poiType == "cue" || poiType == "remix") {
                                    Models::CuePoint cue;
                                    cue.number = cueNum++;
                                    cue.position = poi->getDoubleAttribute("Pos", 0.0);
                                    cue.name = poi->getStringAttribute("Name", "").toStdString();
                                    cue.type = Models::CuePointType::HotCue;
                                    cuePoints.push_back(cue);
                                } else if (poiType == "loop") {
                                    Models::CuePoint cue;
                                    cue.number = cueNum++;
                                    cue.position = poi->getDoubleAttribute("Pos", 0.0);
                                    cue.length = poi->getDoubleAttribute("Size", 0.0);
                                    cue.name = poi->getStringAttribute("Name", "").toStdString();
                                    cue.type = Models::CuePointType::Loop;
                                    cuePoints.push_back(cue);
                                }
                            }

                            tracksAdded += insertTrackIfNew(track, cuePoints);

                            if (progressCallback_ && (progress.processed % 50 == 0)) {
                                progressCallback_(progress);
                            }
                        }
                    }

                    if (trackDb_) trackDb_->commitTransaction();
                }
            }
            break;
        }

        case DJSoftwareType::Serato: {
            spdlog::info("CollectionSyncService: Reading Serato database at {}", info.databasePath);
            if (pathFilter_.empty()) {
                const std::string wm = libraryFileWatermark(
                    juce::File(info.databasePath).getChildFile("database V2"));
                if (trackDb_ && !wm.empty()
                    && trackDb_->getSyncWatermark("serato") == wm
                    && trackDb_->getSyncCount("serato") % 20 != 0
                    && !trackDb_->getSyncWatermark("serato_pathfix").empty()) {
                    progress.currentItem = "Up to date";
                    spdlog::info("CollectionSyncService: Serato database unchanged - skipped");
                    if (trackDb_) trackDb_->setSyncWatermark("serato", wm);
                    break;
                }
                if (trackDb_ && !wm.empty()) trackDb_->setSyncWatermark("serato", wm);
                if (trackDb_) trackDb_->setSyncWatermark("serato_pathfix", "done");
            }
            progress.total = 0;
            progress.processed = 0;
            progress.currentItem = "Scanning Serato database...";
            if (progressCallback_) progressCallback_(progress);

            // Serato stores its database in binary format (_Serato_/database V2)
            juce::File seratoDir(info.databasePath);
            if (seratoDir.isDirectory()) {
                auto dbFileSerato = seratoDir.getChildFile("database V2");
                if (dbFileSerato.existsAsFile()) {
                    juce::MemoryBlock data;
                    if (dbFileSerato.loadFileAsData(data)) {
                        const uint8_t* ptr = static_cast<const uint8_t*>(data.getData());
                        size_t size = data.getSize();
                        size_t pos = 0;

                        if (trackDb_) trackDb_->beginTransaction();

                        while (pos + 8 < size) {
                            if (cancelRequested_.load()) break;
                            if (ptr[pos] == 'o' && ptr[pos+1] == 't' &&
                                ptr[pos+2] == 'r' && ptr[pos+3] == 'k') {
                                uint32_t entryLen = (static_cast<uint32_t>(ptr[pos+4]) << 24) |
                                                    (static_cast<uint32_t>(ptr[pos+5]) << 16) |
                                                    (static_cast<uint32_t>(ptr[pos+6]) << 8) |
                                                    static_cast<uint32_t>(ptr[pos+7]);
                                size_t entryStart = pos + 8;
                                size_t entryEnd = std::min(entryStart + entryLen, size);

                                std::string trackPath, trackTitle, trackArtist, trackAlbum, trackGenre;
                                std::string trackBpm, trackKey;
                                size_t subPos = entryStart;
                                while (subPos + 8 < entryEnd) {
                                    char tag[5] = {static_cast<char>(ptr[subPos]),
                                                   static_cast<char>(ptr[subPos+1]),
                                                   static_cast<char>(ptr[subPos+2]),
                                                   static_cast<char>(ptr[subPos+3]), 0};
                                    uint32_t fieldLen = (static_cast<uint32_t>(ptr[subPos+4]) << 24) |
                                                        (static_cast<uint32_t>(ptr[subPos+5]) << 16) |
                                                        (static_cast<uint32_t>(ptr[subPos+6]) << 8) |
                                                        static_cast<uint32_t>(ptr[subPos+7]);
                                    subPos += 8;
                                    if (subPos + fieldLen > entryEnd) break;

                                    // Serato uses UTF-16BE for string fields
                                    std::string fieldVal;
                                    if (fieldLen >= 2) {
                                        for (size_t fi = 0; fi + 1 < fieldLen; fi += 2) {
                                            char16_t ch = static_cast<char16_t>(
                                                (static_cast<uint16_t>(ptr[subPos + fi]) << 8) |
                                                static_cast<uint16_t>(ptr[subPos + fi + 1]));
                                            if (ch > 0 && ch < 128) fieldVal += static_cast<char>(ch);
                                        }
                                    }

                                    std::string tagStr(tag);
                                    if (tagStr == "ptrk") trackPath = fieldVal;
                                    else if (tagStr == "tsng") trackTitle = fieldVal;
                                    else if (tagStr == "tart") trackArtist = fieldVal;
                                    else if (tagStr == "talb") trackAlbum = fieldVal;
                                    else if (tagStr == "tgen") trackGenre = fieldVal;
                                    else if (tagStr == "tbpm") trackBpm = fieldVal;
                                    else if (tagStr == "tkey") trackKey = fieldVal;

                                    subPos += fieldLen;
                                }

                                if (!trackPath.empty() && passesPathFilter(trackPath)) {
                                    progress.processed++;
                                    progress.currentItem = trackTitle.empty() ? trackPath : trackTitle;

                                    Models::Track track;
                                    track.filePath = trackPath;
                                    track.title    = trackTitle;
                                    track.artist   = trackArtist;
                                    track.album    = trackAlbum;
                                    track.genre    = trackGenre;
                                    track.source   = sourceFromType(type);
                                    track.dateAdded = now;

                                    const double seratoBpm = std::atof(trackBpm.c_str());
                                    if (seratoBpm >= 20.0 && seratoBpm <= 500.0)
                                        track.bpm = seratoBpm;
                                    const std::string seratoKey = musicalKeyToCamelot(trackKey);
                                    if (!seratoKey.empty()) {
                                        track.key        = seratoKey;
                                        track.camelotKey = seratoKey;
                                    }

                                    tracksAdded += insertTrackIfNew(track);

                                    if (progressCallback_ && (progress.processed % 50 == 0)) {
                                        progressCallback_(progress);
                                    }
                                }

                                pos = entryEnd;
                            } else {
                                pos++;
                            }
                        }

                        progress.total = progress.processed;
                        if (trackDb_) trackDb_->commitTransaction();
                    }
                }
            }
            break;
        }

        case DJSoftwareType::Traktor: {
            spdlog::info("CollectionSyncService: Reading Traktor collection at {}", info.databasePath);
            if (pathFilter_.empty()) {
                const std::string wm = libraryFileWatermark(juce::File(info.databasePath));
                if (trackDb_ && !wm.empty()
                    && trackDb_->getSyncWatermark("traktor") == wm
                    && trackDb_->getSyncCount("traktor") % 20 != 0
                    && !trackDb_->getSyncWatermark("traktor_pathfix").empty()) {
                    progress.currentItem = "Up to date";
                    spdlog::info("CollectionSyncService: Traktor collection unchanged - skipped");
                    if (trackDb_) trackDb_->setSyncWatermark("traktor", wm);
                    break;
                }
                if (trackDb_ && !wm.empty()) trackDb_->setSyncWatermark("traktor", wm);
                if (trackDb_) trackDb_->setSyncWatermark("traktor_pathfix", "done");
            }
            progress.total = 0;
            progress.processed = 0;
            progress.currentItem = "Scanning Traktor NML collection...";
            if (progressCallback_) progressCallback_(progress);

            juce::File nmlFile(info.databasePath);
            if (nmlFile.existsAsFile()) {
                auto xml = juce::parseXML(nmlFile);
                if (xml) {
                    auto* collection = xml->getChildByName("COLLECTION");
                    if (collection) {
                        progress.total = collection->getIntAttribute("ENTRIES", 0);

                        if (trackDb_) trackDb_->beginTransaction();

                        for (auto* entry = collection->getFirstChildElement(); entry; entry = entry->getNextElement()) {
                            if (cancelRequested_.load()) break;
                            if (entry->hasTagName("ENTRY")) {
                                progress.processed++;
                                auto title  = entry->getStringAttribute("TITLE", "").toStdString();
                                auto artist = entry->getStringAttribute("ARTIST", "").toStdString();
                                progress.currentItem = title;

                                // Traktor stores path in LOCATION sub-element
                                std::string filePath;
                                auto* location = entry->getChildByName("LOCATION");
                                if (location) {
                                    auto dir  = location->getStringAttribute("DIR", "").toStdString();
                                    auto file = location->getStringAttribute("FILE", "").toStdString();
                                    auto volume = location->getStringAttribute("VOLUME", "").toStdString();
                                    filePath = volume + dir + file;
                                    // Convert Traktor path separators (/:) to native
                                    std::replace(filePath.begin(), filePath.end(), ':', '/');
                                }

                                if (!filePath.empty() && passesPathFilter(filePath)) {
                                    Models::Track track;
                                    track.filePath = filePath;
                                    track.title    = title;
                                    track.artist   = artist;
                                    track.album    = entry->getStringAttribute("ALBUM", "").toStdString();
                                    track.genre    = entry->getStringAttribute("GENRE", "").toStdString();
                                    track.source   = sourceFromType(type);
                                    track.dateAdded = now;

                                    // Traktor stores BPM and key in TEMPO and MUSICAL_KEY.
                                    auto* tempo = entry->getChildByName("TEMPO");
                                    if (tempo) {
                                        track.bpm = tempo->getDoubleAttribute("BPM", 0.0);
                                    }
                                    auto* musicalKey = entry->getChildByName("MUSICAL_KEY");
                                    if (musicalKey) {
                                        track.key = traktorMusicalKeyToCamelot(
                                            musicalKey->getIntAttribute("VALUE", -1));
                                    }
                                    auto* info_elem = entry->getChildByName("INFO");
                                    if (info_elem) {
                                        track.rating = info_elem->getIntAttribute("RANKING", 0) / 51; // Traktor uses 0-255
                                        track.duration = info_elem->getDoubleAttribute("PLAYTIME", 0.0);
                                        if (track.key.empty())
                                            track.key = musicalKeyToCamelot(
                                                info_elem->getStringAttribute("KEY", "").toStdString());
                                    }
                                    track.camelotKey = track.key;

                                    std::vector<Models::CuePoint> cuePoints;
                                    for (auto* cueElem = entry->getChildByName("CUE_V2"); cueElem;
                                         cueElem = cueElem->getNextElementWithTagName("CUE_V2")) {
                                        Models::CuePoint cue;
                                        int cueType = cueElem->getIntAttribute("TYPE", 0);
                                        int hotcue = cueElem->getIntAttribute("HOTCUE", -1);
                                        double startMs = cueElem->getDoubleAttribute("START", 0.0);
                                        double lenMs = cueElem->getDoubleAttribute("LEN", 0.0);

                                        cue.position = startMs / 1000.0;
                                        cue.name = cueElem->getStringAttribute("NAME", "").toStdString();
                                        cue.number = (hotcue >= 0) ? hotcue + 1 : 0;

                                        if (cueType == 5 && lenMs > 0) {
                                            cue.type = Models::CuePointType::Loop;
                                            cue.length = lenMs / 1000.0;
                                        } else if (cueType == 0) {
                                            cue.type = Models::CuePointType::HotCue;
                                        } else if (cueType == 4) {
                                            cue.type = Models::CuePointType::Grid;
                                        } else {
                                            cue.type = Models::CuePointType::MemoryCue;
                                        }
                                        cuePoints.push_back(cue);
                                    }

                                    tracksAdded += insertTrackIfNew(track, cuePoints);
                                }

                                if (progressCallback_ && (progress.processed % 50 == 0)) {
                                    progressCallback_(progress);
                                }
                            }
                        }

                        if (trackDb_) trackDb_->commitTransaction();
                    }
                }
            }
            break;
        }

        case DJSoftwareType::EngineDJ: {
            spdlog::info("CollectionSyncService: Reading Engine DJ database at {}", info.databasePath);
            if (pathFilter_.empty()) {
                juce::File dbf(info.databasePath);
                const std::string wm = libraryFileWatermark(dbf) + "|"
                    + libraryFileWatermark(dbf.getSiblingFile(dbf.getFileName() + "-wal"));
                if (trackDb_ && wm != "|"
                    && trackDb_->getSyncWatermark("enginedj") == wm
                    && trackDb_->getSyncCount("enginedj") % 20 != 0
                    && !trackDb_->getSyncWatermark("enginedj_pathfix").empty()) {
                    progress.currentItem = "Up to date";
                    spdlog::info("CollectionSyncService: Engine DJ database unchanged - skipped");
                    if (trackDb_) trackDb_->setSyncWatermark("enginedj", wm);
                    break;
                }
                if (trackDb_ && wm != "|") trackDb_->setSyncWatermark("enginedj", wm);
                if (trackDb_) trackDb_->setSyncWatermark("enginedj_pathfix", "done");
            }

            sqlite3* djDb = nullptr;
            int rc = sqlite3_open_v2(info.databasePath.c_str(), &djDb, SQLITE_OPEN_READONLY, nullptr);
            if (rc != SQLITE_OK) {
                spdlog::error("CollectionSyncService: Cannot open Engine DJ database");
                if (djDb) sqlite3_close(djDb);
                break;
            }

            sqlite3_stmt* stmt = nullptr;
            rc = sqlite3_prepare_v2(djDb,
                "SELECT path, title, artist, album, genre, length, bpmAnalyzed, rating, key "
                "FROM Track WHERE path IS NOT NULL",
                -1, &stmt, nullptr);

            // Fallback to simpler query if extended columns don't exist
            if (rc != SQLITE_OK) {
                rc = sqlite3_prepare_v2(djDb,
                    "SELECT path, title, artist FROM Track WHERE path IS NOT NULL",
                    -1, &stmt, nullptr);
            }

            if (rc == SQLITE_OK) {
                progress.processed = 0;
                int colCount = sqlite3_column_count(stmt);

                if (trackDb_) trackDb_->beginTransaction();

                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    if (cancelRequested_.load()) break;
                    progress.processed++;
                    const char* path   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* title  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    const char* artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                    if (path && passesPathFilter(path)) {
                        progress.currentItem = title ? title : path;

                        Models::Track track;
                        track.filePath = path;
                        track.title    = title  ? title  : "";
                        track.artist   = artist ? artist : "";
                        track.source   = sourceFromType(type);
                        track.dateAdded = now;

                        if (colCount > 3) {
                            const char* album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                            const char* genre = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                            track.album    = album ? album : "";
                            track.genre    = genre ? genre : "";
                            track.duration = sqlite3_column_double(stmt, 5);
                            track.bpm      = sqlite3_column_double(stmt, 6);
                            track.rating   = sqlite3_column_int(stmt, 7);
                            const char* keyStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                            track.key        = musicalKeyToCamelot(keyStr ? keyStr : "");
                            track.camelotKey = track.key;
                        }

                        tracksAdded += insertTrackIfNew(track);
                    }

                    if (progressCallback_ && (progress.processed % 50 == 0)) {
                        progressCallback_(progress);
                    }
                }
                progress.total = progress.processed;
                sqlite3_finalize(stmt);

                if (trackDb_) trackDb_->commitTransaction();
            }

            sqlite3_close(djDb);
            break;
        }

        case DJSoftwareType::DjayPro: {
            spdlog::info("CollectionSyncService: Reading djay Pro database at {}", info.databasePath);
            progress.total = 0;
            progress.processed = 0;
            progress.currentItem = "djay Pro sync not yet implemented";
            if (progressCallback_) progressCallback_(progress);
            break;
        }
    }

    progress.currentItem = "Complete";
    if (progress.total == 0) progress.total = progress.processed;
    if (progressCallback_) {
        progressCallback_(progress);
    }

    spdlog::info("CollectionSyncService: Sync complete for {} - {} tracks processed, {} new tracks added",
                 DJSoftwareManager::softwareTypeName(type), progress.processed, tracksAdded);
}

bool CollectionSyncService::passesPathFilter(const std::string& path) const {
    if (pathFilter_.empty()) return true;
    return pathFilter_.count(normPath(path)) > 0;
}

void CollectionSyncService::requestCancel() {
    cancelRequested_.store(true);
}

bool CollectionSyncService::syncSoftwareFiltered(DJSoftwareType type,
                                                 const std::vector<std::string>& onlyTrackPaths) {
    if (syncing_.load()) {
        spdlog::warn("CollectionSyncService: syncSoftwareFiltered refused, sync in progress");
        return false;
    }
    pathFilter_.clear();
    for (const auto& p : onlyTrackPaths)
        pathFilter_.insert(normPath(p));
    syncSoftware(type);
    pathFilter_.clear();
    return true;
}

std::vector<ExternalPlaylistDescriptor> CollectionSyncService::listPlaylists(DJSoftwareType type) {
    std::vector<ExternalPlaylistDescriptor> out;
    if (!manager_) return out;

    auto parentOf = [](const std::string& fullPath, const std::string& name) -> std::string {
        if (fullPath.size() > name.size()
            && fullPath.compare(fullPath.size() - name.size(), name.size(), name) == 0) {
            std::string parent = fullPath.substr(0, fullPath.size() - name.size());
            while (!parent.empty() && (parent.back() == '/' || parent.back() == '\\'))
                parent.pop_back();
            return parent;
        }
        return {};
    };

    switch (type) {
    case DJSoftwareType::Rekordbox: {
        std::string masterDb;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Rekordbox && !i.databasePath.empty()) {
                masterDb = i.databasePath;
                break;
            }
        }
        BeatMate::Services::Rekordbox::RekordboxDatabase db;
        bool opened = false;
        if (!masterDb.empty()) {
            std::string decrypted = masterDb + ".decrypted.db";
            if (std::filesystem::exists(decrypted))
                opened = db.openDatabase(decrypted);
            if (!opened) opened = db.openDatabase(masterDb);
        }
        if (opened) {
            for (const auto& info : db.readPlaylistsRich()) {
                ExternalPlaylistDescriptor d;
                d.externalId = info.externalId;
                d.name = info.name;
                d.isFolder = info.attribute != 0;
                if (!d.isFolder)
                    d.trackCount = static_cast<int>(db.readPlaylistContentIds(info.externalId).size());
                out.push_back(std::move(d));
            }
            db.close();
        } else {
            BeatMate::Services::Rekordbox::RekordboxEnvironment env;
            auto xmlPath = env.findXmlPath();
            if (!xmlPath.empty() && std::filesystem::exists(xmlPath)) {
                BeatMate::Services::Rekordbox::RekordboxXmlParser parser;
                if (parser.parseXml(xmlPath)) {
                    for (const auto& pl : parser.getPlaylists()) {
                        ExternalPlaylistDescriptor d;
                        d.externalId = std::to_string(pl.id);
                        d.name = pl.name;
                        d.trackCount = static_cast<int>(pl.trackIds.size());
                        out.push_back(std::move(d));
                    }
                }
            }
        }
        break;
    }
    case DJSoftwareType::Serato: {
        BeatMate::Services::Serato::SeratoDatabase sdb;
        std::string seratoFolder = BeatMate::Services::Serato::SeratoDatabase::findSeratoFolder();
        if (seratoFolder.empty() || !sdb.open(seratoFolder)) break;
        for (const auto& [name, paths] : sdb.getCrates()) {
            ExternalPlaylistDescriptor d;
            d.externalId = name;
            d.name = name;
            d.trackCount = static_cast<int>(paths.size());
            out.push_back(std::move(d));
        }
        break;
    }
    case DJSoftwareType::Traktor: {
        std::string nmlPath;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Traktor && !i.databasePath.empty()) {
                nmlPath = i.databasePath;
                break;
            }
        }
        if (nmlPath.empty()) break;
        BeatMate::Services::Traktor::TraktorCollectionParser parser;
        for (const auto& info : parser.parsePlaylists(nmlPath)) {
            ExternalPlaylistDescriptor d;
            d.externalId = info.fullPath;
            d.name = info.name;
            d.isFolder = info.isFolder;
            d.parentPath = parentOf(info.fullPath, info.name);
            d.trackCount = static_cast<int>(info.entries.size());
            out.push_back(std::move(d));
        }
        break;
    }
    case DJSoftwareType::VirtualDJ: {
        BeatMate::Services::VirtualDJ::VirtualDJPlaylistReader reader;
        for (const auto& info : reader.readPlaylists()) {
            ExternalPlaylistDescriptor d;
            d.externalId = info.fullPath;
            d.name = info.name;
            d.isFolder = info.isFolder;
            d.parentPath = parentOf(info.fullPath, info.name);
            d.trackCount = static_cast<int>(info.entries.size());
            out.push_back(std::move(d));
        }
        break;
    }
    case DJSoftwareType::EngineDJ: {
        BeatMate::Services::EngineDJ::EngineDJService svc;
        if (!svc.initialize()) break;
        for (const auto& pl : svc.readPlaylists()) {
            ExternalPlaylistDescriptor d;
            d.externalId = std::to_string(pl.engineId);
            d.name = pl.name;
            d.trackCount = static_cast<int>(pl.trackPaths.size());
            out.push_back(std::move(d));
        }
        break;
    }
    default:
        break;
    }

    spdlog::info("CollectionSyncService: listPlaylists({}) -> {} entries",
                 DJSoftwareManager::softwareTypeName(type), out.size());
    return out;
}

std::vector<std::string> CollectionSyncService::playlistTrackPaths(
    DJSoftwareType type, const std::set<std::string>& externalIds) {
    std::vector<std::string> out;
    if (!manager_ || externalIds.empty()) return out;

    switch (type) {
    case DJSoftwareType::Rekordbox: {
        std::string masterDb;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Rekordbox && !i.databasePath.empty()) {
                masterDb = i.databasePath;
                break;
            }
        }
        BeatMate::Services::Rekordbox::RekordboxDatabase db;
        bool opened = false;
        if (!masterDb.empty()) {
            std::string decrypted = masterDb + ".decrypted.db";
            if (std::filesystem::exists(decrypted))
                opened = db.openDatabase(decrypted);
            if (!opened) opened = db.openDatabase(masterDb);
        }
        if (opened) {
            for (const auto& info : db.readPlaylistsRich()) {
                if (info.attribute != 0) continue;
                if (externalIds.count(info.externalId) == 0) continue;
                for (const auto& cid : db.readPlaylistContentIds(info.externalId)) {
                    auto p = db.readContentFilePath(cid);
                    if (!p.empty()) out.push_back(std::move(p));
                }
            }
            db.close();
        } else {
            BeatMate::Services::Rekordbox::RekordboxEnvironment env;
            auto xmlPath = env.findXmlPath();
            if (!xmlPath.empty() && std::filesystem::exists(xmlPath)) {
                BeatMate::Services::Rekordbox::RekordboxXmlParser parser;
                if (parser.parseXml(xmlPath)) {
                    const auto& rbTracks = parser.getTracks();
                    for (const auto& pl : parser.getPlaylists()) {
                        if (externalIds.count(std::to_string(pl.id)) == 0) continue;
                        for (auto tid : pl.trackIds) {
                            auto it = std::find_if(rbTracks.begin(), rbTracks.end(),
                                [tid](const auto& rt) { return rt.rekordboxId == std::to_string(tid); });
                            if (it != rbTracks.end()) out.push_back(it->externalPath);
                        }
                    }
                }
            }
        }
        break;
    }
    case DJSoftwareType::Serato: {
        BeatMate::Services::Serato::SeratoDatabase sdb;
        std::string seratoFolder = BeatMate::Services::Serato::SeratoDatabase::findSeratoFolder();
        if (seratoFolder.empty() || !sdb.open(seratoFolder)) break;
        for (const auto& [name, paths] : sdb.getCrates()) {
            if (externalIds.count(name) == 0) continue;
            out.insert(out.end(), paths.begin(), paths.end());
        }
        break;
    }
    case DJSoftwareType::Traktor: {
        std::string nmlPath;
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == DJSoftwareType::Traktor && !i.databasePath.empty()) {
                nmlPath = i.databasePath;
                break;
            }
        }
        if (nmlPath.empty()) break;
        BeatMate::Services::Traktor::TraktorCollectionParser parser;
        for (const auto& info : parser.parsePlaylists(nmlPath)) {
            if (info.isFolder) continue;
            if (externalIds.count(info.fullPath) == 0) continue;
            for (const auto& e : info.entries) out.push_back(e.trackPath);
        }
        break;
    }
    case DJSoftwareType::VirtualDJ: {
        BeatMate::Services::VirtualDJ::VirtualDJPlaylistReader reader;
        for (const auto& info : reader.readPlaylists()) {
            if (info.isFolder) continue;
            if (externalIds.count(info.fullPath) == 0) continue;
            for (const auto& e : info.entries) out.push_back(e.filePath);
        }
        break;
    }
    case DJSoftwareType::EngineDJ: {
        BeatMate::Services::EngineDJ::EngineDJService svc;
        if (!svc.initialize()) break;
        for (const auto& pl : svc.readPlaylists()) {
            if (externalIds.count(std::to_string(pl.engineId)) == 0) continue;
            out.insert(out.end(), pl.trackPaths.begin(), pl.trackPaths.end());
        }
        break;
    }
    default:
        break;
    }

    spdlog::info("CollectionSyncService: playlistTrackPaths({}) -> {} paths for {} playlists",
                 DJSoftwareManager::softwareTypeName(type), out.size(), externalIds.size());
    return out;
}


bool CollectionSyncService::exportToSoftware(DJSoftwareType type, const std::vector<Models::Track>& tracks) {
    if (tracks.empty()) {
        spdlog::warn("CollectionSyncService::exportToSoftware: No tracks to export");
        return false;
    }

    spdlog::info("CollectionSyncService: Exporting {} tracks to {}", tracks.size(), DJSoftwareManager::softwareTypeName(type));

    std::string docsPath = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName().toStdString();

    switch (type) {
        case DJSoftwareType::Rekordbox: {
            BeatMate::Services::Rekordbox::RekordboxXmlExporter exporter;
            for (const auto& track : tracks) {
                BeatMate::Services::Rekordbox::RekordboxXmlExporter::ExportTrack et;
                et.trackId = track.id;
                et.filePath = track.filePath;
                et.title = track.title;
                et.artist = track.artist;
                et.album = track.album;
                et.genre = track.genre;
                et.key = track.key.empty() ? track.camelotKey : track.key;
                et.bpm = static_cast<float>(track.bpm);
                et.rating = track.rating * 51; // 0-5 → 0-255
                et.duration = track.duration;
                et.year = track.year;

                if (trackDb_ && track.id > 0) {
                    auto cues = trackDb_->getCuePoints(track.id);
                    for (const auto& cue : cues) {
                        BeatMate::Services::Rekordbox::RekordboxXmlExporter::ExportTrack::CuePointExport cp;
                        cp.number = cue.number;
                        cp.type = (cue.length > 0) ? 4 : 0; // loop or cue
                        cp.startMs = cue.position * 1000.0;
                        cp.endMs = (cue.length > 0) ? (cue.position + cue.length) * 1000.0 : -1.0;
                        cp.name = cue.name;
                        et.cuePoints.push_back(cp);
                    }
                }
                exporter.addTrack(et);
            }
            std::string outputPath = docsPath + "/BeatMate_Rekordbox_Export.xml";
            bool ok = exporter.exportToFile(outputPath);
            if (ok) spdlog::info("CollectionSyncService: Exported to {}", outputPath);
            return ok;
        }

        case DJSoftwareType::Traktor: {
            BeatMate::Services::Traktor::TraktorNmlExporter exporter;
            for (const auto& track : tracks) {
                auto cues = (trackDb_ && track.id > 0) ? trackDb_->getCuePoints(track.id) : std::vector<Models::CuePoint>{};
                exporter.addTrack(BeatMate::Services::Traktor::TraktorNmlExporter::fromBeatMateTrack(track, cues));
            }
            std::string outputPath = docsPath + "/BeatMate_Traktor_Export.nml";
            bool ok = exporter.exportToFile(outputPath);
            if (ok) spdlog::info("CollectionSyncService: Exported to {}", outputPath);
            return ok;
        }

        case DJSoftwareType::VirtualDJ: {
            BeatMate::Services::VirtualDJ::VirtualDJExporter exporter;
            for (const auto& track : tracks) {
                auto cues = (trackDb_ && track.id > 0) ? trackDb_->getCuePoints(track.id) : std::vector<Models::CuePoint>{};
                exporter.addTrack(BeatMate::Services::VirtualDJ::VirtualDJExporter::fromBeatMateTrack(track, cues));
            }
            std::string outputPath = docsPath + "/BeatMate_VirtualDJ_Export.xml";
            bool ok = exporter.exportToFile(outputPath);
            if (ok) spdlog::info("CollectionSyncService: Exported to {}", outputPath);
            return ok;
        }

        case DJSoftwareType::Serato: {
            BeatMate::Services::Serato::SeratoTagWriter writer;
            int written = 0;
            for (const auto& track : tracks) {
                if (trackDb_ && track.id > 0) {
                    auto cues = trackDb_->getCuePoints(track.id);
                    if (!cues.empty()) {
                        auto seratoCues = BeatMate::Services::Serato::SeratoTagWriter::fromBeatMateCues(cues);
                        if (writer.writeToFile(track.filePath, seratoCues)) {
                            written++;
                        }
                    }
                }
            }
            spdlog::info("CollectionSyncService: Wrote Serato markers to {} files", written);
            return written > 0;
        }

        default:
            spdlog::warn("CollectionSyncService: Export not supported for {}", DJSoftwareManager::softwareTypeName(type));
            return false;
    }
}

bool CollectionSyncService::exportPlaylistToSoftware(DJSoftwareType type, const std::string& playlistName, const std::vector<Models::Track>& tracks) {
    spdlog::info("CollectionSyncService: Exporting playlist '{}' ({} tracks) to {}", playlistName, tracks.size(), DJSoftwareManager::softwareTypeName(type));
    return exportToSoftware(type, tracks);
}

} // namespace BeatMate::Services::DJSoftware
