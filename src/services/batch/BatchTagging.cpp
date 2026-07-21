#include "BatchTagging.h"
#include "../library/TrackMetadata.h"
#include <spdlog/spdlog.h>
namespace BeatMate::Services::Batch {
bool BatchTagging::tagAll(const std::vector<Models::Track>& tracks, const std::map<std::string, std::string>& tags,
                           TaggingProgressCallback callback) {
    cancelled_ = false;
    Library::TrackMetadata writer;
    int total = static_cast<int>(tracks.size());
    int errors = 0;
    for (int i = 0; i < total; ++i) {
        if (cancelled_) { spdlog::info("BatchTagging: Cancelled"); return false; }
        Models::Track t = tracks[static_cast<size_t>(i)];
        for (const auto& [key, value] : tags) {
            if (key == "genre") t.genre = value;
            else if (key == "artist") t.artist = value;
            else if (key == "album") t.album = value;
            else if (key == "title") t.title = value;
        }
        if (t.filePath.empty() || !writer.writeMetadata(t.filePath, t)) {
            errors++;
            spdlog::warn("BatchTagging: Failed to write tags for '{}'", t.title);
        }
        if (callback) callback(i + 1, total, t.title);
    }
    spdlog::info("BatchTagging: Tagged {} tracks ({} errors)", total, errors);
    return errors == 0;
}
} // namespace BeatMate::Services::Batch
