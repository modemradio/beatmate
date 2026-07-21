#include "MultiFormatExporter.h"

#include <spdlog/spdlog.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

namespace BeatMate::Services::Export {

MultiFormatExporter::MultiFormatExporter() = default;


ExportResult MultiFormatExporter::exportTrack(const Models::Track& track, AudioFormat format,
                                                const ExportOptions& options,
                                                const std::string& outputPath,
                                                ExportProgressCallback progress) {
    ExportResult result;
    result.format = format;

    if (progress) progress(0.0f, "Loading audio...");

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(juce::File(track.filePath)));

    if (!reader) {
        result.errorMessage = "Failed to load audio file: " + track.filePath;
        spdlog::error("MultiFormatExporter: {}", result.errorMessage);
        return result;
    }

    Core::AudioTrack audio;
    int numChannels = static_cast<int>(reader->numChannels);
    int64_t numSamples = reader->lengthInSamples;

    juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(numSamples));
    reader->read(&buffer, 0, static_cast<int>(numSamples), 0, true, true);

    std::vector<float> interleaved(numSamples * numChannels);
    for (int64_t i = 0; i < numSamples; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            interleaved[i * numChannels + ch] = buffer.getSample(ch, static_cast<int>(i));
        }
    }

    audio.loadData(std::move(interleaved), static_cast<int>(reader->sampleRate), numChannels);

    if (progress) progress(0.3f, "Processing audio...");

    return exportAudioTrack(audio, format, options, outputPath, progress);
}

ExportResult MultiFormatExporter::exportAudioTrack(const Core::AudioTrack& audio, AudioFormat format,
                                                     const ExportOptions& options,
                                                     const std::string& outputPath,
                                                     ExportProgressCallback progress) {
    ExportResult result;
    result.format = format;
    result.outputPath = outputPath;

    if (!audio.isLoaded()) {
        result.errorMessage = "Audio track not loaded";
        return result;
    }

    size_t totalSamples = audio.getTotalSamples();
    std::vector<float> samples(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i) {
        samples[i] = audio.getSample(i);
    }

    int channels = audio.getChannels();
    int sampleRate = audio.getSampleRate();

    if (options.trimStartMs > 0.0 || options.trimEndMs > 0.0) {
        size_t startSample = static_cast<size_t>(options.trimStartMs / 1000.0 * sampleRate * channels);
        size_t endSample = (options.trimEndMs > 0.0)
                               ? static_cast<size_t>(options.trimEndMs / 1000.0 * sampleRate * channels)
                               : totalSamples;
        endSample = std::min(endSample, totalSamples);
        startSample = std::min(startSample, endSample);
        samples = std::vector<float>(samples.begin() + startSample, samples.begin() + endSample);
        totalSamples = samples.size();
    }

    if (options.normalizeBeforeExport) {
        normalizeAudio(samples, options.targetPeakDb);
    }

    if (options.applyFadeIn || options.applyFadeOut) {
        applyFade(samples, sampleRate, channels,
                  options.applyFadeIn ? options.fadeInMs : 0.0,
                  options.applyFadeOut ? options.fadeOutMs : 0.0);
    }

    if (options.bitDepth < 32 && options.dither != DitherType::None) {
        applyDither(samples, options.bitDepth, options.dither);
    }

    if (progress) progress(0.5f, "Encoding...");

    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());

    juce::File outFile(outputPath);
    std::unique_ptr<juce::AudioFormat> juceFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    int targetSampleRate = (options.sampleRate > 0) ? options.sampleRate : sampleRate;
    int targetChannels = (options.channels > 0) ? options.channels : channels;

    auto outStream = outFile.createOutputStream();
    if (!outStream) {
        result.errorMessage = "Failed to create output file: " + outputPath;
        return result;
    }

    switch (format) {
        case AudioFormat::WAV: {
            juce::WavAudioFormat wavFormat;
            writer.reset(wavFormat.createWriterFor(
                outStream.release(), targetSampleRate, targetChannels,
                options.bitDepth, {}, 0));
            break;
        }
        case AudioFormat::FLAC: {
            juce::FlacAudioFormat flacFormat;
            writer.reset(flacFormat.createWriterFor(
                outStream.release(), targetSampleRate, targetChannels,
                options.bitDepth, {}, options.flacCompression));
            break;
        }
        case AudioFormat::AIFF: {
            juce::AiffAudioFormat aiffFormat;
            writer.reset(aiffFormat.createWriterFor(
                outStream.release(), targetSampleRate, targetChannels,
                options.bitDepth, {}, 0));
            break;
        }
        case AudioFormat::OGG: {
            juce::OggVorbisAudioFormat oggFormat;
            writer.reset(oggFormat.createWriterFor(
                outStream.release(), targetSampleRate, targetChannels,
                options.bitDepth, {}, static_cast<int>(options.oggQuality * 10)));
            break;
        }
        case AudioFormat::MP3:
        case AudioFormat::AAC:
        default: {
            juce::WavAudioFormat wavFormat;
            writer.reset(wavFormat.createWriterFor(
                outStream.release(), targetSampleRate, targetChannels,
                options.bitDepth, {}, 0));
            spdlog::info("MultiFormatExporter: {} export via WAV (external encoder needed)",
                         formatDisplayName(format));
            break;
        }
    }

    if (!writer) {
        result.errorMessage = "Failed to create audio writer for format " + formatDisplayName(format);
        return result;
    }

    size_t numFrames = totalSamples / channels;
    juce::AudioBuffer<float> writeBuffer(channels, static_cast<int>(numFrames));

    for (size_t f = 0; f < numFrames; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t idx = f * channels + ch;
            if (idx < samples.size()) {
                writeBuffer.setSample(ch, static_cast<int>(f), samples[idx]);
            }
        }
    }

    writer->writeFromAudioSampleBuffer(writeBuffer, 0, static_cast<int>(numFrames));
    writer.reset();

    if (progress) progress(0.95f, "Finalizing...");

    result.success = true;
    result.durationSeconds = static_cast<double>(numFrames) / targetSampleRate;
    result.actualSampleRate = targetSampleRate;
    result.actualBitRate = isLossless(format) ? (targetSampleRate * targetChannels * options.bitDepth / 1000)
                                               : options.bitRate;

    try {
        result.fileSizeBytes = static_cast<int64_t>(std::filesystem::file_size(outputPath));
    } catch (...) {}

    if (progress) progress(1.0f, "Complete");

    spdlog::info("MultiFormatExporter: Exported '{}' ({}, {:.1f}s, {} bytes)",
                 outputPath, formatDisplayName(format), result.durationSeconds, result.fileSizeBytes);

    return result;
}


