#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../models/Track.h"
#include "../../core/audio/AudioTrack.h"

namespace BeatMate::Services::Export {

enum class AudioFormat {
    MP3,
    WAV,
    FLAC,
    OGG,
    AAC,
    AIFF
};

enum class MP3EncodingMode {
    CBR,    // Constant Bit Rate
    VBR,    // Variable Bit Rate
    ABR     // Average Bit Rate
};

enum class DitherType {
    None,
    Rectangular,
    Triangular,
    NoiseShaping
};

struct ExportOptions {
    int bitRate = 320;            // kbps (for lossy)
    int sampleRate = 44100;       // Hz
    int channels = 2;
    int bitDepth = 16;            // 16, 24, 32 (for PCM formats)

    MP3EncodingMode mp3Mode = MP3EncodingMode::CBR;
    int mp3VbrQuality = 0;        // 0 (best) to 9 (worst) for VBR

    int flacCompression = 5;      // 0 (fastest) to 8 (smallest)

    float oggQuality = 0.8f;      // 0.0 to 1.0

    bool aacUseHEv2 = false;      // HE-AAC v2 for low bitrates

    DitherType dither = DitherType::Triangular;
    bool normalizeBeforeExport = false;
    float targetPeakDb = -0.3f;
    float targetLUFS = -14.0f;
    bool applyFadeIn = false;
    bool applyFadeOut = false;
    double fadeInMs = 0.0;
    double fadeOutMs = 0.0;

    bool embedMetadata = true;
    bool embedAlbumArt = true;
    bool embedReplayGain = false;

    double trimStartMs = 0.0;
    double trimEndMs = 0.0;       // 0 = no trim
};

using ExportProgressCallback = std::function<void(float progress, const std::string& stage)>;

struct ExportResult {
    bool success = false;
    std::string outputPath;
    std::string errorMessage;
    int64_t fileSizeBytes = 0;
    double durationSeconds = 0.0;
    AudioFormat format;
    int actualBitRate = 0;
    int actualSampleRate = 0;
};

class MultiFormatExporter {
public:
    MultiFormatExporter();
    ~MultiFormatExporter() = default;

    ExportResult exportTrack(const Models::Track& track, AudioFormat format,
                              const ExportOptions& options, const std::string& outputPath,
                              ExportProgressCallback progress = nullptr);

    ExportResult exportAudioTrack(const Core::AudioTrack& audio, AudioFormat format,
                                   const ExportOptions& options, const std::string& outputPath,
                                   ExportProgressCallback progress = nullptr);

    std::vector<ExportResult> exportBatch(const std::vector<Models::Track>& tracks,
                                           AudioFormat format, const ExportOptions& options,
                                           const std::string& outputDir,
                                           ExportProgressCallback progress = nullptr);

    ExportResult exportToMp3(const Core::AudioTrack& audio, const ExportOptions& options,
                              const std::string& outputPath);
    ExportResult exportToWav(const Core::AudioTrack& audio, const ExportOptions& options,
                              const std::string& outputPath);
    ExportResult exportToFlac(const Core::AudioTrack& audio, const ExportOptions& options,
                               const std::string& outputPath);
    ExportResult exportToOgg(const Core::AudioTrack& audio, const ExportOptions& options,
                              const std::string& outputPath);
    ExportResult exportToAac(const Core::AudioTrack& audio, const ExportOptions& options,
                              const std::string& outputPath);
    ExportResult exportToAiff(const Core::AudioTrack& audio, const ExportOptions& options,
                               const std::string& outputPath);

    static std::string formatExtension(AudioFormat format);
    static std::string formatDisplayName(AudioFormat format);
    static std::vector<int> getSupportedBitRates(AudioFormat format);
    static std::vector<int> getSupportedSampleRates(AudioFormat format);
    static std::vector<int> getSupportedBitDepths(AudioFormat format);
    static bool isLossless(AudioFormat format);
    static int64_t estimateFileSize(AudioFormat format, const ExportOptions& options,
                                     double durationSeconds);

    static ExportOptions getPresetCD();
    static ExportOptions getPresetMP3High();
    static ExportOptions getPresetMP3Standard();
    static ExportOptions getPresetFLACArchive();
    static ExportOptions getPresetOggStreaming();
    static ExportOptions getPresetAACiTunes();
    static ExportOptions getPresetWavBroadcast();
    static ExportOptions getPresetAIFFProduction();

private:
    void applyDither(std::vector<float>& samples, int targetBitDepth, DitherType type);
    void applyFade(std::vector<float>& samples, int sampleRate, int channels,
                   double fadeInMs, double fadeOutMs);
    void normalizeAudio(std::vector<float>& samples, float targetPeakDb);
};

} // namespace BeatMate::Services::Export
