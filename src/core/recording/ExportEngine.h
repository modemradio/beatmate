#pragma once
#include "../audio/AudioFileWriter.h"
#include <functional>
#include <string>
namespace BeatMate::Core {
class AudioTrack;
struct ExportOptions {
    std::string format = "wav";   // wav, mp3, flac, ogg, aac
    int bitRate = 320;
    int sampleRate = 44100;
    int bitDepth = 16;
    bool writeMetadata = true;
    std::string title, artist, album;
};
using ExportProgressCallback = std::function<void(float progress)>;
class ExportEngine {
public:
    ExportEngine() = default;
    bool exportTrack(const AudioTrack& track, const std::string& outputPath,
                     const ExportOptions& options = {}, ExportProgressCallback progress = nullptr);
};
}
