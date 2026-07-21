#pragma once

#include <string>
#include <vector>

namespace BeatMate::Core {

enum class AudioFormatType {
    WAV, MP3, FLAC, OGG, AAC, AIFF, WMA, M4A, UNKNOWN
};

struct FormatInfo {
    AudioFormatType type = AudioFormatType::UNKNOWN;
    std::string name;
    std::string extension;
    bool isLossy = false;
    bool canEncode = false;
    bool canDecode = false;
};

class AudioFormat {
public:
    static AudioFormatType fromExtension(const std::string& ext);
    static std::string toExtension(AudioFormatType type);
    static std::string toName(AudioFormatType type);
    static bool isLossy(AudioFormatType type);

    static std::vector<AudioFormatType> getSupportedFormats();
    static std::vector<FormatInfo> getAllFormats();
    static bool isSupported(const std::string& extension);

    static FormatInfo getFormatInfo(AudioFormatType type);
    static FormatInfo getFormatInfoFromPath(const std::string& path);
};

} // namespace BeatMate::Core
