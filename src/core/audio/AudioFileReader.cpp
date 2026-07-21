#include "AudioFileReader.h"
#include "AudioTrack.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

AudioFileReader::AudioFileReader() {
    formatManager_.registerBasicFormats();
}

AudioFileReader::~AudioFileReader() = default;

std::string AudioFileReader::getExtension(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

bool AudioFileReader::isSupported(const std::string& path) {
    auto ext = getExtension(path);
    return ext == ".wav" || ext == ".mp3" || ext == ".flac" ||
           ext == ".ogg" || ext == ".aac" || ext == ".aiff" ||
           ext == ".wma" || ext == ".m4a";
}

std::shared_ptr<AudioTrack> AudioFileReader::readFile(const std::string& path) {
    return readFile(path, nullptr);
}

std::shared_ptr<AudioTrack> AudioFileReader::readFile(const std::string& path,
                                                       ReadProgressCallback progress) {
    // Unicode-safe existence check: on MSVC std::filesystem::path built from a
    if (!juce::File(path).existsAsFile()) {
        spdlog::error("AudioFileReader: file not found: {}", path);
        return nullptr;
    }

    auto track = readWithJuce(path, progress);

    if (track) {
        track->setFilePath(path);
        auto stem = juce::File(path).getFileNameWithoutExtension().toStdString();
        track->setTitle(stem);
    }

    return track;
}

std::shared_ptr<AudioTrack> AudioFileReader::readRange(const std::string& path,
                                                        double startSeconds,
                                                        double durationSeconds) {
    // Stream-read only the requested range directly from disk (fast, no full decode)
    juce::File file(path);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));
    if (!reader) {
        spdlog::error("AudioFileReader: JUCE cannot read format: {}", path);
        return nullptr;
    }

    int channels = static_cast<int>(reader->numChannels);
    int sampleRate = static_cast<int>(reader->sampleRate);
    juce::int64 totalFrames = reader->lengthInSamples;

    // Same guards as readWithJuce — corrupted headers can return ch=0 / sr=0
    if (channels <= 0 || sampleRate <= 0 || totalFrames <= 0) {
        spdlog::warn("AudioFileReader::readRange: invalid header ch={} sr={} frames={} file={}",
                     channels, sampleRate, (long long) totalFrames, path);
        return nullptr;
    }

    juce::int64 startFrame = static_cast<juce::int64>(startSeconds * sampleRate);
    juce::int64 numFrames = static_cast<juce::int64>(durationSeconds * sampleRate);
    numFrames = std::min(numFrames, totalFrames - startFrame);
    if (numFrames <= 0) return nullptr;

    juce::AudioBuffer<float> tempBuffer(channels, static_cast<int>(numFrames));
    reader->read(&tempBuffer, 0, static_cast<int>(numFrames), startFrame, true, true);

    std::vector<float> pcmData(static_cast<size_t>(numFrames * channels));
    const int ch = channels;
    const int nf = static_cast<int>(numFrames);
    if (ch == 2) {
        const float* L = tempBuffer.getReadPointer(0);
        const float* R = tempBuffer.getReadPointer(1);
        float* dst = pcmData.data();
        for (int s = 0; s < nf; ++s) {
            dst[s * 2]     = L[s];
            dst[s * 2 + 1] = R[s];
        }
    } else {
        for (int c = 0; c < ch; ++c) {
            const float* src = tempBuffer.getReadPointer(c);
            float* dst = pcmData.data() + c;
            for (int s = 0; s < nf; ++s) dst[s * ch] = src[s];
        }
    }

    auto track = std::make_shared<AudioTrack>();
    track->loadData(std::move(pcmData), sampleRate, channels);
    track->setFilePath(path);
    auto stem = juce::File(path).getFileNameWithoutExtension().toStdString();
    track->setTitle(stem);
    return track;
}

AudioFileInfo AudioFileReader::getFileInfo(const std::string& path) {
    AudioFileInfo info;

    juce::File file(path);
    if (!file.existsAsFile()) return info;
    info.fileSizeBytes = static_cast<int64_t>(file.getSize());

    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));

    if (reader) {
        info.sampleRate = static_cast<int>(reader->sampleRate);
        info.channels = static_cast<int>(reader->numChannels);
        info.bitsPerSample = static_cast<int>(reader->bitsPerSample);
        info.totalFrames = static_cast<size_t>(reader->lengthInSamples);
        info.duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        info.format = reader->getFormatName().toStdString();
    }

    return info;
}

std::shared_ptr<AudioTrack> AudioFileReader::readWithJuce(const std::string& path,
                                                           ReadProgressCallback progress) {
    juce::File file(path);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager_.createReaderFor(file));

    if (!reader) {
        spdlog::error("AudioFileReader: JUCE cannot read format: {}", path);
        return nullptr;
    }

    int channels = static_cast<int>(reader->numChannels);
    int sampleRate = static_cast<int>(reader->sampleRate);
    juce::int64 totalFrames = reader->lengthInSamples;

    // Garde contre en-têtes corrompus (ch/sr bidons).
    if (channels <= 0 || sampleRate <= 0 || totalFrames <= 0) {
        spdlog::warn("AudioFileReader: invalid header ch={} sr={} frames={} file={}",
                     channels, sampleRate, (long long) totalFrames, path);
        return nullptr;
    }
    // Prevent int overflow in the flat buffer size computation.
    if (totalFrames > (std::numeric_limits<juce::int64>::max() / std::max(1, channels))) {
        spdlog::warn("AudioFileReader: frame*channel overflow ch={} frames={} file={}",
                     channels, (long long) totalFrames, path);
        return nullptr;
    }

    const juce::int64 chunkSize = 65536;
    std::vector<float> pcmData(static_cast<size_t>(totalFrames * channels));

    juce::AudioBuffer<float> tempBuffer(channels, static_cast<int>(std::min(chunkSize, totalFrames)));

    juce::int64 framesRead = 0;
    while (framesRead < totalFrames) {
        juce::int64 toRead = std::min(chunkSize, totalFrames - framesRead);
        int numToRead = static_cast<int>(toRead);

        tempBuffer.setSize(channels, numToRead, false, false, true);
        reader->read(&tempBuffer, 0, numToRead, framesRead, true, true);

        for (int s = 0; s < numToRead; ++s) {
            for (int c = 0; c < channels; ++c) {
                pcmData[static_cast<size_t>((framesRead + s) * channels + c)] =
                    tempBuffer.getSample(c, s);
            }
        }

        framesRead += toRead;

        if (progress) {
            progress(static_cast<float>(framesRead) / static_cast<float>(totalFrames));
        }
    }

    auto track = std::make_shared<AudioTrack>();
    track->loadData(std::move(pcmData), sampleRate, channels);

    auto ext = getExtension(path);
    spdlog::info("Loaded {}: {} ({:.1f}s, {}Hz, {} ch)",
                 ext.substr(1), path, track->getDuration(), sampleRate, channels);
    return track;
}

} // namespace BeatMate::Core
