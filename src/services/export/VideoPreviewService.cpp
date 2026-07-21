#include "VideoPreviewService.h"
#include "VideoExportService.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <spdlog/spdlog.h>

#include <cmath>

namespace BeatMate::Services::Export {

VideoPreviewService::VideoPreviewService() {
    ffmpegPath_ = VideoExportService::findFfmpeg();
}

VideoPreviewService::~VideoPreviewService() = default;

bool VideoPreviewService::isLoaded() const {
    return !videoPath_.empty() && durationSec_ > 0.0;
}

double VideoPreviewService::getDurationSec() const {
    return durationSec_;
}

bool VideoPreviewService::setVideoFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    cache_.clear();
    videoPath_ = path;
    durationSec_ = 0.0;

    juce::File f{juce::String(path)};
    if (!f.existsAsFile()) {
        spdlog::warn("VideoPreviewService: fichier absent {}", path);
        return false;
    }
    if (ffmpegPath_.empty()) ffmpegPath_ = VideoExportService::findFfmpeg();
    if (ffmpegPath_.empty()) {
        spdlog::warn("VideoPreviewService: ffmpeg introuvable");
        return false;
    }

    juce::StringArray args;
    args.add(juce::String(ffmpegPath_));
    args.add("-i");
    args.add(juce::String(path));
    args.add("-hide_banner");

    juce::ChildProcess cp;
    if (!cp.start(args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut)) {
        spdlog::error("VideoPreviewService: ffmpeg launch failed");
        return false;
    }
    const juce::String out = cp.readAllProcessOutput();
    cp.waitForProcessToFinish(3000);

    const int dur = out.indexOf("Duration:");
    if (dur < 0) return false;
    const juce::String tail = out.substring(dur + 9).trim();
    const auto parts = juce::StringArray::fromTokens(tail.upToFirstOccurrenceOf(",", false, false).trim(),
                                                     ":", "");
    if (parts.size() < 3) return false;
    const double h = parts[0].getDoubleValue();
    const double m = parts[1].getDoubleValue();
    const double s = parts[2].getDoubleValue();
    durationSec_ = h * 3600.0 + m * 60.0 + s;
    spdlog::info("VideoPreviewService: charge {} duree={:.2f}s", path, durationSec_);
    return durationSec_ > 0.0;
}

juce::Image VideoPreviewService::extractFrame(double seconds, int width, int height) {
    if (ffmpegPath_.empty() || videoPath_.empty()) return {};

    auto tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("BeatMateVideoPreview");
    tmpDir.createDirectory();
    auto tmpFile = tmpDir.getChildFile("frame_"
        + juce::String((int64_t)(seconds * 1000.0))
        + "_" + juce::String(width) + "x" + juce::String(height)
        + ".png");

    if (!tmpFile.existsAsFile()) {
        juce::StringArray args;
        args.add(juce::String(ffmpegPath_));
        args.add("-y");
        args.add("-ss"); args.add(juce::String(seconds, 3));
        args.add("-i");  args.add(juce::String(videoPath_));
        args.add("-frames:v"); args.add("1");
        args.add("-s"); args.add(juce::String(width) + "x" + juce::String(height));
        args.add("-q:v"); args.add("5");
        args.add(tmpFile.getFullPathName());

        juce::ChildProcess cp;
        if (!cp.start(args, 0)) return {};
        cp.waitForProcessToFinish(4000);
    }

    if (!tmpFile.existsAsFile()) return {};
    return juce::ImageFileFormat::loadFrom(tmpFile);
}

juce::Image VideoPreviewService::getFrameAt(double seconds, int width, int height) {
    if (!isLoaded()) return {};
    const int64_t key = (int64_t) std::llround(seconds * 4.0);

    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
    }

    juce::Image img = extractFrame(seconds, width, height);
    if (img.isValid()) {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        if ((int) cache_.size() >= kCacheSize) {
            cache_.erase(cache_.begin());
        }
        cache_[key] = img;
    }
    return img;
}

} // namespace BeatMate::Services::Export
