#include "AudioConversion.h"
#include "AudioFileReader.h"
#include "AudioFileWriter.h"
#include "AudioTrack.h"
#include "SampleRateConverter.h"
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioConversion::AudioConversion() = default;
AudioConversion::~AudioConversion() = default;

ConversionResult AudioConversion::convertFile(const std::string& inputPath,
                                               const std::string& outputPath,
                                               const ConversionOptions& options) {
    ConversionResult result;
    result.inputPath = inputPath;
    result.outputPath = outputPath;

    auto start = std::chrono::high_resolution_clock::now();

    AudioFileReader reader;
    auto track = reader.readFile(inputPath);
    if (!track) {
        result.errorMessage = "Failed to read input file";
        spdlog::error("Conversion failed: {}", result.errorMessage);
        return result;
    }

    std::shared_ptr<AudioTrack> convertedTrack = track;
    if (options.sampleRate > 0 && options.sampleRate != track->getSampleRate()) {
        SampleRateConverter src;
        auto converted = src.convert(track->getRawData(), track->getNumFrames(),
                                     track->getChannels(), track->getSampleRate(),
                                     options.sampleRate);
        convertedTrack = std::make_shared<AudioTrack>();
        convertedTrack->loadData(std::move(converted), options.sampleRate, track->getChannels());
    }

    WriteOptions writeOpts;
    writeOpts.bitRate = options.bitRate;
    writeOpts.sampleRate = options.sampleRate;
    writeOpts.bitDepth = options.bitDepth;
    writeOpts.channels = options.channels;

    AudioFileWriter writer;
    result.success = writer.writeFile(*convertedTrack, outputPath, writeOpts);

    if (!result.success) {
        result.errorMessage = "Failed to write output file";
    }

    auto end = std::chrono::high_resolution_clock::now();
    result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();

    spdlog::info("Conversion {} -> {} : {} ({:.0f}ms)",
                 inputPath, outputPath,
                 result.success ? "OK" : "FAILED", result.durationMs);
    return result;
}

std::vector<ConversionResult> AudioConversion::convertBatch(
    const std::vector<std::string>& inputPaths,
    const std::string& outputDir,
    const ConversionOptions& options,
    ConversionProgressCallback progress) {

    namespace fs = std::filesystem;
    fs::create_directories(outputDir);

    std::vector<ConversionResult> results;
    int total = static_cast<int>(inputPaths.size());

    for (int i = 0; i < total; ++i) {
        auto stem = fs::path(inputPaths[i]).stem().string();
        std::string ext = options.outputFormat.empty() ? ".wav" : ("." + options.outputFormat);
        auto outputPath = (fs::path(outputDir) / (stem + ext)).string();

        auto result = convertFile(inputPaths[i], outputPath, options);
        results.push_back(result);

        if (progress) {
            progress(i + 1, total, 1.0f);
        }
    }

    return results;
}

ConversionResult AudioConversion::convertFormat(const std::string& inputPath,
                                                 const std::string& targetFormat) {
    namespace fs = std::filesystem;
    auto stem = fs::path(inputPath).stem().string();
    auto dir = fs::path(inputPath).parent_path();
    auto outputPath = (dir / (stem + "." + targetFormat)).string();

    ConversionOptions opts;
    opts.outputFormat = targetFormat;
    return convertFile(inputPath, outputPath, opts);
}

}
