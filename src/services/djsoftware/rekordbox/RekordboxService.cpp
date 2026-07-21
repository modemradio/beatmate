#include "RekordboxService.h"
#include "RekordboxDatabase.h"
#include "RekordboxEnvironment.h"
#include "RekordboxXmlParser.h"

#include "../../library/PlaylistManager.h"
#include "../../library/TrackDataProvider.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>
#include <chrono>

namespace BeatMate::Services::Rekordbox {

RekordboxService::RekordboxService()
    : database_(std::make_unique<RekordboxDatabase>())
    , environment_(std::make_unique<RekordboxEnvironment>()) {
}

RekordboxService::~RekordboxService() = default;

bool RekordboxService::initialize() {
    if (!environment_->isInstalled()) {
        spdlog::warn("RekordboxService: Rekordbox not found on this system");
        return false;
    }

    std::string dbPath = environment_->findDatabasePath();
    if (dbPath.empty()) {
        spdlog::error("RekordboxService: Cannot find Rekordbox database");
        return false;
    }

    if (!database_->openDatabase(dbPath)) {
        spdlog::error("RekordboxService: Failed to open Rekordbox database");
        return false;
    }

    initialized_ = true;
    spdlog::info("RekordboxService: Initialized with database at {}", dbPath);
    return true;
}

bool RekordboxService::isAvailable() const {
    return initialized_;
}

std::vector<Models::RekordboxTrack> RekordboxService::readTracks() {
    if (!initialized_) {
        spdlog::warn("RekordboxService: Not initialized");
        return {};
    }

    auto tracks = database_->readAllTracks();
    spdlog::info("RekordboxService: Read {} tracks from Rekordbox", tracks.size());
    return tracks;
}

std::vector<Models::Playlist> RekordboxService::readPlaylists() {
    if (!initialized_) return {};
    return database_->readPlaylists();
}

std::vector<Models::RekordboxCue> RekordboxService::readHotCues(const std::string& rekordboxId) {
    if (!initialized_) return {};
    return database_->readCuePoints(rekordboxId);
}

bool RekordboxService::syncWithLocalDatabase() {
    if (!initialized_) return false;
    spdlog::info("RekordboxService: Starting sync with local database");
    auto tracks = readTracks();
    spdlog::info("RekordboxService: Sync complete, {} tracks processed", tracks.size());
    return true;
}


XmlImportSummary RekordboxService::importFromXmlFile(
    const juce::File& xml,
    BeatMate::Services::Library::PlaylistManager* playlists,
    BeatMate::Services::Library::TrackDataProvider* tracks)
{
    XmlImportSummary summary;

    if (!xml.existsAsFile()) {
        summary.error = "Le fichier XML est introuvable: " + xml.getFullPathName().toStdString();
        spdlog::error("RekordboxService::importFromXmlFile: {}", summary.error);
        return summary;
    }
    if (tracks == nullptr) {
        summary.error = "TrackDataProvider indisponible - import impossible.";
        return summary;
    }
    if (playlists == nullptr) {
        summary.error = "PlaylistManager indisponible - import impossible.";
        return summary;
    }

    RekordboxXmlParser parser;
    if (!parser.parse(xml)) {
        summary.error = "Echec du parsing XML. Verifiez que le fichier est un export Rekordbox valide.";
        return summary;
    }

    const auto rbTracks    = parser.getTracks();
    const auto rbPlaylists = parser.getPlaylists();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::unordered_map<std::string, int64_t> pathToLocalId;
    {
        auto all = tracks->getAllTracks();
        pathToLocalId.reserve(all.size() * 2);
        for (const auto& t : all) {
            if (!t.filePath.empty())
                pathToLocalId.emplace(t.filePath, t.id);
        }
    }

    // rekordboxId (string from XML) -> BeatMate local track id (int64_t).
    std::unordered_map<std::string, int64_t> rbIdToLocalId;
    rbIdToLocalId.reserve(rbTracks.size() * 2);

    for (const auto& rb : rbTracks) {
        if (rb.externalPath.empty()) {
            summary.skipped++;
            continue;
        }

        int64_t localId = -1;
        auto it = pathToLocalId.find(rb.externalPath);

        Models::Track t;
        if (it != pathToLocalId.end()) {
            // Existing record: fetch it to preserve unknown fields.
            localId = it->second;
            t = tracks->getTrack(localId);
            if (t.id == 0) t.id = localId;
        }

        t.filePath = rb.externalPath;
        if (!rb.title.empty())   t.title   = rb.title;
        if (!rb.artist.empty())  t.artist  = rb.artist;
        if (!rb.album.empty())   t.album   = rb.album;
        if (!rb.genre.empty())   t.genre   = rb.genre;
        if (!rb.label.empty())   t.label   = rb.label;
        if (!rb.comment.empty()) t.comment = rb.comment;
        if (rb.year > 0)         t.year    = rb.year;
        if (rb.duration > 0.0)   t.duration = rb.duration;
        if (rb.bpm > 0.0)        t.bpm     = rb.bpm;
        if (!rb.tonality.empty()) {
            t.key = rb.tonality;
            if (t.camelotKey.empty()) t.camelotKey = rb.tonality;
        }
        if (rb.rating > 0)       t.rating = rb.rating;
        if (!rb.color.empty())   t.color  = rb.color;
        // playCount isn't carried by RekordboxTrack in this parser; keep as-is.
        t.source = Models::TrackSource::Rekordbox;
        if (t.dateAdded == 0)    t.dateAdded = now;
        t.lastModified = now;

        if (localId < 0) {
            int64_t newId = tracks->addTrack(t);
            if (newId > 0) {
                localId = newId;
                pathToLocalId.emplace(t.filePath, newId);
                summary.tracksImported++;
            } else {
                summary.skipped++;
                continue;
            }
        } else {
            tracks->updateTrack(t);
            summary.tracksImported++;
        }

        if (!rb.rekordboxId.empty())
            rbIdToLocalId[rb.rekordboxId] = localId;

        for (const auto& cue : rb.hotCues) {
            Models::CuePoint cp;
            cp.trackId  = localId;
            cp.type     = cue.isLoop ? Models::CuePointType::Loop : Models::CuePointType::HotCue;
            cp.position = cue.position;
            cp.length   = cue.length;
            cp.name     = cue.name;
            cp.color    = cue.color;
            cp.number   = cue.number;
            tracks->saveCuePoint(cp);
        }
    }

    std::unordered_map<int64_t, int64_t> folderIdMap;
    folderIdMap[-1] = -1; // root stays root

    for (const auto& pl : rbPlaylists) {
        int64_t parentBeatMateId = -1;
        auto pit = folderIdMap.find(pl.parentFolderId);
        if (pit != folderIdMap.end()) parentBeatMateId = pit->second;

        // A pure folder in our parser: id < -1 and trackIds empty.
        const bool isFolder = (pl.id < -1) && pl.trackIds.empty();

        if (isFolder) {
            int64_t folderId = playlists->createFolder(pl.name, parentBeatMateId);
            if (folderId > 0) {
                folderIdMap[pl.id] = folderId;
            }
            continue;
        }

        int64_t plId = -1;
        {
            auto existing = playlists->getAllPlaylists();
            for (const auto& e : existing) {
                if (e.name == pl.name && e.parentFolderId == parentBeatMateId) {
                    plId = e.id;
                    break;
                }
            }
        }
        if (plId <= 0) {
            plId = playlists->createPlaylist(pl.name, "Importe depuis Rekordbox XML");
            if (plId > 0 && parentBeatMateId > 0) {
                playlists->movePlaylistToFolder(plId, parentBeatMateId);
            }
        }
        if (plId <= 0) {
            summary.skipped++;
            continue;
        }

        for (int64_t rbKey : pl.trackIds) {
            auto rit = rbIdToLocalId.find(std::to_string(rbKey));
            if (rit == rbIdToLocalId.end()) continue;
            playlists->addTrack(plId, rit->second);
        }
        summary.playlistsImported++;
    }

    spdlog::info("RekordboxService::importFromXmlFile: tracks={}, playlists={}, skipped={}",
                 summary.tracksImported, summary.playlistsImported, summary.skipped);
    return summary;
}

} // namespace BeatMate::Services::Rekordbox