std::vector<ExportResult> MultiFormatExporter::exportBatch(
    const std::vector<Models::Track>& tracks, AudioFormat format,
    const ExportOptions& options, const std::string& outputDir,
    ExportProgressCallback progress) {

    std::vector<ExportResult> results;
    std::filesystem::create_directories(outputDir);

    for (size_t i = 0; i < tracks.size(); ++i) {
        float baseProgress = static_cast<float>(i) / static_cast<float>(tracks.size());
        if (progress) progress(baseProgress, "Exporting " + std::to_string(i + 1) + "/" +
                                                 std::to_string(tracks.size()));

        std::string filename = tracks[i].artist + " - " + tracks[i].title + "." +
                               formatExtension(format);
        for (auto& c : filename) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }

        std::string outputPath = outputDir + "/" + filename;
        results.push_back(exportTrack(tracks[i], format, options, outputPath));
    }

    int successCount = 0;
    for (const auto& r : results) if (r.success) successCount++;
    spdlog::info("MultiFormatExporter: Batch export complete: {}/{} succeeded",
                 successCount, results.size());

    return results;
}


ExportResult MultiFormatExporter::exportToMp3(const Core::AudioTrack& audio,
                                                const ExportOptions& options,
                                                const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::MP3, options, outputPath);
}

ExportResult MultiFormatExporter::exportToWav(const Core::AudioTrack& audio,
                                                const ExportOptions& options,
                                                const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::WAV, options, outputPath);
}

ExportResult MultiFormatExporter::exportToFlac(const Core::AudioTrack& audio,
                                                 const ExportOptions& options,
                                                 const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::FLAC, options, outputPath);
}

ExportResult MultiFormatExporter::exportToOgg(const Core::AudioTrack& audio,
                                                const ExportOptions& options,
                                                const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::OGG, options, outputPath);
}

