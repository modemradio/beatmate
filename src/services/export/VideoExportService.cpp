#include "VideoExportService.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

namespace BeatMate::Services::Export {

namespace {
struct TempDirGuard {
    fs::path dir;
    bool armed{true};
    explicit TempDirGuard(fs::path d) : dir(std::move(d)) {}
    ~TempDirGuard() {
        if (!armed) return;
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void disarm() { armed = false; }
};
} // namespace

std::string VideoExportService::findFfmpeg() {
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                      .getParentDirectory();
    juce::Array<juce::File> candidates;
#ifdef _WIN32
    candidates.add(exeDir.getChildFile("ffmpeg.exe"));
    candidates.add(exeDir.getChildFile("tools").getChildFile("ffmpeg.exe"));
    candidates.add(exeDir.getChildFile("tools").getChildFile("external").getChildFile("ffmpeg.exe"));
#else
    candidates.add(exeDir.getChildFile("ffmpeg"));
    candidates.add(exeDir.getChildFile("tools").getChildFile("ffmpeg"));
    candidates.add(juce::File("/opt/homebrew/bin/ffmpeg"));
    candidates.add(juce::File("/usr/local/bin/ffmpeg"));
    candidates.add(juce::File("/usr/bin/ffmpeg"));
#endif
    for (const auto& c : candidates)
        if (c.existsAsFile()) return c.getFullPathName().toStdString();

#ifdef _WIN32
    juce::ChildProcess cp;
    if (cp.start("where ffmpeg")) {
        auto out = cp.readAllProcessOutput().trim();
        if (out.isNotEmpty()) {
            auto path = out.upToFirstOccurrenceOf("\n", false, false)
                           .removeCharacters("\r")
                           .trim();
            return path.toStdString();
        }
    }
#else
    juce::ChildProcess cp;
    if (cp.start("which ffmpeg")) {
        auto out = cp.readAllProcessOutput().trim();
        if (out.isNotEmpty())
            return out.upToFirstOccurrenceOf("\n", false, false).trim().toStdString();
    }
#endif
    return {};
}

static juce::String fmtIndex(int i) {
    std::ostringstream oss;
    oss << std::setw(6) << std::setfill('0') << i;
    return juce::String(oss.str());
}

static std::vector<float> computeWaveform(const std::string& wavPath,
                                          double fps,
                                          double& outDurationSec)
{
    std::vector<float> peaks;
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();
    juce::File f(wavPath);
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
    if (!reader) { outDurationSec = 0; return peaks; }
    const double sr = reader->sampleRate;
    const int64_t totalLen = reader->lengthInSamples;
    outDurationSec = (double)totalLen / sr;
    const int numFrames = (int)std::ceil(outDurationSec * fps);
    peaks.reserve((size_t)numFrames);

    const int samplesPerFrame = (int)(sr / fps);
    juce::AudioBuffer<float> buf((int)reader->numChannels, samplesPerFrame);
    int64_t cursor = 0;
    for (int i = 0; i < numFrames; ++i) {
        const int toRead = (int)std::min<int64_t>(samplesPerFrame, totalLen - cursor);
        if (toRead <= 0) { peaks.push_back(0); continue; }
        reader->read(&buf, 0, toRead, cursor, true, reader->numChannels > 1);
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            auto* d = buf.getReadPointer(ch);
            for (int s = 0; s < toRead; ++s) peak = juce::jmax(peak, std::abs(d[s]));
        }
        peaks.push_back(peak);
        cursor += toRead;
    }
    return peaks;
}

static void drawFrame(juce::Image& img, int frameIdx, double fps,
                      const std::vector<float>& peaks,
                      const std::vector<Models::Track>& tracks,
                      const std::vector<double>& trackStarts,
                      double duration)
{
    juce::Graphics g(img);
    const int W = img.getWidth();
    const int H = img.getHeight();

    juce::ColourGradient bg(juce::Colour(0xFF1E1B4B), 0.0f, 0.0f,
                            juce::Colour(0xFF000010), (float)W, (float)H, false);
    g.setGradientFill(bg);
    g.fillRect(0, 0, W, H);

    const int wfTop = H / 2 - H / 8;
    const int wfH   = H / 4;
    const float barW = (float)W / (float)juce::jmax(1, (int)peaks.size());
    g.setColour(juce::Colour(0xFF00D9FF).withAlpha(0.55f));
    for (size_t i = 0; i < peaks.size(); ++i) {
        const float h = peaks[i] * (float)wfH;
        g.fillRect((float)(i * barW), (float)(wfTop + wfH / 2 - h / 2), barW, h);
    }

    const float xHead = (float)frameIdx * barW;
    g.setColour(juce::Colours::white);
    g.fillRect(xHead - 1.0f, (float)wfTop - 6.0f, 2.0f, (float)wfH + 12.0f);

    double now = (double)frameIdx / fps;
    int curTrack = -1;
    for (size_t i = 0; i < trackStarts.size(); ++i) {
        const double s = trackStarts[i];
        const double e = (i + 1 < trackStarts.size()) ? trackStarts[i + 1] : duration;
        if (now >= s && now < e) { curTrack = (int)i; break; }
    }
    if (curTrack >= 0 && curTrack < (int)tracks.size()) {
        const auto& t = tracks[(size_t)curTrack];
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight((float)(H / 20)).withStyle("Bold")));
        g.drawText(juce::String(t.title), 40, 40, W - 80, H / 12,
                   juce::Justification::centredLeft, true);
        g.setFont(juce::Font(juce::FontOptions().withHeight((float)(H / 28))));
        g.setColour(juce::Colour(0xFFB0B0C8));
        g.drawText(juce::String(t.artist), 40, 40 + H / 12, W - 80, H / 16,
                   juce::Justification::centredLeft, true);
    }

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::Font(juce::FontOptions().withHeight((float)(H / 40))));
    g.drawText("BeatMate V12 — PerfStudio", 0, H - H / 20, W - 20, H / 25,
               juce::Justification::centredRight);
}

