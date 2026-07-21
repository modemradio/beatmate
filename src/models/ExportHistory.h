#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct ExportHistory {
    int64_t id = 0;
    int64_t trackId = 0;
    std::string format;             // "mp3", "flac", "wav", "aac", "ogg"
    int bitRate = 320;              // kbps
    int sampleRate = 44100;
    std::string outputPath;         // full path to exported file
    int64_t exportedAt = 0;         // unix timestamp
    int64_t fileSize = 0;           // bytes

    // Export options used
    bool normalizeAudio = false;
    float targetLoudness = -14.0f;  // LUFS
    bool includeMetadata = true;
    bool includeAlbumArt = true;
    bool includeCuePoints = false;

    // Status
    std::string status;             // "success", "failed", "cancelled"
    std::string errorMessage;
    double progress = 0.0;          // 0-1

    // Source info
    std::string sourceFormat;
    int sourceBitRate = 0;
    int sourceSampleRate = 0;

    // Constructors
    ExportHistory() = default;

    ExportHistory(int64_t id, int64_t trackId, const std::string& format, const std::string& outputPath)
        : id(id), trackId(trackId), format(format), outputPath(outputPath) {}

    bool operator==(const ExportHistory& other) const { return id == other.id; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ExportHistory,
        id, trackId, format, bitRate, sampleRate, outputPath,
        exportedAt, fileSize,
        normalizeAudio, targetLoudness, includeMetadata,
        includeAlbumArt, includeCuePoints,
        status, errorMessage, progress,
        sourceFormat, sourceBitRate, sourceSampleRate
    )
};

} // namespace BeatMate::Models
