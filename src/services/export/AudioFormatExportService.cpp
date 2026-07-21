#include "AudioFormatExportService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <fstream>

using json = nlohmann::json;

namespace BeatMate::Services::Export {

AudioFormatExportService::AudioFormatExportService() {
    initDefaultProfiles();
}

void AudioFormatExportService::initDefaultProfiles() {
    profiles_ = {
        {"mp3-320", "MP3 320kbps CBR (DJ standard)", AudioFormat::MP3,
         MultiFormatExporter::getPresetMP3High(), QualityPreset::High},
        {"mp3-256", "MP3 256kbps CBR", AudioFormat::MP3,
         [](){ ExportOptions o; o.bitRate = 256; o.sampleRate = 44100; return o; }(), QualityPreset::Standard},
        {"mp3-192", "MP3 192kbps VBR", AudioFormat::MP3,
         MultiFormatExporter::getPresetMP3Standard(), QualityPreset::Standard},
        {"mp3-128", "MP3 128kbps (web/preview)", AudioFormat::MP3,
         [](){ ExportOptions o; o.bitRate = 128; o.sampleRate = 44100; return o; }(), QualityPreset::Draft},

        {"wav-cd", "WAV CD Quality (44.1kHz/16-bit)", AudioFormat::WAV,
         MultiFormatExporter::getPresetCD(), QualityPreset::High},
        {"wav-hires", "WAV Hi-Res (96kHz/24-bit)", AudioFormat::WAV,
         [](){ ExportOptions o; o.sampleRate = 96000; o.bitDepth = 24; return o; }(), QualityPreset::Master},
        {"wav-broadcast", "WAV Broadcast (48kHz/24-bit, EBU R128)", AudioFormat::WAV,
         MultiFormatExporter::getPresetWavBroadcast(), QualityPreset::Broadcast},
        {"wav-32float", "WAV 32-bit Float (production)", AudioFormat::WAV,
         [](){ ExportOptions o; o.sampleRate = 48000; o.bitDepth = 32; return o; }(), QualityPreset::Master},

        {"flac-cd", "FLAC CD Quality (44.1kHz/16-bit)", AudioFormat::FLAC,
         [](){ ExportOptions o; o.sampleRate = 44100; o.bitDepth = 16; o.flacCompression = 5; return o; }(), QualityPreset::High},
        {"flac-hires", "FLAC Hi-Res Archive (96kHz/24-bit)", AudioFormat::FLAC,
         MultiFormatExporter::getPresetFLACArchive(), QualityPreset::Master},

        {"ogg-high", "OGG Vorbis Quality 8", AudioFormat::OGG,
         [](){ ExportOptions o; o.sampleRate = 44100; o.oggQuality = 0.8f; return o; }(), QualityPreset::High},
        {"ogg-streaming", "OGG Vorbis Streaming", AudioFormat::OGG,
         MultiFormatExporter::getPresetOggStreaming(), QualityPreset::Streaming},

        {"aac-256", "AAC 256kbps (iTunes Plus)", AudioFormat::AAC,
         MultiFormatExporter::getPresetAACiTunes(), QualityPreset::High},
        {"aac-128", "AAC 128kbps (streaming)", AudioFormat::AAC,
         [](){ ExportOptions o; o.bitRate = 128; o.sampleRate = 44100; return o; }(), QualityPreset::Streaming},

        {"aiff-cd", "AIFF CD Quality (44.1kHz/16-bit)", AudioFormat::AIFF,
         [](){ ExportOptions o; o.sampleRate = 44100; o.bitDepth = 16; return o; }(), QualityPreset::High},
        {"aiff-production", "AIFF Production (96kHz/32-bit)", AudioFormat::AIFF,
         MultiFormatExporter::getPresetAIFFProduction(), QualityPreset::Master},
    };
}

FormatProfile AudioFormatExportService::getProfile(const std::string& name) const {
    for (const auto& p : profiles_) {
        if (p.name == name) return p;
    }
    for (const auto& p : customProfiles_) {
        if (p.name == name) return p;
    }
    return profiles_[0];
}

std::vector<FormatProfile> AudioFormatExportService::getAllProfiles() const {
    std::vector<FormatProfile> all = profiles_;
    all.insert(all.end(), customProfiles_.begin(), customProfiles_.end());
    return all;
}

std::vector<FormatProfile> AudioFormatExportService::getProfilesForFormat(AudioFormat format) const {
    std::vector<FormatProfile> result;
    for (const auto& p : profiles_) {
        if (p.format == format) result.push_back(p);
    }
    for (const auto& p : customProfiles_) {
        if (p.format == format) result.push_back(p);
    }
    return result;
}

std::vector<FormatProfile> AudioFormatExportService::getProfilesForQuality(QualityPreset quality) const {
    std::vector<FormatProfile> result;
    for (const auto& p : profiles_) {
        if (p.quality == quality) result.push_back(p);
    }
    return result;
}

void AudioFormatExportService::addCustomProfile(const FormatProfile& profile) {
    customProfiles_.push_back(profile);
    spdlog::info("AudioFormatExportService: Added custom profile '{}'", profile.name);
}

void AudioFormatExportService::removeCustomProfile(const std::string& name) {
    customProfiles_.erase(
        std::remove_if(customProfiles_.begin(), customProfiles_.end(),
                       [&name](const FormatProfile& p) { return p.name == name; }),
        customProfiles_.end());
}

ExportResult AudioFormatExportService::exportWithProfile(const Core::AudioTrack& audio,
                                                           const std::string& profileName,
                                                           const std::string& outputPath) {
    auto profile = getProfile(profileName);
    return exporter_.exportAudioTrack(audio, profile.format, profile.options, outputPath);
}

ExportResult AudioFormatExportService::exportWithQuality(const Core::AudioTrack& audio,
                                                           AudioFormat format,
                                                           QualityPreset quality,
                                                           const std::string& outputPath) {
    auto options = recommendOptions(format, qualityPresetName(quality));
    return exporter_.exportAudioTrack(audio, format, options, outputPath);
}

AudioFormat AudioFormatExportService::recommendFormat(double durationSeconds, bool needsLossless,
                                                        int64_t maxFileSizeBytes) const {
    if (needsLossless) {
        if (maxFileSizeBytes > 0) {
            int64_t flacSize = MultiFormatExporter::estimateFileSize(AudioFormat::FLAC,
                                                                      MultiFormatExporter::getPresetCD(),
                                                                      durationSeconds);
            if (flacSize <= maxFileSizeBytes) return AudioFormat::FLAC;
        }
        return AudioFormat::WAV;
    }

    if (maxFileSizeBytes > 0) {
        ExportOptions mp3Opts; mp3Opts.bitRate = 320;
        int64_t mp3Size = MultiFormatExporter::estimateFileSize(AudioFormat::MP3, mp3Opts, durationSeconds);
        if (mp3Size <= maxFileSizeBytes) return AudioFormat::MP3;

        ExportOptions aacOpts; aacOpts.bitRate = 256;
        int64_t aacSize = MultiFormatExporter::estimateFileSize(AudioFormat::AAC, aacOpts, durationSeconds);
        if (aacSize <= maxFileSizeBytes) return AudioFormat::AAC;

        return AudioFormat::OGG;
    }

    return AudioFormat::MP3;
}

ExportOptions AudioFormatExportService::recommendOptions(AudioFormat format,
                                                           const std::string& useCase) const {
    auto profiles = getProfilesForFormat(format);
    if (!profiles.empty()) return profiles[0].options;
    return ExportOptions{};
}

QualityPreset AudioFormatExportService::recommendQuality(const std::string& useCase) const {
    if (useCase == "dj" || useCase == "club") return QualityPreset::DJReady;
    if (useCase == "archive" || useCase == "master") return QualityPreset::Master;
    if (useCase == "broadcast" || useCase == "radio") return QualityPreset::Broadcast;
    if (useCase == "streaming" || useCase == "web") return QualityPreset::Streaming;
    if (useCase == "preview" || useCase == "draft") return QualityPreset::Draft;
    return QualityPreset::High;
}

std::string AudioFormatExportService::qualityPresetName(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Draft:      return "Draft";
        case QualityPreset::Standard:   return "Standard";
        case QualityPreset::High:       return "High";
        case QualityPreset::Master:     return "Master";
        case QualityPreset::Broadcast:  return "Broadcast";
        case QualityPreset::Streaming:  return "Streaming";
        case QualityPreset::DJReady:    return "DJ Ready";
    }
    return "Standard";
}

