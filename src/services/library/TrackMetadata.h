#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackMetadata {
public:
    TrackMetadata() = default;
    ~TrackMetadata() = default;

    std::optional<Models::Track> readMetadata(const std::string& filePath);

    bool writeMetadata(const std::string& filePath, const Models::Track& track);

    std::vector<uint8_t> readAlbumArt(const std::string& filePath);
    bool writeAlbumArt(const std::string& filePath, const std::vector<uint8_t>& data);

    static bool isSupportedFormat(const std::string& filePath);
    static std::vector<std::string> getSupportedExtensions();

private:
    std::optional<Models::Track> readMP3Metadata(const std::string& filePath);
    std::optional<Models::Track> readFLACMetadata(const std::string& filePath);
    std::optional<Models::Track> readWAVMetadata(const std::string& filePath);
    std::optional<Models::Track> readOGGMetadata(const std::string& filePath);
    std::optional<Models::Track> readAACMetadata(const std::string& filePath);
    std::optional<Models::Track> readAIFFMetadata(const std::string& filePath);
    std::optional<Models::Track> readWMAMetadata(const std::string& filePath);

    std::string getFileExtension(const std::string& filePath) const;
    std::string detectFileFormat(const std::string& filePath) const;
};

}
