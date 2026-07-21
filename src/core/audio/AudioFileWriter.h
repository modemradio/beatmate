#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <string>

namespace BeatMate::Core {

class AudioTrack;

struct WriteOptions {
    int bitRate = 320;       // kbps for compressed formats
    int sampleRate = 0;      // 0 = use source
    int bitDepth = 16;       // 16 or 24 for WAV
    int channels = 0;        // 0 = use source
    float quality = 0.8f;    // 0-1 for OGG/FLAC
};

using WriteProgressCallback = std::function<void(float progress)>;

class AudioFileWriter {
public:
    AudioFileWriter();
    ~AudioFileWriter();

    bool writeWAV(const AudioTrack& track, const std::string& outputPath,
                  const WriteOptions& opts = {}, WriteProgressCallback progress = nullptr);

    bool writeMP3(const AudioTrack& track, const std::string& outputPath,
                  const WriteOptions& opts = {}, WriteProgressCallback progress = nullptr);

    bool writeFLAC(const AudioTrack& track, const std::string& outputPath,
                   const WriteOptions& opts = {}, WriteProgressCallback progress = nullptr);

    bool writeOGG(const AudioTrack& track, const std::string& outputPath,
                  const WriteOptions& opts = {}, WriteProgressCallback progress = nullptr);

    bool writeFile(const AudioTrack& track, const std::string& outputPath,
                   const WriteOptions& opts = {}, WriteProgressCallback progress = nullptr);

private:
    bool writeWithJuceFormat(juce::AudioFormat* format,
                             const AudioTrack& track, const std::string& outputPath,
                             const WriteOptions& opts, WriteProgressCallback progress);

    juce::AudioFormatManager formatManager_;
};

} // namespace BeatMate::Core
