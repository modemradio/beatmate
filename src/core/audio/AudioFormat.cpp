#include "AudioFormat.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace BeatMate::Core {

AudioFormatType AudioFormat::fromExtension(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.front() == '.') lower = lower.substr(1);

    if (lower == "wav") return AudioFormatType::WAV;
    if (lower == "mp3") return AudioFormatType::MP3;
    if (lower == "flac") return AudioFormatType::FLAC;
    if (lower == "ogg") return AudioFormatType::OGG;
    if (lower == "aac") return AudioFormatType::AAC;
    if (lower == "aiff" || lower == "aif") return AudioFormatType::AIFF;
    if (lower == "wma") return AudioFormatType::WMA;
    if (lower == "m4a") return AudioFormatType::M4A;
    return AudioFormatType::UNKNOWN;
}

std::string AudioFormat::toExtension(AudioFormatType type) {
    switch (type) {
        case AudioFormatType::WAV: return ".wav";
        case AudioFormatType::MP3: return ".mp3";
        case AudioFormatType::FLAC: return ".flac";
        case AudioFormatType::OGG: return ".ogg";
        case AudioFormatType::AAC: return ".aac";
        case AudioFormatType::AIFF: return ".aiff";
        case AudioFormatType::WMA: return ".wma";
        case AudioFormatType::M4A: return ".m4a";
        default: return "";
    }
}

std::string AudioFormat::toName(AudioFormatType type) {
    switch (type) {
        case AudioFormatType::WAV: return "WAV";
        case AudioFormatType::MP3: return "MP3";
        case AudioFormatType::FLAC: return "FLAC";
        case AudioFormatType::OGG: return "OGG Vorbis";
        case AudioFormatType::AAC: return "AAC";
        case AudioFormatType::AIFF: return "AIFF";
        case AudioFormatType::WMA: return "WMA";
        case AudioFormatType::M4A: return "M4A";
        default: return "Unknown";
    }
}

bool AudioFormat::isLossy(AudioFormatType type) {
    switch (type) {
        case AudioFormatType::MP3:
        case AudioFormatType::OGG:
        case AudioFormatType::AAC:
        case AudioFormatType::WMA:
        case AudioFormatType::M4A:
            return true;
        default:
            return false;
    }
}

std::vector<AudioFormatType> AudioFormat::getSupportedFormats() {
    return {
        AudioFormatType::WAV, AudioFormatType::MP3, AudioFormatType::FLAC,
        AudioFormatType::OGG, AudioFormatType::AAC, AudioFormatType::AIFF,
        AudioFormatType::WMA, AudioFormatType::M4A
    };
}

std::vector<FormatInfo> AudioFormat::getAllFormats() {
    std::vector<FormatInfo> formats;
    for (auto type : getSupportedFormats()) {
        formats.push_back(getFormatInfo(type));
    }
    return formats;
}

bool AudioFormat::isSupported(const std::string& extension) {
    return fromExtension(extension) != AudioFormatType::UNKNOWN;
}

FormatInfo AudioFormat::getFormatInfo(AudioFormatType type) {
    FormatInfo info;
    info.type = type;
    info.name = toName(type);
    info.extension = toExtension(type);
    info.isLossy = isLossy(type);

    switch (type) {
        case AudioFormatType::WAV:
            info.canDecode = true; info.canEncode = true; break;
        case AudioFormatType::MP3:
            info.canDecode = true; info.canEncode = true; break;
        case AudioFormatType::FLAC:
            info.canDecode = true; info.canEncode = true; break;
        case AudioFormatType::OGG:
            info.canDecode = true; info.canEncode = true; break;
        case AudioFormatType::AAC:
            info.canDecode = true; info.canEncode = true; break;
        case AudioFormatType::AIFF:
            info.canDecode = true; info.canEncode = false; break;
        case AudioFormatType::WMA:
            info.canDecode = true; info.canEncode = false; break;
        case AudioFormatType::M4A:
            info.canDecode = true; info.canEncode = true; break;
        default:
            break;
    }
    return info;
}

FormatInfo AudioFormat::getFormatInfoFromPath(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    auto type = fromExtension(ext);
    return getFormatInfo(type);
}

} // namespace BeatMate::Core