bool VideoExportService::exportVideo(const std::string& sourceWav,
                                      const std::vector<Models::Track>& tracks,
                                      const std::vector<double>& trackStartTimes,
                                      const std::string& outputMp4,
                                      const Options& opts,
                                      ProgressCallback progress)
{
    cancelRequested_.store(false, std::memory_order_relaxed);

    auto report = [&](float pct, const std::string& phase) -> bool {
        spdlog::info("[VideoExport] {:.1f}% — {}", pct * 100.0f, phase);
        if (progress) {
            if (!progress(pct, phase)) {
                cancelRequested_.store(true, std::memory_order_relaxed);
                return false;
            }
        }
        return !cancelRequested_.load(std::memory_order_relaxed);
    };

    const std::string ffmpeg = findFfmpeg();
    if (ffmpeg.empty()) {
        report(0.0f, "error: ffmpeg.exe not found (see docs/INSTALL_EXTERNAL_TOOLS.md)");
        return false;
    }

    auto tmp = fs::temp_directory_path() / ("beatmate_video_" +
        std::to_string(juce::Time::getHighResolutionTicks()));
    std::error_code mkec;
    fs::create_directories(tmp, mkec);
    if (mkec) {
        report(0.0f, "error: cannot create tmp dir: " + mkec.message());
        return false;
    }
    TempDirGuard tmpGuard(tmp);

    if (!report(0.05f, "analyzing audio")) {
        report(0.0f, "cancelled");
        return false;
    }
    double duration = 0.0;
    auto peaks = computeWaveform(sourceWav, opts.fps, duration);
    const int numFrames = (int)peaks.size();
    if (numFrames <= 0) {
        report(0.0f, "error: source audio empty");
        return false;
    }

    {
        const int64_t pngBytesPerFrame =
            (int64_t)opts.width * (int64_t)opts.height * 3 / 2;
        const int64_t pngBytes = pngBytesPerFrame * (int64_t)numFrames;
        const int64_t mp4Bytes =
            (int64_t)((opts.videoBitrateKbps + opts.audioBitrateKbps) * 1000.0
                      * duration / 8.0);
        const int64_t estimated = (int64_t)((pngBytes + mp4Bytes) * 1.2);

        const int64_t freeOut = juce::File(juce::String(outputMp4))
                                    .getParentDirectory().getBytesFreeOnVolume();
        const int64_t freeTmp = juce::File(juce::String(tmp.string()))
                                    .getBytesFreeOnVolume();
        if (freeOut > 0 && freeOut < estimated) {
            report(0.0f, "error: not enough free space at output ("
                   + std::to_string(freeOut / (1024 * 1024)) + " MiB free, "
                   + std::to_string(estimated / (1024 * 1024)) + " MiB needed)");
            return false;
        }
        if (freeTmp > 0 && freeTmp < estimated) {
            report(0.0f, "error: not enough free space in tmp ("
                   + std::to_string(freeTmp / (1024 * 1024)) + " MiB free, "
                   + std::to_string(estimated / (1024 * 1024)) + " MiB needed)");
            return false;
        }
    }

    juce::Image img(juce::Image::ARGB, opts.width, opts.height, true);
    constexpr int kCancelCheckEvery = 8;  // toutes les 8 frames
    for (int i = 0; i < numFrames; ++i) {
        if ((i % kCancelCheckEvery) == 0
            && cancelRequested_.load(std::memory_order_relaxed))
        {
            report(0.0f, "cancelled");
            return false; // tmpGuard nettoie
        }

        img.clear(img.getBounds(), juce::Colours::black);
        drawFrame(img, i, opts.fps, peaks, tracks, trackStartTimes, duration);
        auto framePath = tmp / ("frame_" + fmtIndex(i).toStdString() + ".png");
        juce::File outFile(framePath.string());
        juce::FileOutputStream fos(outFile);
        if (!fos.openedOk()) {
            report(0.0f, "error: cannot open PNG for write: " + framePath.string());
            return false;
        }
        juce::PNGImageFormat png;
        if (!png.writeImageToStream(img, fos)) {
            report(0.0f, "error: PNG write failed at frame " + std::to_string(i));
            return false;
        }
        if ((i & 15) == 0) {
            if (!report(0.05f + 0.75f * ((float)i / (float)numFrames),
                        "rendering frame " + std::to_string(i) + "/"
                        + std::to_string(numFrames))) {
                report(0.0f, "cancelled");
                return false;
            }
        }
    }

    if (!report(0.85f, "muxing with ffmpeg")) {
        report(0.0f, "cancelled");
        return false;
    }
    juce::StringArray args;
    args.add(juce::String(ffmpeg));
    args.add("-y");                                  // overwrite
    args.add("-framerate"); args.add(juce::String(opts.fps));
    args.add("-i"); args.add(juce::String((tmp / "frame_%06d.png").string()));
    args.add("-i"); args.add(juce::String(sourceWav));
    args.add("-c:v"); args.add("libx264");
    args.add("-pix_fmt"); args.add("yuv420p");
    args.add("-crf"); args.add("18");
    args.add("-c:a"); args.add("aac");
    args.add("-b:a"); args.add(juce::String(opts.audioBitrateKbps) + "k");
    args.add("-shortest");
    args.add(juce::String(outputMp4));

    juce::ChildProcess cp;
    if (!cp.start(args)) {
        report(0.0f, "error: failed to launch ffmpeg");
        return false;
    }

    constexpr int kFfmpegTimeoutMs = 600 * 1000;
    constexpr int kPollIntervalMs = 200;
    int waitedMs = 0;
    bool finished = false;
    while (waitedMs < kFfmpegTimeoutMs) {
        if (cp.waitForProcessToFinish(kPollIntervalMs)) {
            finished = true;
            break;
        }
        waitedMs += kPollIntervalMs;
        if (cancelRequested_.load(std::memory_order_relaxed)) {
            spdlog::warn("[VideoExport] cancel requested — killing ffmpeg");
            cp.kill();
            cp.waitForProcessToFinish(2000);
            report(0.0f, "cancelled");
            return false;
        }
    }
    if (!finished) {
        spdlog::error("[VideoExport] ffmpeg timed out after {} ms — killing", kFfmpegTimeoutMs);
        cp.kill();
        cp.waitForProcessToFinish(2000);
        report(0.0f, "error: ffmpeg timeout (process killed)");
        return false;
    }

    const int rc = cp.getExitCode();
    if (rc != 0) {
        report(0.0f, "error: ffmpeg returned " + std::to_string(rc));
        return false;
    }

    report(1.0f, "done");
    return true;
}

} // namespace BeatMate::Services::Export
