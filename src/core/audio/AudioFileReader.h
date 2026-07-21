#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <string>
#include <functional>

namespace BeatMate::Core {

class AudioTrack;

struct AudioFileInfo {
    std::string format;
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    double duration = 0.0;
    size_t totalFrames = 0;
    int64_t fileSizeBytes = 0;
};

using ReadProgressCallback = std::function<void(float progress)>;

class AudioFileReader {
public:
    AudioFileReader();
    ~AudioFileReader();

    std::shared_ptr<AudioTrack> readFile(const std::string& path);
    std::shared_ptr<AudioTrack> readFile(const std::string& path, ReadProgressCallback progress);

    std::shared_ptr<AudioTrack> readRange(const std::string& path,
                                          double startSeconds, double durationSeconds);

    AudioFileInfo getFileInfo(const std::string& path);

    static bool isSupported(const std::string& path);

private:
    std::shared_ptr<AudioTrack> readWithJuce(const std::string& path, ReadProgressCallback progress);

    static std::string getExtension(const std::string& path);

    juce::AudioFormatManager formatManager_;
};

} // namespace BeatMate::Core
