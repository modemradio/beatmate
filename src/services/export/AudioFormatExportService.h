#pragma once

#include <map>
#include <string>
#include <vector>

#include "MultiFormatExporter.h"

namespace BeatMate::Services::Export {

enum class QualityPreset {
    Draft,          // Low quality, fast
    Standard,       // Good quality, balanced
    High,           // High quality
    Master,         // Maximum quality, lossless
    Broadcast,      // Broadcast standard (EBU R128)
    Streaming,      // Optimized for streaming platforms
    DJReady         // Ready for DJ software (WAV/AIFF 44.1kHz 16-bit)
};

struct FormatProfile {
    std::string name;
    std::string description;
    AudioFormat format;
    ExportOptions options;
    QualityPreset quality;
};

class AudioFormatExportService {
public:
    AudioFormatExportService();
    ~AudioFormatExportService() = default;

    FormatProfile getProfile(const std::string& name) const;
    std::vector<FormatProfile> getAllProfiles() const;
    std::vector<FormatProfile> getProfilesForFormat(AudioFormat format) const;
    std::vector<FormatProfile> getProfilesForQuality(QualityPreset quality) const;
    void addCustomProfile(const FormatProfile& profile);
    void removeCustomProfile(const std::string& name);

    ExportResult exportWithProfile(const Core::AudioTrack& audio,
                                    const std::string& profileName,
                                    const std::string& outputPath);
    ExportResult exportWithQuality(const Core::AudioTrack& audio,
                                    AudioFormat format,
                                    QualityPreset quality,
                                    const std::string& outputPath);

    AudioFormat recommendFormat(double durationSeconds, bool needsLossless,
                                 int64_t maxFileSizeBytes = 0) const;
    ExportOptions recommendOptions(AudioFormat format, const std::string& useCase) const;
    QualityPreset recommendQuality(const std::string& useCase) const;

    static std::string qualityPresetName(QualityPreset preset);
    static std::string formatDescription(AudioFormat format);

    bool saveProfiles(const std::string& filePath) const;
    bool loadProfiles(const std::string& filePath);

private:
    void initDefaultProfiles();

    MultiFormatExporter exporter_;
    std::vector<FormatProfile> profiles_;
    std::vector<FormatProfile> customProfiles_;
};

} // namespace BeatMate::Services::Export