ExportResult MultiFormatExporter::exportToAac(const Core::AudioTrack& audio,
                                                const ExportOptions& options,
                                                const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::AAC, options, outputPath);
}

ExportResult MultiFormatExporter::exportToAiff(const Core::AudioTrack& audio,
                                                 const ExportOptions& options,
                                                 const std::string& outputPath) {
    return exportAudioTrack(audio, AudioFormat::AIFF, options, outputPath);
}


std::string MultiFormatExporter::formatExtension(AudioFormat format) {
    switch (format) {
        case AudioFormat::MP3:  return "mp3";
        case AudioFormat::WAV:  return "wav";
        case AudioFormat::FLAC: return "flac";
        case AudioFormat::OGG:  return "ogg";
        case AudioFormat::AAC:  return "m4a";
        case AudioFormat::AIFF: return "aiff";
    }
    return "wav";
}

std::string MultiFormatExporter::formatDisplayName(AudioFormat format) {
    switch (format) {
        case AudioFormat::MP3:  return "MP3";
        case AudioFormat::WAV:  return "WAV";
        case AudioFormat::FLAC: return "FLAC";
        case AudioFormat::OGG:  return "OGG Vorbis";
        case AudioFormat::AAC:  return "AAC";
        case AudioFormat::AIFF: return "AIFF";
    }
    return "Unknown";
}

std::vector<int> MultiFormatExporter::getSupportedBitRates(AudioFormat format) {
    switch (format) {
        case AudioFormat::MP3:  return {128, 160, 192, 224, 256, 320};
        case AudioFormat::AAC:  return {96, 128, 160, 192, 256, 320};
        case AudioFormat::OGG:  return {96, 128, 160, 192, 224, 256, 320, 500};
        default: return {};
    }
}

