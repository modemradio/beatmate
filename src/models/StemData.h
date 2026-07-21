#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct StemData {
    int64_t trackId = 0;

    std::string vocals;
    std::string drums;
    std::string bass;
    std::string melody;
    std::string other;

    int64_t separatedAt = 0;        // unix timestamp
    std::string modelUsed;          // e.g. "demucs_v4", "spleeter_5stems", "htdemucs"
    std::string modelVersion;

    float vocalsQuality = 0.0f;     // estimated separation quality 0-1
    float drumsQuality = 0.0f;
    float bassQuality = 0.0f;
    float melodyQuality = 0.0f;
    float otherQuality = 0.0f;

    double processingTimeSeconds = 0.0;
    std::string processingDevice;   // "cpu", "cuda", "mps"
    int sampleRate = 44100;
    std::string format;             // "wav", "flac"

    int64_t vocalsSize = 0;
    int64_t drumsSize = 0;
    int64_t bassSize = 0;
    int64_t melodySize = 0;
    int64_t otherSize = 0;

    bool isComplete = false;
    std::string status;             // "pending", "processing", "complete", "failed"
    std::string errorMessage;
    double progress = 0.0;          // 0-1

    StemData() = default;
    explicit StemData(int64_t trackId) : trackId(trackId) {}

    bool operator==(const StemData& other) const { return trackId == other.trackId; }

    [[nodiscard]] int64_t totalSize() const {
        return vocalsSize + drumsSize + bassSize + melodySize + otherSize;
    }

    [[nodiscard]] bool hasAllStems() const {
        return !vocals.empty() && !drums.empty() && !bass.empty() &&
               !melody.empty() && !other.empty();
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StemData,
        trackId, vocals, drums, bass, melody, other,
        separatedAt, modelUsed, modelVersion,
        vocalsQuality, drumsQuality, bassQuality, melodyQuality, otherQuality,
        processingTimeSeconds, processingDevice, sampleRate, format,
        vocalsSize, drumsSize, bassSize, melodySize, otherSize,
        isComplete, status, errorMessage, progress
    )
};

} // namespace BeatMate::Models
