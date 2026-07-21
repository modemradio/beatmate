#include "DJImportService.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::DJSoftware {

DJImportService::DJImportService(std::shared_ptr<DJSoftwareManager> manager,
                                 CollectionSyncService* sync,
                                 std::shared_ptr<Library::TrackDatabase> db)
    : manager_(std::move(manager)), sync_(sync), db_(std::move(db))
{
}

std::vector<DJSoftwareInfo> DJImportService::detectSources()
{
    if (!manager_)
        return {};
    manager_->refresh();
    return manager_->getDetectedSoftware();
}

std::vector<ExternalPlaylistDescriptor> DJImportService::listPlaylists(DJSoftwareType type)
{
    if (!sync_)
        return {};
    return sync_->listPlaylists(type);
}

DJImportReport DJImportService::importSelection(const DJImportSelection& selection,
                                                SyncProgressCallback progress)
{
    DJImportReport report;
    if (!sync_ || !db_ || !db_->isOpen())
    {
        report.error = "services unavailable";
        return report;
    }

    const int64_t before = db_->getTrackCount();

    if (progress)
        sync_->setProgressCallback(progress);

    bool ran = false;
    if (selection.wholeCollection)
    {
        ran = sync_->syncSoftwareFiltered(selection.type, {});
        if (ran && selection.importPlaylists)
            report.playlistsImported = sync_->syncPlaylistsFor(selection.type);
    }
    else
    {
        std::set<std::string> ids(selection.playlistExternalIds.begin(),
                                  selection.playlistExternalIds.end());
        auto paths = sync_->playlistTrackPaths(selection.type, ids);
        if (paths.empty())
        {
            sync_->setProgressCallback(nullptr);
            report.error = "empty selection";
            return report;
        }
        ran = sync_->syncSoftwareFiltered(selection.type, paths);
        if (ran && selection.importPlaylists)
            report.playlistsImported = sync_->syncPlaylistsFor(selection.type, ids);
    }

    sync_->setProgressCallback(nullptr);

    if (!ran)
    {
        report.error = "sync busy";
        return report;
    }

    report.cancelled = sync_->wasCancelled();
    report.tracksImported = static_cast<int>(db_->getTrackCount() - before);
    spdlog::info("[DJImport] {} -> {} new tracks, {} playlists, cancelled={}",
                 DJSoftwareManager::softwareTypeName(selection.type),
                 report.tracksImported, report.playlistsImported, report.cancelled);
    return report;
}

void DJImportService::cancel()
{
    if (sync_)
        sync_->requestCancel();
}

} // namespace BeatMate::Services::DJSoftware
