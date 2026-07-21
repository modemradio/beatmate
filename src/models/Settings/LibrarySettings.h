#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct WatchFolder {
    std::string path;
    bool enabled = true;
    bool scanSubfolders = true;
    bool autoImport = true;
    int64_t lastScanned = 0;

    WatchFolder() = default;
    explicit WatchFolder(const std::string& path) : path(path) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WatchFolder,
        path, enabled, scanSubfolders, autoImport, lastScanned
    )
};

struct LibrarySettings {
    std::vector<WatchFolder> watchFolders;
    bool autoImport = true;
    bool scanSubfolders = true;
    int scanIntervalMinutes = 30;

    bool moveToTrash = true;
    bool copyFilesToLibrary = false;
    std::string libraryFolder;
    bool organizeByArtist = false;      // auto-organize files into Artist/Album folders
    bool renameFiles = false;

    std::vector<std::string> supportedFormats = {
        "mp3", "flac", "wav", "aiff", "aac", "m4a", "ogg", "wma", "opus", "alac"
    };

    bool detectDuplicates = true;
    bool detectDuplicatesByHash = true;
    bool detectDuplicatesByMetadata = true;

    bool checkMissingFiles = true;
    bool autoRelocate = false;

    bool readMetadataOnImport = true;
    bool writeMetadataToFile = false;
    bool embedAlbumArt = false;
    int maxAlbumArtSize = 500;          // max dimension in pixels

    bool vacuumOnClose = false;
    int maxRecentSearches = 50;

    bool keepImportHistory = true;
    int maxImportHistory = 1000;

    LibrarySettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(LibrarySettings,
        watchFolders, autoImport, scanSubfolders, scanIntervalMinutes,
        moveToTrash, copyFilesToLibrary, libraryFolder,
        organizeByArtist, renameFiles,
        supportedFormats,
        detectDuplicates, detectDuplicatesByHash, detectDuplicatesByMetadata,
        checkMissingFiles, autoRelocate,
        readMetadataOnImport, writeMetadataToFile, embedAlbumArt, maxAlbumArtSize,
        vacuumOnClose, maxRecentSearches,
        keepImportHistory, maxImportHistory
    )
};

} // namespace BeatMate::Models::Settings
