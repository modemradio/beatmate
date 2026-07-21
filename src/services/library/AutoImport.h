#pragma once
#include "TrackScanner.h"

namespace BeatMate::Services::Library {

TrackScanner::FileCallback makeAutoImportHandler();

void restoreWatchFoldersFromSettings(TrackScanner& scanner);

} // namespace BeatMate::Services::Library