std::vector<int> MultiFormatExporter::getSupportedSampleRates(AudioFormat format) {
    switch (format) {
        case AudioFormat::MP3:  return {22050, 32000, 44100, 48000};
        case AudioFormat::AAC:  return {22050, 32000, 44100, 48000, 96000};
        case AudioFormat::OGG:  return {22050, 32000, 44100, 48000, 96000};
        case AudioFormat::WAV:  return {22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
        case AudioFormat::FLAC: return {22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
        case AudioFormat::AIFF: return {22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
    }
    return {44100, 48000};
}

std::vector<int> MultiFormatExporter::getSupportedBitDepths(AudioFormat format) {
    switch (format) {
        case AudioFormat::WAV:  return {16, 24, 32};
        case AudioFormat::FLAC: return {16, 24};
        case AudioFormat::AIFF: return {16, 24, 32};
        default: return {16};
    }
}

bool MultiFormatExporter::isLossless(AudioFormat format) {
    return format == AudioFormat::WAV || format == AudioFormat::FLAC || format == AudioFormat::AIFF;
}

int64_t MultiFormatExporter::estimateFileSize(AudioFormat format, const ExportOptions& options,
                                                double durationSeconds) {
    if (isLossless(format)) {
        double bytesPerSec = options.sampleRate * options.channels * (options.bitDepth / 8.0);
        double compressionRatio = (format == AudioFormat::FLAC) ? 0.6 : 1.0;
        return static_cast<int64_t>(bytesPerSec * durationSeconds * compressionRatio);
    } else {
        return static_cast<int64_t>(options.bitRate * 1000.0 / 8.0 * durationSeconds);
    }
}


ExportOptions MultiFormatExporter::getPresetCD() {
    ExportOptions o;
    o.sampleRate = 44100; o.bitDepth = 16; o.channels = 2;
    return o;
}

ExportOptions MultiFormatExporter::getPresetMP3High() {
    ExportOptions o;
    o.bitRate = 320; o.sampleRate = 44100; o.mp3Mode = MP3EncodingMode::CBR;
    return o;
}

ExportOptions MultiFormatExporter::getPresetMP3Standard() {
    ExportOptions o;
    o.bitRate = 192; o.sampleRate = 44100; o.mp3Mode = MP3EncodingMode::VBR; o.mp3VbrQuality = 2;
    return o;
}

ExportOptions MultiFormatExporter::getPresetFLACArchive() {
    ExportOptions o;
    o.sampleRate = 96000; o.bitDepth = 24; o.flacCompression = 8;
    return o;
}

ExportOptions MultiFormatExporter::getPresetOggStreaming() {
    ExportOptions o;
    o.sampleRate = 44100; o.oggQuality = 0.6f; o.bitRate = 160;
    return o;
}

ExportOptions MultiFormatExporter::getPresetAACiTunes() {
    ExportOptions o;
    o.bitRate = 256; o.sampleRate = 44100;
    return o;
}

ExportOptions MultiFormatExporter::getPresetWavBroadcast() {
    ExportOptions o;
    o.sampleRate = 48000; o.bitDepth = 24; o.channels = 2;
    o.normalizeBeforeExport = true; o.targetLUFS = -23.0f;
    return o;
}

ExportOptions MultiFormatExporter::getPresetAIFFProduction() {
    ExportOptions o;
    o.sampleRate = 96000; o.bitDepth = 32; o.channels = 2;
    return o;
}


void MultiFormatExporter::applyDither(std::vector<float>& samples, int targetBitDepth,
                                        DitherType type) {
    if (type == DitherType::None) return;

    float quantizationStep = 1.0f / static_cast<float>(1 << (targetBitDepth - 1));
    std::mt19937 gen(42); // Deterministic seed for reproducibility
    std::uniform_real_distribution<float> uniformDist(-0.5f, 0.5f);

    float prevNoise = 0.0f;

    for (auto& sample : samples) {
        float noise = 0.0f;
        switch (type) {
            case DitherType::Rectangular:
                noise = uniformDist(gen) * quantizationStep;
                break;
            case DitherType::Triangular: {
                float r1 = uniformDist(gen);
                float r2 = uniformDist(gen);
                noise = (r1 + r2) * quantizationStep * 0.5f;
                break;
            }
            case DitherType::NoiseShaping: {
                float r1 = uniformDist(gen);
                float r2 = uniformDist(gen);
                noise = (r1 + r2) * quantizationStep * 0.5f - prevNoise * 0.5f;
                prevNoise = noise;
                break;
            }
            default:
                break;
        }

        sample += noise;
        sample = std::round(sample / quantizationStep) * quantizationStep;
        sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

void MultiFormatExporter::applyFade(std::vector<float>& samples, int sampleRate, int channels,
                                      double fadeInMs, double fadeOutMs) {
    size_t totalFrames = samples.size() / channels;

    if (fadeInMs > 0.0) {
        size_t fadeInFrames = static_cast<size_t>(fadeInMs / 1000.0 * sampleRate);
        fadeInFrames = std::min(fadeInFrames, totalFrames);
        for (size_t f = 0; f < fadeInFrames; ++f) {
            float gain = static_cast<float>(f) / static_cast<float>(fadeInFrames);
            gain = std::sin(gain * 3.14159265f * 0.5f);
            for (int ch = 0; ch < channels; ++ch) {
                samples[f * channels + ch] *= gain;
            }
        }
    }

    if (fadeOutMs > 0.0) {
        size_t fadeOutFrames = static_cast<size_t>(fadeOutMs / 1000.0 * sampleRate);
        fadeOutFrames = std::min(fadeOutFrames, totalFrames);
        for (size_t f = 0; f < fadeOutFrames; ++f) {
            float gain = static_cast<float>(fadeOutFrames - f) / static_cast<float>(fadeOutFrames);
            gain = std::sin(gain * 3.14159265f * 0.5f);
            size_t idx = (totalFrames - fadeOutFrames + f) * channels;
            for (int ch = 0; ch < channels; ++ch) {
                if (idx + ch < samples.size()) {
                    samples[idx + ch] *= gain;
                }
            }
        }
    }
}

void MultiFormatExporter::normalizeAudio(std::vector<float>& samples, float targetPeakDb) {
    float peak = 0.0f;
    for (auto s : samples) peak = std::max(peak, std::abs(s));

    if (peak < 1e-10f) return;

    float targetPeakLinear = std::pow(10.0f, targetPeakDb / 20.0f);
    float gain = targetPeakLinear / peak;

    for (auto& s : samples) s *= gain;
}

} // namespace BeatMate::Services::Export
