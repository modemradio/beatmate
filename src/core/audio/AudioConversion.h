#pragma once

#include <functional>
#include <string>
#include <vector>

namespace BeatMate::Core {

struct ConversionOptions {
    std::string outputFormat;  // "wav", "mp3", "flac", "ogg"
    int bitRate = 320;
    int sampleRate = 0;        // 0 = keep original
    int bitDepth = 16;
    int channels = 0;          // 0 = keep original
};

struct ConversionResult {
    std::string inputPath;
    std::string outputPath;
    bool success = false;
    std::string errorMessage;
    double durationMs = 0.0;
};

using ConversionProgressCallback = std::function<void(int current, int total, float fileProgress)>;

class AudioConversion {
public:
    AudioConversion();
    ~AudioConversion();

    ConversionResult convertFile(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ConversionOptions& options = {});

    std::vector<ConversionResult> convertBatch(
        const std::vector<std::string>& inputPaths,
        const std::string& outputDir,
        const ConversionOptions& options = {},
        ConversionProgressCallback progress = nullptr);

    ConversionResult convertFormat(const std::string& inputPath,
                                    const std::string& targetFormat);
};

} // namespace BeatMate::Core