std::string AudioFormatExportService::formatDescription(AudioFormat format) {
    switch (format) {
        case AudioFormat::MP3:  return "MPEG-1 Audio Layer 3 - Universal lossy format";
        case AudioFormat::WAV:  return "Waveform Audio - Uncompressed PCM audio";
        case AudioFormat::FLAC: return "Free Lossless Audio Codec - Lossless compression";
        case AudioFormat::OGG:  return "OGG Vorbis - Open-source lossy format";
        case AudioFormat::AAC:  return "Advanced Audio Coding - Modern lossy format";
        case AudioFormat::AIFF: return "Audio Interchange File Format - Apple uncompressed";
    }
    return "";
}

bool AudioFormatExportService::saveProfiles(const std::string& filePath) const {
    json j = json::array();
    for (const auto& p : customProfiles_) {
        j.push_back({
            {"name", p.name},
            {"description", p.description},
            {"format", MultiFormatExporter::formatExtension(p.format)},
            {"bitRate", p.options.bitRate},
            {"sampleRate", p.options.sampleRate},
            {"bitDepth", p.options.bitDepth},
            {"channels", p.options.channels},
            {"quality", static_cast<int>(p.quality)}
        });
    }

    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    return true;
}

bool AudioFormatExportService::loadProfiles(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;

    try {
        auto j = json::parse(ifs);
        customProfiles_.clear();
        for (const auto& item : j) {
            FormatProfile p;
            p.name = item.value("name", "");
            p.description = item.value("description", "");
            std::string fmt = item.value("format", "wav");
            if (fmt == "mp3") p.format = AudioFormat::MP3;
            else if (fmt == "flac") p.format = AudioFormat::FLAC;
            else if (fmt == "ogg") p.format = AudioFormat::OGG;
            else if (fmt == "m4a" || fmt == "aac") p.format = AudioFormat::AAC;
            else if (fmt == "aiff") p.format = AudioFormat::AIFF;
            else p.format = AudioFormat::WAV;
            p.options.bitRate = item.value("bitRate", 320);
            p.options.sampleRate = item.value("sampleRate", 44100);
            p.options.bitDepth = item.value("bitDepth", 16);
            p.options.channels = item.value("channels", 2);
            p.quality = static_cast<QualityPreset>(item.value("quality", 2));
            customProfiles_.push_back(p);
        }
        return true;
    } catch (...) { return false; }
}

} // namespace BeatMate::Services::Export
