#pragma once

#include <juce_graphics/juce_graphics.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace BeatMate::Services::Export {

class VideoPreviewService {
public:
    VideoPreviewService();
    ~VideoPreviewService();

    bool setVideoFile(const std::string& path);
    bool isLoaded() const;

    juce::Image getFrameAt(double seconds, int width = 320, int height = 180);

    double getDurationSec() const;

private:
    std::string ffmpegPath_;
    std::string videoPath_;
    double      durationSec_ = 0.0;

    mutable std::mutex cacheMutex_;
    std::unordered_map<int64_t, juce::Image> cache_;
    static constexpr int kCacheSize = 30;

    juce::Image extractFrame(double seconds, int width, int height);
};

} // namespace BeatMate::Services::Export
