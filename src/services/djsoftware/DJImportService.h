#pragma once
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "CollectionSyncService.h"

namespace BeatMate::Services::DJSoftware {

struct DJImportSelection {
    DJSoftwareType type = DJSoftwareType::Rekordbox;
    bool wholeCollection = false;
    std::vector<std::string> playlistExternalIds;
    bool importPlaylists = true;
};

struct DJImportReport {
    int tracksImported = 0;
    int playlistsImported = 0;
    bool cancelled = false;
    std::string error;
};

class DJImportService {
public:
    DJImportService(std::shared_ptr<DJSoftwareManager> manager,
                    CollectionSyncService* sync,
                    std::shared_ptr<Library::TrackDatabase> db);

    std::vector<DJSoftwareInfo> detectSources();
    std::vector<ExternalPlaylistDescriptor> listPlaylists(DJSoftwareType type);
    DJImportReport importSelection(const DJImportSelection& selection,
                                   SyncProgressCallback progress);
    void cancel();

private:
    std::shared_ptr<DJSoftwareManager> manager_;
    CollectionSyncService* sync_ = nullptr;
    std::shared_ptr<Library::TrackDatabase> db_;
};

} // namespace BeatMate::Services::DJSoftware
