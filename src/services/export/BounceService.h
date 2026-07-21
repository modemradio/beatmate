#pragma once
#include <functional>
#include <string>
#include <vector>
#include "MultiFormatExporter.h"
#include "../../core/audio/AudioTrack.h"
#include "../../models/Track.h"

namespace BeatMate::Services::Export {

struct BounceOptions {
    AudioFormat format = AudioFormat::WAV;
    ExportOptions exportOptions;
    double startTimeSeconds = 0.0;
    double endTimeSeconds = 0.0;   // 0 = full length
    bool normalizeOutput = false;
    float targetLUFS = -14.0f;
    bool splitByMarkers = false;
    std::string outputDirectory;
    std::string fileNamePrefix;
};

using BounceProgressCallback = std::function<void(float progress, const std::string& stage)>;

struct BounceResult {
    bool success = false;
    std::string outputPath;
    std::string errorMessage;
    double durationSeconds = 0.0;
    int64_t fileSizeBytes = 0;
    float peakLevel = 0.0f;
    float rmsLevel = 0.0f;
    float integratedLUFS = 0.0f;
    double processingTimeSeconds = 0.0;
};

class BounceService {
public:
    BounceService();
    ~BounceService() = default;

    BounceResult bounceTrack(const Core::AudioTrack& audio, const BounceOptions& options,
                               BounceProgressCallback progress = nullptr);

    BounceResult bounceMix(const std::vector<Core::AudioTrack*>& stems,
                             const std::vector<float>& volumes,
                             const std::vector<float>& pans,
                             const BounceOptions& options,
                             BounceProgressCallback progress = nullptr);

    BounceResult bounceWithCrossfade(const Core::AudioTrack& trackA,
                                       const Core::AudioTrack& trackB,
                                       double crossfadeMs,
                                       const BounceOptions& options);

    std::vector<BounceResult> bounceSplit(const Core::AudioTrack& audio,
                                           const std::vector<double>& splitPointsSeconds,
                                           const BounceOptions& options);

    bool cancelBounce();
    bool isBouncing() const { return isBouncing_; }

    float measurePeakLevel(const std::vector<float>& samples) const;
    float measureRMSLevel(const std::vector<float>& samples) const;
    float measureLUFS(const std::vector<float>& samples, int sampleRate, int channels) const;

private:
    void mixSamples(const std::vector<Core::AudioTrack*>& stems,
                     const std::vector<float>& volumes,
                     const std::vector<float>& pans,
                     std::vector<float>& output, int sampleRate, int channels);

    MultiFormatExporter exporter_;
    bool isBouncing_ = false;
    bool cancelRequested_ = false;
};

} // namespace BeatMate::Services::Export
