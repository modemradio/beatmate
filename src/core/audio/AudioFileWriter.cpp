#include "AudioFileWriter.h"
#include "AudioTrack.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioFileWriter::AudioFileWriter() {
    formatManager_.registerBasicFormats();
}

AudioFileWriter::~AudioFileWriter() = default;

bool AudioFileWriter::writeWithJuceFormat(juce::AudioFormat* format,
                                           const AudioTrack& track,
                                           const std::string& outputPath,
                                           const WriteOptions& opts,
                                           WriteProgressCallback progress) {
    if (!format) {
        spdlog::error("AudioFileWriter: null format for {}", outputPath);
        return false;
    }

    int channels = (opts.channels > 0) ? opts.channels : track.getChannels();
    int sampleRate = (opts.sampleRate > 0) ? opts.sampleRate : track.getSampleRate();
    int bitDepth = opts.bitDepth;

    auto possibleBitDepths = format->getPossibleBitDepths();
    if (!possibleBitDepths.isEmpty() && !possibleBitDepths.contains(bitDepth)) {
        bitDepth = possibleBitDepths.getLast();
    }

    juce::File file(outputPath);
    if (file.exists())
        file.deleteFile();

    auto outputStream = std::make_unique<juce::FileOutputStream>(file);
    if (outputStream->failedToOpen()) {
        spdlog::error("AudioFileWriter: Cannot create output file: {}", outputPath);
        return false;
    }

    juce::StringPairArray metaData;
    int qualityIndex = 0;

    auto qualityOptions = format->getQualityOptions();
    if (!qualityOptions.isEmpty()) {
        qualityIndex = static_cast<int>(opts.quality * (qualityOptions.size() - 1));
        qualityIndex = juce::jlimit(0, qualityOptions.size() - 1, qualityIndex);
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format->createWriterFor(outputStream.get(),
                                sampleRate,
                                static_cast<unsigned int>(channels),
                                bitDepth,
                                metaData,
                                qualityIndex));

    if (!writer) {
        spdlog::error("AudioFileWriter: Failed to create writer for: {}", outputPath);
        return false;
    }

    outputStream.release();

    size_t totalFrames = track.getNumFrames();
    const float* data = track.getRawData();
    int trackChannels = track.getChannels();

    const size_t chunkSize = 65536;
    juce::AudioBuffer<float> tempBuffer(channels, static_cast<int>(chunkSize));

    size_t framesWritten = 0;
    while (framesWritten < totalFrames) {
        size_t toWrite = std::min(chunkSize, totalFrames - framesWritten);
        int numToWrite = static_cast<int>(toWrite);

        tempBuffer.setSize(channels, numToWrite, false, false, true);

        for (int s = 0; s < numToWrite; ++s) {
            for (int c = 0; c < channels; ++c) {
                int srcChannel = std::min(c, trackChannels - 1);
                float sample = data[(framesWritten + s) * trackChannels + srcChannel];
                tempBuffer.setSample(c, s, sample);
            }
        }

        if (!writer->writeFromAudioSampleBuffer(tempBuffer, 0, numToWrite)) {
            spdlog::error("AudioFileWriter: Write failed at frame {}", framesWritten);
            return false;
        }

        framesWritten += toWrite;

        if (progress) {
            progress(static_cast<float>(framesWritten) / static_cast<float>(totalFrames));
        }
    }

    spdlog::info("AudioFileWriter: Written {} ({} frames)", outputPath, totalFrames);
    return true;
}

bool AudioFileWriter::writeWAV(const AudioTrack& track, const std::string& outputPath,
                                const WriteOptions& opts, WriteProgressCallback progress) {
    juce::WavAudioFormat wavFormat;
    WriteOptions wavOpts = opts;
    if (wavOpts.bitDepth != 24 && wavOpts.bitDepth != 32) {
        wavOpts.bitDepth = 16;
    }
    return writeWithJuceFormat(&wavFormat, track, outputPath, wavOpts, progress);
}

bool AudioFileWriter::writeMP3(const AudioTrack& track, const std::string& outputPath,
                                const WriteOptions& opts, WriteProgressCallback progress) {
    namespace fs = std::filesystem;
    auto tempWav = fs::temp_directory_path() / "beatmate_temp_export.wav";

    if (!writeWAV(track, tempWav.string())) {
        return false;
    }

    int bitRate = (opts.bitRate > 0) ? opts.bitRate : 320;
    std::string cmd = "ffmpeg -y -i \"" + tempWav.string() +
                      "\" -codec:a libmp3lame -b:a " + std::to_string(bitRate) +
                      "k \"" + outputPath + "\" 2>/dev/null";

    int ret = std::system(cmd.c_str());
    fs::remove(tempWav);

    if (ret != 0) {
        spdlog::error("MP3 export requires FFmpeg installed on your system. "
                      "Please install FFmpeg (https://ffmpeg.org) and ensure it is in your PATH, "
                      "or use WAV/FLAC/OGG format instead. Failed to export: {}", outputPath);
        return false;
    }

    if (progress) progress(1.0f);
    spdlog::info("Written MP3: {} ({}kbps)", outputPath, bitRate);
    return true;
}

bool AudioFileWriter::writeFLAC(const AudioTrack& track, const std::string& outputPath,
                                 const WriteOptions& opts, WriteProgressCallback progress) {
    juce::FlacAudioFormat flacFormat;
    WriteOptions flacOpts = opts;
    if (flacOpts.bitDepth != 24) {
        flacOpts.bitDepth = 16;
    }
    return writeWithJuceFormat(&flacFormat, track, outputPath, flacOpts, progress);
}

bool AudioFileWriter::writeOGG(const AudioTrack& track, const std::string& outputPath,
                                const WriteOptions& opts, WriteProgressCallback progress) {
    juce::OggVorbisAudioFormat oggFormat;
    return writeWithJuceFormat(&oggFormat, track, outputPath, opts, progress);
}

bool AudioFileWriter::writeFile(const AudioTrack& track, const std::string& outputPath,
                                 const WriteOptions& opts, WriteProgressCallback progress) {
    auto ext = std::filesystem::path(outputPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".wav") return writeWAV(track, outputPath, opts, progress);
    if (ext == ".mp3") return writeMP3(track, outputPath, opts, progress);
    if (ext == ".flac") return writeFLAC(track, outputPath, opts, progress);
    if (ext == ".ogg") return writeOGG(track, outputPath, opts, progress);
    if (ext == ".aiff" || ext == ".aif") {
        juce::AiffAudioFormat aiffFormat;
        return writeWithJuceFormat(&aiffFormat, track, outputPath, opts, progress);
    }

    spdlog::error("Unsupported output format: {}", ext);
    return false;
}

}
