#pragma once

#include <string>
#include <vector>
#include <memory>

#include <juce_core/juce_core.h>

#include "../../../models/RekordboxTrack.h"
#include "../../../models/Playlist.h"

namespace BeatMate::Services::Library {
    class PlaylistManager;
    class TrackDataProvider;
}

namespace BeatMate::Services::Rekordbox {

class RekordboxDatabase;
class RekordboxEnvironment;

struct XmlImportSummary {
    int tracksImported = 0;
    int playlistsImported = 0;
    int skipped = 0;
    std::string error;               // empty on success
    bool ok() const { return error.empty(); }
};

class RekordboxService {
public:
    RekordboxService();
    ~RekordboxService();

    bool initialize();
    bool isAvailable() const;

    std::vector<Models::RekordboxTrack> readTracks();
    std::vector<Models::Playlist> readPlaylists();
    std::vector<Models::RekordboxCue> readHotCues(const std::string& rekordboxId);

    bool syncWithLocalDatabase();

    XmlImportSummary importFromXmlFile(const juce::File& xml,
                                       BeatMate::Services::Library::PlaylistManager* playlists,
                                       BeatMate::Services::Library::TrackDataProvider* tracks);

private:
    std::unique_ptr<RekordboxDatabase> database_;
    std::unique_ptr<RekordboxEnvironment> environment_;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::Rekordbox
