#include "MixLabView.h"
#include "../styles/ColorPalette.h"
#include "../utils/ViewPrefs.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../widgets/utility/PeakMeterDual.h"
#include "../widgets/ToastNotifier.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../core/stems/StemSeparator.h"
#include "../../core/stems/StemCache.h"
#include "../../core/audio/AudioTrack.h"
#include "../../core/audio/AudioEngine.h"
#include "../../core/audio/AudioFileReader.h"
#include "../../core/analysis/BeatGridGenerator.h"
#include "../../core/recording/LiveSetRecorder.h"
#include "../../core/timestretch/RealtimePitchProcessor.h"
#include "../../models/TrackAnalysis.h"
#include "../../services/ai/SmartTransitionGen.h"
#include "../../services/library/PeakFileService.h"
#include "../../services/library/TrackDatabase.h"
#include "studio/StudioTimeline.h"
#include "studio/AIMixWizardDialog.h"
#include "../../services/export/AbletonExportService.h"
#include "../../app/Application.h"
#include "../../app/ServiceLocator.h"
#include "../../services/config/I18n.h"
#include "../../services/config/SettingsManager.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <random>
#include <nlohmann/json.hpp>

namespace BeatMate { class ServiceLocator; }
extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::UI {

std::vector<float> MixLabView::s_clipboard;
double MixLabView::s_clipboardDuration = 0.0;
std::vector<MixLabView::BlockClipboardEntry> MixLabView::s_blockClipboard;

namespace {
// Bounded LRU peak cache keyed by track file path. Peak generation is expensive
static constexpr size_t kPeakCacheMaxEntries = 200;

struct PeakCacheEntry {
    bool resolved = false;
    Services::Library::PeakData data;
    // iterator into the LRU list for O(1) relocation
    std::list<std::string>::iterator lruIter{};
};
static std::unordered_map<std::string, PeakCacheEntry> g_peakCache;
static std::list<std::string> g_peakLruOrder;  // front = most recent
static std::mutex g_peakCacheMutex;

static std::mutex g_peakSvcMutex;
static std::unique_ptr<Services::Library::PeakFileService> g_peakSvc;

static Services::Library::PeakFileService& getPeakFileService()
{
    std::lock_guard<std::mutex> lk(g_peakSvcMutex);
    if (!g_peakSvc) {
        g_peakSvc = std::make_unique<Services::Library::PeakFileService>();
        Services::Library::PeakConfig cfg;
        cfg.cacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("BeatMate").getChildFile("Peaks")
                                .getFullPathName().toStdString();
        cfg.useCache = true;
        cfg.generateColorData = true;          // force 3-band RGB pipeline
        cfg.segmentsPerTrack = 2000;
        g_peakSvc->initialize(cfg);
        spdlog::info("[MixLab] PeakFileService init — RGB band-split ENABLED, 2000 segs");
    }
    return *g_peakSvc;
}

// Repaint bridge: AudioThumbnail is a juce::ChangeBroadcaster, so we
class ThumbRepaintBridge : public juce::ChangeListener {
public:
    explicit ThumbRepaintBridge(juce::Component::SafePointer<juce::Component> target)
        : target_(target) {}
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        juce::MessageManager::callAsync([t = target_]() mutable {
            if (t) t->repaint();
        });
    }
private:
    juce::Component::SafePointer<juce::Component> target_;
};

struct JuceThumbEntry {
    std::unique_ptr<juce::AudioThumbnail>   thumb;
    std::unique_ptr<ThumbRepaintBridge>     listener;
};
static std::unordered_map<std::string, std::shared_ptr<JuceThumbEntry>> g_juceThumbs;
static std::mutex g_juceThumbsMutex;

static juce::AudioFormatManager& getThumbFormatManager()
{
    // AudioFormatManager is non-copyable; lazy-initialise in place.
    static juce::AudioFormatManager mgr;
    static bool s_initialized = false;
    if (!s_initialized) {
        mgr.registerBasicFormats();   // MP3/WAV/AIFF/FLAC/OGG on any platform
        s_initialized = true;
    }
    return mgr;
}

static juce::AudioThumbnailCache& getThumbCache()
{
    static juce::AudioThumbnailCache cache{ 256 };   // up to 256 tracks in RAM
    return cache;
}

static juce::AudioThumbnail*
getOrCreateJuceThumb(const std::string& path, juce::Component* target)
{
    if (path.empty()) return nullptr;
    std::lock_guard<std::mutex> lk(g_juceThumbsMutex);
    auto it = g_juceThumbs.find(path);
    if (it == g_juceThumbs.end()) {
        auto entry = std::make_shared<JuceThumbEntry>();
        entry->thumb = std::make_unique<juce::AudioThumbnail>(
            512,                              // source samples per thumb sample
            getThumbFormatManager(),
            getThumbCache());
        entry->listener = std::make_unique<ThumbRepaintBridge>(
            juce::Component::SafePointer<juce::Component>(target));
        entry->thumb->addChangeListener(entry->listener.get());
        juce::File f{juce::String::fromUTF8(path.c_str())};
        if (f.existsAsFile()) {
            entry->thumb->setSource(new juce::FileInputSource(f));
        }
        it = g_juceThumbs.emplace(path, std::move(entry)).first;
    } else if (target) {
        auto bridge = std::make_unique<ThumbRepaintBridge>(
            juce::Component::SafePointer<juce::Component>(target));
        it->second->thumb->removeChangeListener(it->second->listener.get());
        it->second->thumb->addChangeListener(bridge.get());
        it->second->listener = std::move(bridge);
    }
    return it->second->thumb.get();
}

// Move `path` to the front of the LRU list. Caller must hold g_peakCacheMutex.
static void touchLruLocked(const std::string& path)
{
    auto it = g_peakCache.find(path);
    if (it == g_peakCache.end()) return;
    g_peakLruOrder.erase(it->second.lruIter);
    g_peakLruOrder.push_front(path);
    it->second.lruIter = g_peakLruOrder.begin();
}

// Evict entries beyond the max. Caller must hold g_peakCacheMutex.
static void enforceLruBoundLocked()
{
    while (g_peakCache.size() > kPeakCacheMaxEntries) {
        const std::string& oldest = g_peakLruOrder.back();
        g_peakCache.erase(oldest);
        g_peakLruOrder.pop_back();
    }
}

static const Services::Library::PeakData* getCachedPeaks(const std::string& path)
{
    if (path.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lk(g_peakCacheMutex);
        auto it = g_peakCache.find(path);
        if (it != g_peakCache.end()) {
            touchLruLocked(path);
            return it->second.resolved ? &it->second.data : nullptr;
        }
        // Insert pending sentinel so concurrent paint calls don't double-load.
        g_peakLruOrder.push_front(path);
        PeakCacheEntry entry;
        entry.lruIter = g_peakLruOrder.begin();
        g_peakCache.emplace(path, std::move(entry));
    }

    // Load off the locked region. getPeakFileService() is lazy-init once.
    auto peaks = getPeakFileService().getPeaksByPath(path);

    std::lock_guard<std::mutex> lk(g_peakCacheMutex);
    auto it = g_peakCache.find(path);
    if (it == g_peakCache.end()) return nullptr; // evicted between unlock/relock
    if (peaks && peaks->isValid()) {
        it->second.data = *peaks;
        it->second.resolved = true;
        enforceLruBoundLocked();
        return &it->second.data;
    }
    enforceLruBoundLocked();
    return nullptr;
}

static juce::ThreadPool& getPeakGenerationPool()
{
    static juce::ThreadPool pool{ 2 };   // 2 decoders in parallel is plenty
    return pool;
}

static bool tryReadHotCueRgbPeaks(const std::string& path,
                                  Services::Library::PeakData& out)
{
    auto cacheDir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("waveform_cache");
    const auto key = juce::String::toHexString(juce::String(path).hashCode64());
    auto f = cacheDir.getChildFile(key + ".rgbpeaks2");
    if (!f.existsAsFile() || f.getSize() < (int64_t)(sizeof(double) + sizeof(int32_t)))
        return false;
    juce::FileInputStream fis(f);
    if (!fis.openedOk()) return false;
    double dur = 0.0;
    int32_t n  = 0;
    if (fis.read(&dur, sizeof(double)) != sizeof(double)) return false;
    if (fis.read(&n,   sizeof(int32_t)) != sizeof(int32_t)) return false;
    if (n <= 0 || n > 500000) return false;
    out.segmentCount = n;
    out.duration     = dur;
    out.peaksPositive.resize(n);
    out.lowFreq.resize(n);
    out.midFreq.resize(n);
    out.highFreq.resize(n);
    const int bytes = n * (int) sizeof(float);
    if (fis.read(out.peaksPositive.data(), bytes) != bytes) return false;
    if (fis.read(out.lowFreq.data(),       bytes) != bytes) return false;
    if (fis.read(out.midFreq.data(),       bytes) != bytes) return false;
    if (fis.read(out.highFreq.data(),      bytes) != bytes) return false;
    return out.isValid();
}

static void schedulePeakGeneration(const std::string& path,
                                   juce::Component::SafePointer<juce::Component> target)
{
    getPeakGenerationPool().addJob([path, target]() mutable {
        auto peaksUP = getPeakFileService().generatePeaks(path, 0);
        bool ok = (peaksUP && peaksUP->isValid());
        {
            std::lock_guard<std::mutex> lk(g_peakCacheMutex);
            auto it = g_peakCache.find(path);
            if (it != g_peakCache.end() && ok) {
                it->second.data     = *peaksUP;
                it->second.resolved = true;
                enforceLruBoundLocked();
            }
        }
        juce::MessageManager::callAsync([target]() mutable {
            if (target) target->repaint();
        });
    });
}

// Studio-side peak lookup: (1) in-memory LRU, (2) HotCue .rgbpeaks sidecar,
static const Services::Library::PeakData* getCachedPeaksShared(
    const std::string& path, juce::Component* repaintTarget)
{
    if (path.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lk(g_peakCacheMutex);
        auto it = g_peakCache.find(path);
        if (it != g_peakCache.end()) {
            touchLruLocked(path);
            if (it->second.resolved) return &it->second.data;
            return nullptr;  // generation already pending
        }
        // Reserve a pending slot so concurrent paints don't stack decodes.
        g_peakLruOrder.push_front(path);
        PeakCacheEntry entry;
        entry.lruIter = g_peakLruOrder.begin();
        g_peakCache.emplace(path, std::move(entry));
    }

    {
        auto bmpk = getPeakFileService().getPeaksByPath(path);
        const bool bmpkHasRgb = bmpk && bmpk->isValid()
                                && !bmpk->lowFreq.empty()
                                && !bmpk->midFreq.empty()
                                && !bmpk->highFreq.empty();
        if (bmpkHasRgb) {
            std::lock_guard<std::mutex> lk(g_peakCacheMutex);
            auto it = g_peakCache.find(path);
            if (it != g_peakCache.end()) {
                it->second.data     = *bmpk;
                it->second.resolved = true;
                enforceLruBoundLocked();
                return &it->second.data;
            }
        }
    }

    schedulePeakGeneration(path,
        juce::Component::SafePointer<juce::Component>(repaintTarget));
    return nullptr;
}
} // namespace

MixLabView::TimelineTrackBlock::TimelineTrackBlock() = default;

MixLabView::TimelineTrackBlock::TimelineTrackBlock(const Models::Track& track)
    : m_track(track)
{
    m_endTime = track.duration;
}

void MixLabView::TimelineTrackBlock::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRoundedRectangle(b, 4.0f);

    float handleH = 6.0f;
    juce::Colour handleCol = m_handleHovered ? juce::Colour(0xFFFCD34D) : juce::Colour(0xFFEAB308);
    g.setColour(handleCol);
    g.fillRoundedRectangle(b.getX(), b.getY(), b.getWidth(), handleH, 2.0f);

    bool selected = (getProperties()["selected"].toString() == "1");
    g.setColour(selected ? Colors::primary() : juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, selected ? 2.0f : 1.0f);

    float headerY = handleH + 2;
    float headerH = 28;

    g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    g.setColour(juce::Colours::white);
    g.drawText(juce::String(m_track.title), 6, (int)headerY, (int)b.getWidth() - 100, 14,
               juce::Justification::centredLeft, true);

    g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xFF94A3B8));
    g.drawText(juce::String(m_track.artist), 6, (int)headerY + 14, (int)b.getWidth() / 2, 12,
               juce::Justification::centredLeft, true);

    if (b.getWidth() > 180)
    {
        float rx = b.getRight() - 100;
        juce::Rectangle<float> bpmR(rx, headerY + 2, 44, 14);
        g.setColour(Colors::primary().withAlpha(0.25f));
        g.fillRoundedRectangle(bpmR, 3.0f);
        g.setColour(Colors::primary());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(juce::String(m_track.bpm, 0), bpmR, juce::Justification::centred);

        juce::Rectangle<float> keyR(rx + 48, headerY + 2, 44, 14);
        g.setColour(juce::Colour(0xFF8B5CF6).withAlpha(0.25f));
        g.fillRoundedRectangle(keyR, 3.0f);
        g.setColour(juce::Colour(0xFF8B5CF6));
        auto keyStr = m_track.camelotKey.empty() ? juce::String(m_track.key) : juce::String(m_track.camelotKey);
        g.drawText(keyStr, keyR, juce::Justification::centred);
    }

    float waveY = headerY + headerH;
    float waveH = b.getHeight() - waveY - 20;
    float waveX = 4.0f;
    float waveW = b.getWidth() - 8;

    if (showStems && waveH > 60)
    {
        struct StemRow {
            const char* label;
            juce::Colour color;
        };
        // Couleurs unifiées avec le mode Performance (ColorPalette.h) pour
        StemRow stems[] = {
            { "VOCALS", Colors::stemVocals() },
            { "DRUMS",  Colors::stemDrums()  },
            { "BASS",   Colors::stemBass()   },
            { "OTHER",  Colors::stemOther()  }
        };

        float subH = waveH / 4.0f;
        std::mt19937 rngStem((unsigned int)(m_track.id ^ (int64_t)(m_track.bpm * 100)));
        std::uniform_real_distribution<float> distStem(0.0f, 1.0f);

        for (int s = 0; s < 4; ++s)
        {
            float subY = waveY + s * subH;

            g.setColour(juce::Colour(0xFF0A0A12));
            g.fillRect(waveX, subY, waveW, subH - 1);

            juce::Rectangle<float> labelR(waveX, subY, 44.0f, subH);
            g.setColour(stems[s].color.withAlpha(0.2f));
            g.fillRect(labelR);
            g.setColour(stems[s].color);
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText(stems[s].label, labelR, juce::Justification::centred);

            float mwX = waveX + 46;
            float mwW = waveW - 50;
            float mwMid = subY + subH * 0.5f;
            int numBars = (int)(mwW / 2.0f);
            if (numBars < 20) numBars = 20;

            for (int i = 0; i < numBars; ++i)
            {
                float t = (float)i / numBars;
                float barX = mwX + t * mwW;
                float barW = mwW / numBars - 0.3f;
                if (barW < 0.5f) barW = 0.5f;

                float energy = 0.3f + 0.4f * std::sin(t * 3.14159f) + 0.3f * distStem(rngStem);
                float barH = energy * (subH - 6) * 0.5f;

                g.setColour(stems[s].color.withAlpha(0.7f));
                g.fillRect(barX, mwMid - barH, barW, barH * 2);
            }

            g.setColour(juce::Colour(0xFF2A2A38));
            g.drawHorizontalLine((int)(subY + subH - 1), waveX, waveX + waveW);
        }

        goto skipNormalWaveform;
    }

    if (waveH > 30 && waveW > 40)
    {
        juce::ColourGradient bg(
            juce::Colour(0xFF0F0F1A), waveX, waveY,
            juce::Colour(0xFF05050B), waveX, waveY + waveH, false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(waveX, waveY, waveW, waveH, 2.0f);

        const float midLine = waveY + waveH * 0.5f;
        int numBars = (int)(waveW / 2.0f);
        if (numBars < 50) numBars = 50;

        auto* thumb = getOrCreateJuceThumb(m_track.filePath, this);
        const bool thumbReady =
            (thumb != nullptr && thumb->getNumChannels() > 0
             && thumb->getNumSamplesFinished() > 0);

        if (thumbReady) {
            juce::Rectangle<int> wr(
                (int)(waveX + 1), (int)(midLine - waveH * 0.5f + 1),
                (int)(waveW - 2), (int)(waveH - 2));
            const double total = thumb->getTotalLength() > 0.0
                                     ? thumb->getTotalLength()
                                     : juce::jmax(1.0, m_track.duration);
            g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.35f));
            thumb->drawChannels(g, wr, 0.0, total, 0.95f);
        }

        const Services::Library::PeakData* peaks = getCachedPeaksShared(m_track.filePath, this);

        if (peaks && peaks->segmentCount > 0
            && !peaks->lowFreq.empty()
            && !peaks->midFreq.empty()
            && !peaks->highFreq.empty())
        {
            const int segs = peaks->segmentCount;
            const float halfH = waveH * 0.5f - 1.0f;

            float maxBand = 1e-4f;
            for (int s = 0; s < segs; ++s) {
                maxBand = std::max(maxBand, std::abs(peaks->lowFreq [s]));
                maxBand = std::max(maxBand, std::abs(peaks->midFreq [s]));
                maxBand = std::max(maxBand, std::abs(peaks->highFreq[s]));
            }
            const float inv = 1.0f / maxBand;

            const int    bars = juce::jmax((int) waveW - 2, 50);
            const float  pxPerBar = (waveW - 2.0f) / (float) bars;

            const juce::Colour LOW = juce::Colour::fromRGB(240, 60, 60);
            const juce::Colour MID = juce::Colour::fromRGB(60, 220, 90);
            const juce::Colour HI  = juce::Colour::fromRGB(70, 130, 255);

            for (int i = 0; i < bars; ++i) {
                const int i0 = (int) ((float) i       / bars * segs);
                const int i1 = juce::jmax(i0 + 1,
                                          (int) ((float)(i + 1) / bars * segs));
                float L = 0.0f, M = 0.0f, H = 0.0f;
                for (int s = i0; s < std::min(i1, segs); ++s) {
                    L = std::max(L, std::abs(peaks->lowFreq [s]));
                    M = std::max(M, std::abs(peaks->midFreq [s]));
                    H = std::max(H, std::abs(peaks->highFreq[s]));
                }
                L *= inv; M *= inv; H *= inv;

                const float amp = std::max({ L, M, H });
                if (amp < 0.02f) continue;

                float rF = L * LOW.getFloatRed()   + M * MID.getFloatRed()   + H * HI.getFloatRed();
                float gF = L * LOW.getFloatGreen() + M * MID.getFloatGreen() + H * HI.getFloatGreen();
                float bF = L * LOW.getFloatBlue()  + M * MID.getFloatBlue()  + H * HI.getFloatBlue();
                const float cmax = std::max({ rF, gF, bF, 1e-4f });
                rF /= cmax; gF /= cmax; bF /= cmax;

                const float barH = amp * halfH;
                const float barX = waveX + 1.0f + (float) i * pxPerBar;
                const float barW = std::max(1.0f, pxPerBar);

                g.setColour(juce::Colour::fromFloatRGBA(rF, gF, bF, 1.0f));
                g.fillRect(barX, midLine - barH, barW, barH * 2.0f);
            }
        } else if (!thumbReady) {
            g.setColour(juce::Colour(0xFF94A3B8).withAlpha(0.6f));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("Decodage waveform...", (int) waveX, (int) (midLine - 8),
                       (int) waveW, 16, juce::Justification::centred);
        }

        g.setColour(juce::Colour(0x40FFFFFF));
        g.drawHorizontalLine((int)midLine, waveX, waveX + waveW);
    }

    skipNormalWaveform:

    if (loopEnabled && loopOutTime > loopInTime && m_track.duration > 0.0)
    {
        const float dur = (float) m_track.duration;
        float lx1 = waveX + (float)(juce::jlimit(0.0, (double)dur, loopInTime)  / dur) * waveW;
        float lx2 = waveX + (float)(juce::jlimit(0.0, (double)dur, loopOutTime) / dur) * waveW;
        g.setColour(juce::Colour(0xFFFDE047).withAlpha(0.18f));
        g.fillRect(lx1, waveY, juce::jmax(2.f, lx2 - lx1), waveH);
        g.setColour(juce::Colour(0xFFFDE047));
        g.fillRect(lx1 - 1.f, waveY, 2.f, waveH);
        g.fillRect(lx2 - 1.f, waveY, 2.f, waveH);
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFFDE047));
        g.drawText("LOOP", (int)(lx1 + 3), (int)waveY + 2, 40, 10,
                   juce::Justification::centredLeft, false);
    }

    if (!cuePoints.empty() && m_track.duration > 0.0)
    {
        const float dur = (float) m_track.duration;
        for (const auto& cue : cuePoints)
        {
            if (cue.position < 0.0 || cue.position > dur) continue;
            const float cx = waveX + (float)((cue.position / dur) * waveW);

            juce::Colour col;
            if (!cue.color.empty() && cue.color[0] == '#' && cue.color.size() >= 7) {
                col = juce::Colour::fromString(juce::String("ff") + cue.color.substr(1));
            } else {
                switch (cue.type) {
                    case Models::CuePointType::HotCue:     col = juce::Colour(0xFFEF4444); break;
                    case Models::CuePointType::MemoryCue:  col = juce::Colour(0xFF3B82F6); break;
                    case Models::CuePointType::Loop:       col = juce::Colour(0xFFFDE047); break;
                    case Models::CuePointType::Grid:       col = juce::Colour(0xFF94A3B8); break;
                    case Models::CuePointType::IntroStart: col = juce::Colour(0xFF22C55E); break;
                    case Models::CuePointType::IntroEnd:   col = juce::Colour(0xFF10B981); break;
                    case Models::CuePointType::OutroStart: col = juce::Colour(0xFFF59E0B); break;
                    case Models::CuePointType::OutroEnd:   col = juce::Colour(0xFFEA580C); break;
                    default:                               col = juce::Colour(0xFF8B5CF6); break;
                }
            }

            if (cue.isLoop() && cue.length > 0.0) {
                float lx2 = waveX + (float)(((cue.position + cue.length) / dur) * waveW);
                g.setColour(col.withAlpha(0.15f));
                g.fillRect(cx, waveY, juce::jmax(2.f, lx2 - cx), waveH);
                g.setColour(col);
                g.fillRect(lx2 - 1.f, waveY, 2.f, waveH);
            }

            g.setColour(col.withAlpha(0.85f));
            g.fillRect(cx - 0.5f, waveY, 1.5f, waveH);

            const float flagH = 12.0f;
            const float flagW = 14.0f;
            juce::Path flag;
            flag.startNewSubPath(cx, waveY);
            flag.lineTo(cx + flagW, waveY);
            flag.lineTo(cx + flagW, waveY + flagH - 3.f);
            flag.lineTo(cx + 3.f,   waveY + flagH);
            flag.lineTo(cx,         waveY + flagH - 3.f);
            flag.closeSubPath();
            g.setColour(col);
            g.fillPath(flag);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            juce::String label;
            if (cue.number > 0)                 label = juce::String(cue.number);
            else if (cue.type == Models::CuePointType::IntroStart) label = "I";
            else if (cue.type == Models::CuePointType::IntroEnd)   label = "I";
            else if (cue.type == Models::CuePointType::OutroStart) label = "O";
            else if (cue.type == Models::CuePointType::OutroEnd)   label = "O";
            else if (cue.type == Models::CuePointType::MemoryCue)  label = "M";
            else if (cue.type == Models::CuePointType::Loop)       label = "L";
            else                                                   label = "C";
            g.drawText(label, (int)cx, (int)waveY + 1, (int)flagW, (int)flagH - 2,
                       juce::Justification::centred, false);
        }
    }

    if (hasSelection())
    {
        float selX1 = waveX + (m_selStart / m_track.duration) * waveW;
        float selX2 = waveX + (m_selEnd / m_track.duration) * waveW;
        g.setColour(Colors::primary().withAlpha(0.3f));
        g.fillRect(selX1, waveY, selX2 - selX1, waveH);
        g.setColour(Colors::primary());
        g.drawVerticalLine((int)selX1, waveY, waveY + waveH);
        g.drawVerticalLine((int)selX2, waveY, waveY + waveH);
    }

    auto drawAutoLane = [&](const std::vector<AutoPt>& pts, juce::Colour col) {
        if (pts.size() < 1 || m_track.duration <= 0) return;

        juce::Path path;
        bool first = true;
        for (auto& p : pts)
        {
            float px = waveX + (float)((p.time / m_track.duration) * waveW);
            float py = waveY + waveH * (1.0f - p.value); // 0 = bas, 1 = haut
            if (first) { path.startNewSubPath(px, py); first = false; }
            else path.lineTo(px, py);

            g.setColour(col);
            g.fillEllipse(px - 3, py - 3, 6, 6);
            g.setColour(juce::Colours::white);
            g.drawEllipse(px - 3, py - 3, 6, 6, 1.0f);
        }
        g.setColour(col.withAlpha(0.7f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    };

    if (showAutoVolume) drawAutoLane(autoVolume, juce::Colours::white);
    if (showAutoEq)
    {
        drawAutoLane(autoEqHi, juce::Colour(0xFFEF4444));
        drawAutoLane(autoEqMid, juce::Colour(0xFF22C55E));
        drawAutoLane(autoEqLo, juce::Colour(0xFF3B82F6));
    }
    if (showAutoFilter) drawAutoLane(autoFilter, juce::Colour(0xFF06B6D4));
    if (showAutoPitch) drawAutoLane(autoPitch, juce::Colour(0xFF8B5CF6));

    if ((fadeInSec > 0.0 || fadeOutSec > 0.0) && m_track.duration > 0.0 && waveH > 8)
    {
        const float dur = (float) m_track.duration;
        const int steps = 40;
        if (fadeInSec > 0.0) {
            juce::Path p;
            p.startNewSubPath(waveX, waveY);
            for (int i = 0; i <= steps; ++i) {
                float t = (float) i / (float) steps;
                double off = t * fadeInSec;
                float v = evalFadeAt(off);
                float xpx = waveX + (off / dur) * waveW;
                float ypx = waveY + (1.0f - v) * waveH;
                p.lineTo(xpx, ypx);
            }
            p.lineTo(waveX + (fadeInSec / dur) * waveW, waveY);
            p.closeSubPath();
            g.setColour(juce::Colour(0xAA000000));
            g.fillPath(p);
            g.setColour(juce::Colour(0xFF3B82F6));
            for (int i = 0; i < steps; ++i) {
                float t1 = (float)  i      / (float) steps;
                float t2 = (float) (i + 1) / (float) steps;
                float v1 = evalFadeAt(t1 * fadeInSec);
                float v2 = evalFadeAt(t2 * fadeInSec);
                float x1 = waveX + (t1 * fadeInSec / dur) * waveW;
                float x2 = waveX + (t2 * fadeInSec / dur) * waveW;
                g.drawLine(x1, waveY + (1.f - v1) * waveH, x2, waveY + (1.f - v2) * waveH, 1.5f);
            }
        }
        if (fadeOutSec > 0.0) {
            const double tail = dur - fadeOutSec;
            juce::Path p;
            float xStart = waveX + (tail / dur) * waveW;
            p.startNewSubPath(xStart, waveY);
            for (int i = 0; i <= steps; ++i) {
                float t = (float) i / (float) steps;
                double off = tail + t * fadeOutSec;
                float v = evalFadeAt(off);
                float xpx = waveX + (off / dur) * waveW;
                float ypx = waveY + (1.0f - v) * waveH;
                p.lineTo(xpx, ypx);
            }
            p.lineTo(waveX + waveW, waveY);
            p.closeSubPath();
            g.setColour(juce::Colour(0xAA000000));
            g.fillPath(p);
            g.setColour(juce::Colour(0xFF8B5CF6));
            for (int i = 0; i < steps; ++i) {
                float t1 = (float)  i      / (float) steps;
                float t2 = (float) (i + 1) / (float) steps;
                double o1 = tail + t1 * fadeOutSec;
                double o2 = tail + t2 * fadeOutSec;
                float v1 = evalFadeAt(o1);
                float v2 = evalFadeAt(o2);
                float x1 = waveX + (o1 / dur) * waveW;
                float x2 = waveX + (o2 / dur) * waveW;
                g.drawLine(x1, waveY + (1.f - v1) * waveH, x2, waveY + (1.f - v2) * waveH, 1.5f);
            }
        }
    }

    int mins = (int)m_track.duration / 60, secs = (int)m_track.duration % 60;
    g.setFont(juce::Font(8.0f));
    g.setColour(juce::Colour(0xFF64748B));
    g.drawText(juce::String::formatted("%d:%02d", mins, secs),
               (int)b.getWidth() - 38, (int)b.getBottom() - 14, 36, 12,
               juce::Justification::centredRight);

    float eNorm = juce::jlimit(0.0f, 1.0f, m_track.energy / 10.0f);
    g.setColour(juce::Colour(0xFF1E293B));
    g.fillRoundedRectangle(4.0f, b.getBottom() - 12, 60, 4, 2.0f);
    juce::Colour eCol = eNorm < 0.4f ? juce::Colour(0xFF22C55E)
                      : eNorm < 0.7f ? juce::Colour(0xFFF59E0B)
                                     : juce::Colour(0xFFEF4444);
    g.setColour(eCol);
    g.fillRoundedRectangle(4.0f, b.getBottom() - 12, 60 * eNorm, 4, 2.0f);
}

void MixLabView::TimelineTrackBlock::resized() {}

float MixLabView::TimelineTrackBlock::evalFadeAt(double offsetSec) const
{
    const double dur = (m_track.duration > 0.0) ? m_track.duration : 1.0;
    float v = 1.0f;
    if (fadeInSec > 0.0 && offsetSec < fadeInSec) {
        float t = (float)(offsetSec / fadeInSec);   // 0..1
        switch (fadeInCurve) {
            case FadeCurve::Linear: v = t; break;
            case FadeCurve::SCurve: v = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t)); break;
            case FadeCurve::Exp:    v = t * t; break;
        }
    }
    const double tail = dur - fadeOutSec;
    if (fadeOutSec > 0.0 && offsetSec > tail) {
        float t = (float)((dur - offsetSec) / fadeOutSec); // 1..0
        if (t < 0.f) t = 0.f;
        float vOut = 1.f;
        switch (fadeOutCurve) {
            case FadeCurve::Linear: vOut = t; break;
            case FadeCurve::SCurve: vOut = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t)); break;
            case FadeCurve::Exp:    vOut = t * t; break;
        }
        v = std::min(v, vOut);
    }
    return juce::jlimit(0.0f, 1.0f, v);
}

void MixLabView::TimelineTrackBlock::addAutomationPoint(int laneId, double time, float value)
{
    const double maxTime = (m_track.duration > 0.0) ? m_track.duration : (m_endTime - m_startTime);
    const double clampedTime = juce::jlimit(0.0, maxTime > 0.0 ? maxTime : time, time);
    AutoPt pt { clampedTime, juce::jlimit(0.0f, 1.0f, value) };
    std::vector<AutoPt>* lane = nullptr;
    switch (laneId)
    {
        case 0: lane = &autoVolume; break;
        case 1: lane = &autoEqHi; break;
        case 2: lane = &autoEqMid; break;
        case 3: lane = &autoEqLo; break;
        case 4: lane = &autoFilter; break;
        case 5: lane = &autoPitch; break;
        case 6: lane = &autoStemDrums; break;
        case 7: lane = &autoStemBass; break;
        case 8: lane = &autoStemMelody; break;
        case 9: lane = &autoStemVocals; break;
        default: return;
    }
    lane->push_back(pt);
    std::sort(lane->begin(), lane->end(), [](const AutoPt& a, const AutoPt& b) {
        return a.time < b.time;
    });
    repaint();
}

void MixLabView::TimelineTrackBlock::clearAllAutomation()
{
    autoVolume.clear();
    autoEqHi.clear(); autoEqMid.clear(); autoEqLo.clear();
    autoFilter.clear(); autoPitch.clear();
    autoStemDrums.clear(); autoStemBass.clear();
    autoStemMelody.clear(); autoStemVocals.clear();
    repaint();
}

MixLabView::TimelineTrackBlock::DragMode
MixLabView::TimelineTrackBlock::pickDragMode(juce::Point<int> p) const
{
    const int w = getWidth();
    constexpr int EDGE_PX        = 7;
    constexpr int FADE_CORNER_PX = 14;

    // Top corners: fade handles take priority over edge trims.
    if (p.y < FADE_CORNER_PX) {
        if (p.x < FADE_CORNER_PX)     return DragMode::FadeInHandle;
        if (p.x > w - FADE_CORNER_PX) return DragMode::FadeOutHandle;
    }
    if (p.x < EDGE_PX)                return DragMode::ResizeLeft;
    if (p.x > w - EDGE_PX)            return DragMode::ResizeRight;
    if (p.y < 8)                      return DragMode::MoveClip;
    return DragMode::None;
}

void MixLabView::TimelineTrackBlock::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    if (e.mods.isPopupMenu())
    {
        // Make sure this block is the timeline's current selection before we
        {
            juce::Component* p = getParentComponent();
            while (p) {
                if (auto* tl = dynamic_cast<TimelineComponent*>(p)) {
                    for (int i = 0; i < tl->getTrackCount(); ++i) {
                        if (&tl->getBlock(i) == this) { tl->setSelectedBlock(i); break; }
                    }
                    break;
                }
                p = p->getParentComponent();
            }
        }

        juce::PopupMenu menu;
        menu.addItem(1, BM_TJ("studio.menu.cut")     + juce::String("   Ctrl+X"));
        menu.addItem(2, BM_TJ("studio.menu.copy")    + juce::String("   Ctrl+C"));
        menu.addItem(3, BM_TJ("studio.menu.paste")   + juce::String("   Ctrl+V"));
        menu.addSeparator();
        menu.addItem(4, BM_TJ("studio.menu.delete")      + juce::String("   Del"));
        menu.addItem(5, BM_TJ("studio.menu.split")       + juce::String("   Ctrl+B"));
        menu.addItem(6, BM_TJ("studio.menu.duplicate"));
        menu.addSeparator();
        menu.addItem(10, "Quantize this clip");
        menu.addItem(11, "Align start to next beat");
        menu.addItem(12, "Align start to previous beat");
        menu.addSeparator();
        menu.addItem(13, "Trim to selection");
        menu.addItem(14, "Insert silence (1 bar)");
        menu.addItem(15, "Add fade-in 2 s");
        menu.addItem(16, "Add fade-out 2 s");
        menu.addSeparator();
        menu.addItem(17, "Set as INTRO marker");
        menu.addItem(18, "Set as OUTRO marker");
        menu.addItem(19, "Lock clip position");
        menu.addSeparator();
        menu.addItem(8, BM_TJ("studio.menu.clearSel"));
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this](int result) {
                auto findTimeline = [this]() -> TimelineComponent* {
                    juce::Component* p = getParentComponent();
                    while (p) {
                        if (auto* tl = dynamic_cast<TimelineComponent*>(p)) return tl;
                        p = p->getParentComponent();
                    }
                    return nullptr;
                };
                auto* tl = findTimeline();
                if (!tl) return;
                switch (result) {
                    case 1: tl->copySelection(); tl->deleteSelection(); break;
                    case 2: tl->copySelection(); break;
                    case 3: tl->pasteSelection(); break;
                    case 4: tl->deleteSelection(); break;
                    case 5: tl->splitAtPlayhead(); break;
                    case 6: {
                        int idx = -1;
                        for (int i = 0; i < tl->getTrackCount(); ++i) {
                            if (&tl->getBlock(i) == this) { idx = i; break; }
                        }
                        if (idx >= 0) {
                            auto& src = tl->getBlock(idx);
                            auto dup = std::make_unique<TimelineTrackBlock>(src.getTrack());
                            dup->setAudioRange(src.getAudioStartSec(),
                                                src.getAudioEndSec() > 0.0
                                                    ? src.getAudioEndSec()
                                                    : src.getTrack().duration);
                            dup->setStartTime(src.getStartTime() + src.getTrack().duration);
                            tl->insertBlock(idx + 1, std::move(dup));
                        }
                        break;
                    }
                    case 8: clearSelection(); break;
                    case 10: {
                        if (m_track.bpm <= 0) {
                            BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8(u8"BPM inconnu"),
                                juce::String::fromUTF8(u8"Analysez la piste avant cette opération."),
                                BeatMate::UI::Widgets::ToastNotifier::Kind::Warning);
                            break;
                        }
                        double bpm = m_track.bpm;
                        double beat = 60.0 / bpm;
                        double snapped = std::round(m_startTime / beat) * beat;
                        setStartTime(snapped);
                        spdlog::info("[MixLab] Quantize clip -> {:.3f}s (beat={:.3f}s)", snapped, beat);
                        if (onClipChanged) onClipChanged();
                        break;
                    }
                    case 11: {
                        if (m_track.bpm <= 0) {
                            BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8(u8"BPM inconnu"),
                                juce::String::fromUTF8(u8"Analysez la piste avant cette opération."),
                                BeatMate::UI::Widgets::ToastNotifier::Kind::Warning);
                            break;
                        }
                        double bpm = m_track.bpm;
                        double beat = 60.0 / bpm;
                        double snapped = std::ceil(m_startTime / beat) * beat;
                        setStartTime(snapped);
                        spdlog::info("[MixLab] Align to NEXT beat -> {:.3f}s", snapped);
                        if (onClipChanged) onClipChanged();
                        break;
                    }
                    case 12: {
                        if (m_track.bpm <= 0) {
                            BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8(u8"BPM inconnu"),
                                juce::String::fromUTF8(u8"Analysez la piste avant cette opération."),
                                BeatMate::UI::Widgets::ToastNotifier::Kind::Warning);
                            break;
                        }
                        double bpm = m_track.bpm;
                        double beat = 60.0 / bpm;
                        double snapped = std::floor(m_startTime / beat) * beat;
                        setStartTime(snapped);
                        spdlog::info("[MixLab] Align to PREV beat -> {:.3f}s", snapped);
                        if (onClipChanged) onClipChanged();
                        break;
                    }
                    case 13: {
                        if (hasSelection()) {
                            const double s = getSelectionStart();
                            const double e = getSelectionEnd();
                            setAudioRange(s, e);
                            spdlog::info("[MixLab] Trim clip to [{:.3f}..{:.3f}]", s, e);
                            if (onClipChanged) onClipChanged();
                        } else {
                            spdlog::info("[MixLab] Trim requires a highlighted selection");
                        }
                        break;
                    }
                    case 14: {
                        if (m_track.bpm <= 0) {
                            BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                                juce::String::fromUTF8(u8"BPM inconnu"),
                                juce::String::fromUTF8(u8"Analysez la piste avant cette opération."),
                                BeatMate::UI::Widgets::ToastNotifier::Kind::Warning);
                            break;
                        }
                        double bpm = m_track.bpm;
                        double bar = 4.0 * 60.0 / bpm;
                        setStartTime(m_startTime + bar);
                        spdlog::info("[MixLab] Insert silence (1 bar = {:.3f}s)", bar);
                        if (onClipChanged) onClipChanged();
                        break;
                    }
                    case 15: { fadeInSec  = 2.0; spdlog::info("[MixLab] fade-in = 2 s"); repaint(); if (onClipChanged) onClipChanged(); break; }
                    case 16: { fadeOutSec = 2.0; spdlog::info("[MixLab] fade-out = 2 s"); repaint(); if (onClipChanged) onClipChanged(); break; }
                    case 17: { getProperties().set("marker", "INTRO"); spdlog::info("[MixLab] Clip marked as INTRO"); repaint(); break; }
                    case 18: { getProperties().set("marker", "OUTRO"); spdlog::info("[MixLab] Clip marked as OUTRO"); repaint(); break; }
                    case 19: {
                        const bool wasLocked = (getProperties()["locked"].toString() == "1");
                        getProperties().set("locked", wasLocked ? "0" : "1");
                        spdlog::info("[MixLab] Clip {}", wasLocked ? "UNLOCKED" : "LOCKED");
                        repaint();
                        break;
                    }
                    default: break;
                }
            });
        return;
    }

    if (!cuePoints.empty() && m_track.duration > 0.0 && pos.y < 22)
    {
        const float waveX = 4.0f;
        const float waveW = (float)getWidth() - 8;
        for (const auto& cue : cuePoints) {
            if (cue.position < 0.0 || cue.position > m_track.duration) continue;
            const float cx = waveX + (float)((cue.position / m_track.duration) * waveW);
            if (std::abs((float)pos.x - cx) < 6.0f) {
                auto* anc = getParentComponent();
                while (anc && !dynamic_cast<TimelineComponent*>(anc))
                    anc = anc->getParentComponent();
                if (auto* tl = dynamic_cast<TimelineComponent*>(anc))
                    tl->setPlayheadPosition(m_startTime + cue.position);
                return;
            }
        }
    }

    const DragMode mode = pickDragMode(pos);
    if (mode != DragMode::None && !e.mods.isShiftDown())
    {
        m_dragMode             = mode;
        m_dragOrigin           = pos;
        m_dragStartTime        = m_startTime;
        m_dragStartAudioStart  = m_audioStartSec;
        m_dragStartAudioEnd    = (m_audioEndSec > 0.0) ? m_audioEndSec : m_track.duration;
        m_dragStartFadeIn      = fadeInSec;
        m_dragStartFadeOut     = fadeOutSec;
        if (mode == DragMode::MoveClip) {
            m_dragging = true;
            m_dragStartX = pos.x;
        }
        getProperties().set("selected", "1");
        juce::Component* p = getParentComponent();
        while (p) {
            if (auto* tl = dynamic_cast<TimelineComponent*>(p)) {
                for (int i = 0; i < tl->getTrackCount(); ++i)
                    if (&tl->getBlock(i) == this) { tl->setSelectedBlock(i); break; }
                break;
            }
            p = p->getParentComponent();
        }
        repaint();
        return;
    }

    if (beatGridMode == 0 && !beatGridPositions.empty() && m_track.duration > 0
        && pos.y >= 28 && pos.y <= 40)
    {
        const float waveX = 4.0f;
        const float waveW = (float)getWidth() - 8;
        int nearest = -1;
        double nearestDist = 1e9;
        for (size_t i = 0; i < beatGridPositions.size(); ++i) {
            const double t = beatGridPositions[i];
            const float bx = waveX + (float)((t / m_track.duration) * waveW);
            const double d = std::fabs((double)pos.x - (double)bx);
            if (d < nearestDist) { nearestDist = d; nearest = (int)i; }
        }
        if (nearest >= 0 && nearestDist <= 5.0) {
            manualBeatDragIndex = nearest;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            return;
        }
    }

    if (e.mods.isShiftDown() && pos.y > 36 && m_track.duration > 0)
    {
        m_selecting = true;
        float waveX = 4.0f;
        float waveW = (float)getWidth() - 8;
        double timeAtClick = ((pos.x - waveX) / waveW) * m_track.duration;

        // Snap au beat boundary le plus proche (BeatMate = unite minimale = 1 beat)
        if (m_track.bpm > 0)
        {
            double beatDuration = 60.0 / m_track.bpm;
            timeAtClick = std::round(timeAtClick / beatDuration) * beatDuration;
        }

        m_selStart = juce::jlimit(0.0, m_track.duration, timeAtClick);
        m_selEnd = m_selStart;
        repaint();
    }
    else if (!e.mods.isShiftDown())
    {
        clearSelection();
    }

    getProperties().set("selected", "1");
    juce::Component* p = getParentComponent();
    while (p) {
        if (auto* tl = dynamic_cast<TimelineComponent*>(p)) {
            for (int i = 0; i < tl->getTrackCount(); ++i) {
                if (&tl->getBlock(i) == this) { tl->setSelectedBlock(i); break; }
            }
            break;
        }
        p = p->getParentComponent();
    }
    if (auto* parent = getParentComponent())
        parent->repaint();
}

void MixLabView::TimelineTrackBlock::mouseDrag(const juce::MouseEvent& e)
{
    auto* tl = [this]() -> TimelineComponent* {
        juce::Component* c = getParentComponent();
        while (c) {
            if (auto* t = dynamic_cast<TimelineComponent*>(c)) return t;
            c = c->getParentComponent();
        }
        return nullptr;
    }();

    if (tl && m_dragMode != DragMode::None && m_dragMode != DragMode::SelectRange
        && m_dragMode != DragMode::ManualBeat)
    {
        const auto p = e.getPosition();
        // Pixels/sec: TimelineComponent lays blocks out with
        const double pps = std::max(1.0, (double) tl->getZoomLevel() * 10.0);
        const double dxSec = (double)(p.x - m_dragOrigin.x) / pps;

        auto snap = [&](double sec) -> double {
            const auto mode = tl->getSnapMode();
            if (mode == TimelineComponent::SnapMode::Off) return sec;
            const double bpm = std::max(60.0, (double) m_track.bpm);
            const double beatSec = 60.0 / bpm;
            double unit = beatSec;
            switch (mode) {
                case TimelineComponent::SnapMode::Bar:       unit = beatSec * 4.0; break;
                case TimelineComponent::SnapMode::HalfBar:   unit = beatSec * 2.0; break;
                case TimelineComponent::SnapMode::Sixteenth: unit = beatSec * 0.25; break;
                default: break;
            }
            return std::round(sec / unit) * unit;
        };

        switch (m_dragMode) {
            case DragMode::MoveClip: {
                const double nt = std::max(0.0, snap(m_dragStartTime + dxSec));
                m_startTime = nt;
                tl->resized();  // recompute all block pixel bounds
                if (onClipChanged) onClipChanged();
                repaint();
                return;
            }
            case DragMode::ResizeLeft: {
                const double ns = juce::jlimit(0.0,
                    m_dragStartAudioEnd - 0.1,
                    snap(m_dragStartAudioStart + dxSec));
                const double delta = ns - m_dragStartAudioStart;
                setAudioRange(ns, m_dragStartAudioEnd);
                m_startTime = std::max(0.0, m_dragStartTime + delta);
                tl->resized();
                if (onClipChanged) onClipChanged();
                repaint();
                return;
            }
            case DragMode::ResizeRight: {
                const double ne = juce::jlimit(m_dragStartAudioStart + 0.1,
                    std::max(0.1, (double) m_track.duration + m_dragStartAudioStart),
                    snap(m_dragStartAudioEnd + dxSec));
                setAudioRange(m_dragStartAudioStart, ne);
                tl->resized();
                if (onClipChanged) onClipChanged();
                repaint();
                return;
            }
            case DragMode::FadeInHandle: {
                const double cap = std::max(0.1, m_track.duration * 0.5);
                const double fi = juce::jlimit(0.0, cap, m_dragStartFadeIn + dxSec);
                setFadeIn(fi, fadeInCurve);
                if (onClipChanged) onClipChanged();
                return;
            }
            case DragMode::FadeOutHandle: {
                const double cap = std::max(0.1, m_track.duration * 0.5);
                const double fo = juce::jlimit(0.0, cap, m_dragStartFadeOut - dxSec);
                setFadeOut(fo, fadeOutCurve);
                if (onClipChanged) onClipChanged();
                return;
            }
            default: break;
        }
    }

    if (manualBeatDragIndex >= 0 && manualBeatDragIndex < (int)beatGridPositions.size()
        && m_track.duration > 0)
    {
        const float waveX = 4.0f;
        const float waveW = (float)getWidth() - 8;
        double newT = (((double)e.getPosition().x - waveX) / waveW) * m_track.duration;
        const double lo = (manualBeatDragIndex > 0)
            ? beatGridPositions[manualBeatDragIndex - 1] + 0.001 : 0.0;
        const double hi = (manualBeatDragIndex + 1 < (int)beatGridPositions.size())
            ? beatGridPositions[manualBeatDragIndex + 1] - 0.001 : m_track.duration;
        newT = juce::jlimit(lo, hi, newT);
        beatGridPositions[manualBeatDragIndex] = newT;
        beatGridBarPositions.clear();
        for (size_t i = 0; i < beatGridPositions.size(); i += 4)
            beatGridBarPositions.push_back(beatGridPositions[i]);
        if (auto* parent = getParentComponent()) parent->repaint();
        repaint();
        return;
    }

    if (m_selecting && m_track.duration > 0)
    {
        float waveX = 4.0f;
        float waveW = (float)getWidth() - 8;
        double timeAtPos = (((float)e.getPosition().x - waveX) / waveW) * m_track.duration;
        m_selEnd = juce::jlimit(0.0, m_track.duration, timeAtPos);
        if (m_selEnd < m_selStart) std::swap(m_selStart, m_selEnd);
        repaint();
    }
}

void MixLabView::TimelineTrackBlock::mouseMove(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    const auto mode = pickDragMode(pos);
    switch (mode) {
        case DragMode::ResizeLeft:
        case DragMode::ResizeRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case DragMode::FadeInHandle:
        case DragMode::FadeOutHandle:
        case DragMode::MoveClip:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        default:
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    const bool wasHovered = m_handleHovered;
    m_handleHovered = (pos.y < 8);
    if (wasHovered != m_handleHovered) repaint();
}

void MixLabView::TimelineTrackBlock::mouseUp(const juce::MouseEvent&)
{
    m_dragMode = DragMode::None;
    m_dragging = false;
    m_selecting = false;
    manualBeatDragIndex = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

MixLabView::NavigationBar::NavigationBar() = default;

void MixLabView::NavigationBar::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xFF0A0A12));
    g.fillRoundedRectangle(b, 2.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b, 2.0f, 1.0f);

    if (m_blocks.empty()) return;

    double totalDur = 0;
    for (auto* bl : m_blocks)
        if (bl) totalDur += bl->getTrack().duration;
    if (totalDur <= 0) return;

    float x = 2.0f;
    int idx = 0;
    for (auto* bl : m_blocks)
    {
        if (!bl) continue;
        float ratio = (float)(bl->getTrack().duration / totalDur);
        float w = (b.getWidth() - 4) * ratio;

        static const juce::uint32 colors[] = { 0xFF3B82F6, 0xFF8B5CF6, 0xFF22C55E, 0xFFF59E0B };
        g.setColour(juce::Colour(colors[idx % 4]).withAlpha(0.6f));
        g.fillRect(x, 2.0f, w - 1, b.getHeight() - 4);

        x += w;
        idx++;
    }

    if (m_viewEnd > m_viewStart)
    {
        float vx1 = (float)(m_viewStart / totalDur) * (b.getWidth() - 4) + 2;
        float vx2 = (float)(m_viewEnd / totalDur) * (b.getWidth() - 4) + 2;
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.fillRect(vx1, 0.0f, vx2 - vx1, b.getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawRect(vx1, 0.0f, vx2 - vx1, b.getHeight(), 1.0f);
    }

    if (m_playheadPos > 0)
    {
        float phX = (float)(m_playheadPos / totalDur) * (b.getWidth() - 4) + 2;
        g.setColour(juce::Colour(0xFFEF4444));
        g.drawVerticalLine((int)phX, 0.0f, b.getHeight());
    }
}

void MixLabView::NavigationBar::mouseDown(const juce::MouseEvent& e)
{
    if (m_blocks.empty()) return;
    double totalDur = 0;
    for (auto* bl : m_blocks) if (bl) totalDur += bl->getTrack().duration;
    if (totalDur <= 0) return;

    double clickRatio = (double)e.getPosition().x / getWidth();
    double seekTime = clickRatio * totalDur;
    if (onSeek) onSeek(seekTime);
}

void MixLabView::NavigationBar::mouseDrag(const juce::MouseEvent& e)
{
    mouseDown(e);
}

MixLabView::TempoLane::TempoLane() = default;

void MixLabView::TempoLane::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xFF0A0A12));
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b, 3.0f, 1.0f);

    g.setFont(juce::Font(8.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF94A3B8));
    g.drawText("TEMPO", 4, 2, 60, 10, juce::Justification::centredLeft);

    static const char* modeNames[] = { "AUTO", "MANUAL", "FIXED" };
    g.setColour(Colors::accent());
    g.drawText(modeNames[(int)m_mode], 50, 2, 60, 10, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFF64748B));
    g.setFont(juce::Font(7.0f));
    g.drawText(juce::String(m_maxBpm, 0), (int)b.getWidth() - 24, 12, 22, 8, juce::Justification::centredRight);
    g.drawText(juce::String(m_minBpm, 0), (int)b.getWidth() - 24, (int)b.getHeight() - 10, 22, 8, juce::Justification::centredRight);

    if (m_points.empty()) return;

    auto bpmToY = [&](double bpm) {
        float ratio = (float)((bpm - m_minBpm) / (m_maxBpm - m_minBpm));
        return b.getBottom() - 4 - ratio * (b.getHeight() - 18);
    };

    juce::Path linePath;
    bool first = true;
    for (auto& pt : m_points)
    {
        float x = pt.time / 600.0f * (b.getWidth() - 30); // 600s = 10min visible
        float y = bpmToY(pt.bpm);
        if (first) { linePath.startNewSubPath(x, y); first = false; }
        else linePath.lineTo(x, y);

        g.setColour(Colors::accent());
        g.fillEllipse(x - 3, y - 3, 6, 6);
    }
    g.setColour(Colors::accent().withAlpha(0.6f));
    g.strokePath(linePath, juce::PathStrokeType(1.5f));
}


MixLabView::TempoLane::PointPx MixLabView::TempoLane::pointToPx(double time, double bpm) const
{
    auto b = getLocalBounds().toFloat();
    const float ratio = static_cast<float>((bpm - m_minBpm) / (m_maxBpm - m_minBpm));
    const float x = static_cast<float>(time / m_visibleSeconds) * (b.getWidth() - 30.0f);
    const float y = b.getBottom() - 4.0f - ratio * (b.getHeight() - 18.0f);
    return { x, y };
}

bool MixLabView::TempoLane::pxToPoint(int x, int y, double& outTime, double& outBpm) const
{
    const float w = static_cast<float>(getWidth() - 30);
    const float h = static_cast<float>(getHeight() - 18);
    if (w <= 0 || h <= 0) return false;
    outTime = juce::jlimit(0.0, m_visibleSeconds,
                           static_cast<double>(x) / w * m_visibleSeconds);
    const float yOffset = static_cast<float>(y - 4);
    const float ratio   = juce::jlimit(0.0f, 1.0f, 1.0f - yOffset / h);
    outBpm = m_minBpm + static_cast<double>(ratio) * (m_maxBpm - m_minBpm);
    return true;
}

int MixLabView::TempoLane::hitPoint(int x, int y, int hitRadiusPx) const
{
    int best = -1;
    int bestDist2 = hitRadiusPx * hitRadiusPx;
    for (int i = 0; i < (int) m_points.size(); ++i) {
        const auto px = pointToPx(m_points[i].time, m_points[i].bpm);
        const int dx = static_cast<int>(px.x) - x;
        const int dy = static_cast<int>(px.y) - y;
        const int d2 = dx * dx + dy * dy;
        if (d2 <= bestDist2) {
            bestDist2 = d2;
            best = i;
        }
    }
    return best;
}

void MixLabView::TempoLane::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown()) {
        const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
        if (idx >= 0) {
            removePoint(idx);
            if (onChanged) onChanged();
            repaint();
        }
        return;
    }

    const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
    if (idx >= 0) {
        m_draggedPoint = idx;
        return;
    }

    if (m_mode == TempoMode::Manual) {
        double t = 0.0, bpm = 0.0;
        if (pxToPoint(e.getPosition().x, e.getPosition().y, t, bpm)) {
            addPoint(t, bpm);
            for (int i = 0; i < (int) m_points.size(); ++i) {
                if (std::abs(m_points[i].time - t) < 1e-6
                 && std::abs(m_points[i].bpm  - bpm) < 1e-6) {
                    m_draggedPoint = i;
                    break;
                }
            }
            if (onChanged) onChanged();
            repaint();
        }
    }
}

void MixLabView::TempoLane::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggedPoint < 0 || m_draggedPoint >= (int) m_points.size()) return;
    double t = 0.0, bpm = 0.0;
    if (!pxToPoint(e.getPosition().x, e.getPosition().y, t, bpm)) return;
    m_points[m_draggedPoint].time = t;
    m_points[m_draggedPoint].bpm  = bpm;
    if (onChanged) onChanged();
    repaint();
}

void MixLabView::TempoLane::mouseUp(const juce::MouseEvent&)
{
    if (m_draggedPoint >= 0) {
        // Re-sort points by time after the drag (positions may have crossed).
        std::sort(m_points.begin(), m_points.end(),
                  [](const TempoPoint& a, const TempoPoint& b) { return a.time < b.time; });
        m_draggedPoint = -1;
        if (onChanged) onChanged();
        repaint();
    }
}

void MixLabView::TempoLane::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
    if (idx >= 0) {
        removePoint(idx);
        if (onChanged) onChanged();
        repaint();
    }
}

void MixLabView::TempoLane::addPoint(double time, double bpm)
{
    m_points.push_back({ time, bpm });
    std::sort(m_points.begin(), m_points.end(), [](const TempoPoint& a, const TempoPoint& b) {
        return a.time < b.time;
    });
}

void MixLabView::TempoLane::removePoint(int index)
{
    if (index >= 0 && index < (int)m_points.size())
        m_points.erase(m_points.begin() + index);
}

MixLabView::MixerStrip::MixerStrip(const juce::String& label)
    : m_label(label)
{
    auto makeKnob = [this]() {
        auto k = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        k->setRange(0.0, 2.0, 0.01);
        k->setValue(1.0, juce::dontSendNotification);
        k->setColour(juce::Slider::rotarySliderFillColourId, Colors::primary());
        k->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF1E293B));
        addAndMakeVisible(k.get());
        return k;
    };

    m_eqHiKnob = makeKnob();
    m_eqMidKnob = makeKnob();
    m_eqLoKnob = makeKnob();

    m_filterKnob = makeKnob();
    m_filterKnob->setRange(0.0, 1.0, 0.01);
    m_filterKnob->setValue(0.5);
    m_filterKnob->setColour(juce::Slider::rotarySliderFillColourId, Colors::accent());

    m_volumeFader = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    m_volumeFader->setRange(0.0, 1.0, 0.01);
    m_volumeFader->setValue(1.0, juce::dontSendNotification);
    m_volumeFader->setColour(juce::Slider::trackColourId, Colors::primary());
    m_volumeFader->setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF1E293B));
    addAndMakeVisible(*m_volumeFader);

    auto makeBtn = [this](const juce::String& txt, juce::Colour col) {
        auto b = std::make_unique<juce::TextButton>(txt);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1E293B));
        b->setColour(juce::TextButton::textColourOffId, col);
        b->setColour(juce::TextButton::buttonOnColourId, col.withAlpha(0.4f));
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        b->setClickingTogglesState(true);
        addAndMakeVisible(b.get());
        return b;
    };
    m_soloBtn = makeBtn("S", juce::Colour(0xFFF59E0B));
    m_muteBtn = makeBtn("M", juce::Colour(0xFFEF4444));
    m_autoBtn = makeBtn("A", Colors::accent());

    m_soloBtn->onClick = [this] {
        isSolo = m_soloBtn->getToggleState();
        spdlog::info("[MixLab] Mixer '{}' Solo={}", m_label.toStdString(), isSolo);
    };
    m_muteBtn->onClick = [this] {
        isMuted = m_muteBtn->getToggleState();
        if (m_volumeFader) {
            m_volumeFader->setColour(juce::Slider::trackColourId,
                isMuted ? juce::Colour(0xFF475569) : Colors::primary());
            m_volumeFader->repaint();
        }
        spdlog::info("[MixLab] Mixer '{}' Mute={}", m_label.toStdString(), isMuted);
    };
    m_autoBtn->onClick = [this] {
        // "Auto" means "Auto-bypass EQ/Filter automation" — when OFF the
        isAutoBypass = !m_autoBtn->getToggleState();
        spdlog::info("[MixLab] Mixer '{}' AutoBypass={}", m_label.toStdString(), isAutoBypass);
    };
}

void MixLabView::MixerStrip::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b, 4.0f, 1.0f);

    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(Colors::primary());
    g.drawText(m_label, 0, 4, (int)b.getWidth(), 12, juce::Justification::centred);

    g.setFont(juce::Font(7.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF64748B));
    g.drawText("HI",  0, 22, (int)b.getWidth(), 8, juce::Justification::centred);
    g.drawText("MID", 0, 60, (int)b.getWidth(), 8, juce::Justification::centred);
    g.drawText("LO",  0, 98, (int)b.getWidth(), 8, juce::Justification::centred);
    g.drawText("FLT", 0, 136, (int)b.getWidth(), 8, juce::Justification::centred);
}

void MixLabView::MixerStrip::resized()
{
    int w = getWidth();
    int knobSize = juce::jmin(28, w - 4);
    int kx = (w - knobSize) / 2;

    m_eqHiKnob->setBounds(kx, 30, knobSize, knobSize);
    m_eqMidKnob->setBounds(kx, 68, knobSize, knobSize);
    m_eqLoKnob->setBounds(kx, 106, knobSize, knobSize);
    m_filterKnob->setBounds(kx, 144, knobSize, knobSize);

    int btnW = (w - 8) / 3;
    int btnY = 178;
    m_soloBtn->setBounds(2, btnY, btnW, 16);
    m_muteBtn->setBounds(2 + btnW + 1, btnY, btnW, 16);
    m_autoBtn->setBounds(2 + (btnW + 1) * 2, btnY, btnW, 16);

    int faderTop = btnY + 22;
    int faderH = getHeight() - faderTop - 4;
    if (faderH < 30) faderH = 30;
    m_volumeFader->setBounds((w - 12) / 2, faderTop, 12, faderH);
}

class TimelineContentComponent : public juce::Component,
                                 public juce::SettableTooltipClient
{
public:
    MixLabView::TimelineComponent* timeline = nullptr;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF0A0A12));

        if (timeline)
        {
            if (timeline->showBeatGrid) timeline->paintBeatGrid(g);
            timeline->paintTransitions(g);
        }
    }

    // Crossfade point editing state. -1/-1 when not dragging.
    int m_dragTrans = -1;
    int m_dragPointIdx = -1;

    // Anchor drag state (Studio `<-> ` orange chevrons). -1 = idle.
    int    m_dragAnchorTrans = -1;
    int    m_dragAnchorSide  = -1;
    int    m_dragAnchorStartX = 0;
    double m_dragAnchorStartDur = 0.0;

    static constexpr float kAnchorHitW = 10.0f;

    // Returns 0 (left anchor), 1 (right anchor), or -1 (none) for the given
    int hitAnchor(int transIdx, juce::Point<int> pos) const
    {
        if (!timeline) return -1;
        auto* a = timeline->tryGetBlock(transIdx);
        auto* b = timeline->tryGetBlock(transIdx + 1);
        if (!a || !b) return -1;
        const float zoneX = (float)a->getRight();
        const float zoneR = (float)b->getX();
        const float zoneW = zoneR - zoneX;
        if (zoneW <= 6.0f) return -1;
        const float ay = (float)a->getY() + 8.0f;
        const float ah = juce::jmax(14.0f, (float)a->getHeight() - 16.0f);
        if (pos.y < ay || pos.y > ay + ah) return -1;
        if (std::abs((float)pos.x - zoneX) <= kAnchorHitW) return 0;
        if (std::abs((float)pos.x - zoneR) <= kAnchorHitW) return 1;
        return -1;
    }

    int anchorAt(juce::Point<int> pos, int& outSide) const
    {
        if (!timeline) return -1;
        const int n = timeline->getTrackCount();
        const int t = (int)timeline->getTransitions().size();
        for (int i = 0; i + 1 < n && i < t; ++i) {
            const int side = hitAnchor(i, pos);
            if (side >= 0) { outSide = side; return i; }
        }
        outSide = -1;
        return -1;
    }

    juce::Rectangle<float> curveRectFor(int i) const
    {
        if (!timeline) return {};
        auto* a = timeline->tryGetBlock(i);
        auto* b = timeline->tryGetBlock(i + 1);
        if (!a || !b) return {};
        const float x = (float)a->getRight();
        const float w = (float)(b->getX() - a->getRight());
        const float y = (float)a->getY();
        const float h = (float)a->getHeight();
        if (w <= 6.0f) return {};
        return juce::Rectangle<float>(x, y, w, h)
                   .reduced(8.0f, 0.0f)
                   .withTrimmedTop(22.0f)
                   .withTrimmedBottom(8.0f);
    }

    int hitPoint(int i, juce::Point<int> pos, float radiusPx = 8.0f) const
    {
        if (!timeline) return -1;
        auto& trans = timeline->getTransitions();
        if (i < 0 || i >= (int)trans.size()) return -1;
        const auto rect = curveRectFor(i);
        if (rect.isEmpty()) return -1;
        const auto& pts = trans[i].crossfadePoints;
        for (int k = 0; k < (int)pts.size(); ++k) {
            const float px = rect.getX() + (float)pts[k].time * rect.getWidth();
            const float py = rect.getBottom() - (float)pts[k].value * rect.getHeight();
            const float dx = px - (float)pos.x;
            const float dy = py - (float)pos.y;
            if (dx * dx + dy * dy <= radiusPx * radiusPx) return k;
        }
        return -1;
    }

    int transAt(juce::Point<int> pos) const
    {
        if (!timeline) return -1;
        const int n = timeline->getTrackCount();
        const int t = (int)timeline->getTransitions().size();
        for (int i = 0; i + 1 < n && i < t; ++i) {
            auto rect = curveRectFor(i);
            if (!rect.isEmpty() && rect.contains((float)pos.x, (float)pos.y))
                return i;
        }
        return -1;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!timeline) return;

        const auto pos = e.getPosition();

        {
            int side = -1;
            const int ti = anchorAt(pos, side);
            if (ti >= 0) {
                if (e.mods.isRightButtonDown()) {
                    showAnchorContextMenu(ti, side, pos);
                    return;
                }
                auto& trans = timeline->getTransitions();
                if (ti < (int)trans.size()) {
                    timeline->m_selectedTransition = ti;
                    timeline->m_selectedBlock = -1;
                    m_dragAnchorTrans   = ti;
                    m_dragAnchorSide    = side;
                    m_dragAnchorStartX  = pos.x;
                    m_dragAnchorStartDur = trans[ti].durationSeconds > 0.0
                                              ? trans[ti].durationSeconds
                                              : (trans[ti].durationBars * 4.0 * 0.5); // fallback ~0.5s/bar
                    if (auto* parent = getParentComponent()) parent->repaint();
                    repaint();
                    return;
                }
            }
        }

        if (e.mods.isRightButtonDown()) {
            const int ti = transAt(pos);
            if (ti >= 0) {
                const int pi = hitPoint(ti, pos);
                if (pi >= 0) {
                    showPointContextMenu(ti, pi, pos);
                    return;
                }
                showTransitionContextMenu(ti, pos);
                return;
            }
            timeline->handleContentClick(e);
            return;
        }

        const int ti = transAt(pos);
        if (ti >= 0) {
            timeline->m_selectedTransition = ti;
            timeline->m_selectedBlock = -1;
            auto& trans = timeline->getTransitions();
            int pi = hitPoint(ti, pos);
            if (pi < 0) {
                const auto rect = curveRectFor(ti);
                const float u = juce::jlimit(0.0f, 1.0f,
                    (float)(pos.x - rect.getX()) / rect.getWidth());
                const float v = juce::jlimit(0.0f, 1.0f,
                    1.0f - (float)(pos.y - rect.getY()) / rect.getHeight());
                MixLabView::EQAutomationPoint p; p.time = u; p.value = v;
                auto& pts = trans[ti].crossfadePoints;
                auto it = std::lower_bound(pts.begin(), pts.end(), p,
                    [](const MixLabView::EQAutomationPoint& a,
                       const MixLabView::EQAutomationPoint& b){
                        return a.time < b.time;
                    });
                pi = (int)(it - pts.begin());
                pts.insert(it, p);
            }
            m_dragTrans = ti;
            m_dragPointIdx = pi;
            repaint();
            return;
        }

        timeline->handleContentClick(e);
    }

    // Forward decls — defined right below the class so they can call the
    void showAnchorContextMenu     (int ti, int side, juce::Point<int> screenPos);
    void showPointContextMenu      (int ti, int pi,   juce::Point<int> screenPos);
    void showTransitionContextMenu (int ti,           juce::Point<int> screenPos);

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!timeline) return;

        if (m_dragAnchorTrans >= 0 && m_dragAnchorSide >= 0) {
            auto& trans = timeline->getTransitions();
            if (m_dragAnchorTrans >= (int)trans.size()) {
                m_dragAnchorTrans = m_dragAnchorSide = -1;
                return;
            }
            const float pixelsPerSecond =
                static_cast<float>(timeline->getZoomLevel()) * 10.0f;
            const float deltaPx = (float)(e.getPosition().x - m_dragAnchorStartX);
            const double sign     = (m_dragAnchorSide == 0) ? -1.0 : +1.0;
            const bool   freeMode = e.mods.isShiftDown();
            const double deltaSec = sign * (double)deltaPx / std::max(1.0f, pixelsPerSecond);
            double newDur = juce::jlimit(0.5, 600.0, m_dragAnchorStartDur + deltaSec);

            double bpm = 0.0;
            if (auto* a = timeline->tryGetBlock(m_dragAnchorTrans)) {
                if (a->getTrack().bpm > 0) bpm = (double)a->getTrack().bpm;
            }

            if (bpm <= 0.0) {
                trans[m_dragAnchorTrans].durationSeconds = newDur;
            } else {
                const double secPerBar = 4.0 * 60.0 / bpm;
                if (!freeMode) {
                    const int   bars  = std::max(1, (int)std::round(newDur / secPerBar));
                    newDur = bars * secPerBar;
                    trans[m_dragAnchorTrans].durationBars    = bars;
                    trans[m_dragAnchorTrans].durationSeconds = newDur;
                } else {
                    trans[m_dragAnchorTrans].durationSeconds = newDur;
                    trans[m_dragAnchorTrans].durationBars    =
                        std::max(1, (int)std::round(newDur / secPerBar));
                }
            }

            {
                juce::String s;
                s << "Length: "
                  << juce::String(trans[m_dragAnchorTrans].durationBars)
                  << " bars ("
                  << juce::String(trans[m_dragAnchorTrans].durationSeconds, 2)
                  << " s)";
                setTooltip(s);
            }

            if (auto* parent = getParentComponent()) parent->repaint();
            repaint();
            return;
        }

        if (m_dragTrans < 0 || m_dragPointIdx < 0) return;
        auto& trans = timeline->getTransitions();
        if (m_dragTrans >= (int)trans.size()) { m_dragTrans = -1; return; }
        auto& pts = trans[m_dragTrans].crossfadePoints;
        if (m_dragPointIdx >= (int)pts.size())   { m_dragPointIdx = -1; return; }

        const auto rect = curveRectFor(m_dragTrans);
        if (rect.isEmpty()) return;

        const float fineScale = e.mods.isShiftDown() ? 0.25f : 1.0f;
        const float dx = (float)(e.getPosition().x - e.getMouseDownX()) * fineScale;
        const float dy = (float)(e.getPosition().y - e.getMouseDownY()) * fineScale;
        const float u = juce::jlimit(0.0f, 1.0f,
            (float)(e.getMouseDownX() + dx - rect.getX()) / rect.getWidth());
        const float v = juce::jlimit(0.0f, 1.0f,
            1.0f - (float)(e.getMouseDownY() + dy - rect.getY()) / rect.getHeight());

        pts[m_dragPointIdx].time  = u;
        pts[m_dragPointIdx].value = v;

        {
            const float gain = juce::jmax(1.0e-4f, v);
            const float dB   = 20.0f * std::log10(gain);
            juce::String s;
            s << "Track Volume: "
              << (dB >= 0.0f ? "+" : "")
              << juce::String(dB, 2) << " dB";
            setTooltip(s);
        }

        std::stable_sort(pts.begin(), pts.end(),
            [](const MixLabView::EQAutomationPoint& a,
               const MixLabView::EQAutomationPoint& b){
                return a.time < b.time;
            });
        for (int k = 0; k < (int)pts.size(); ++k)
            if (pts[k].time == u && pts[k].value == v) { m_dragPointIdx = k; break; }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        m_dragTrans = -1;
        m_dragPointIdx = -1;
        m_dragAnchorTrans = -1;
        m_dragAnchorSide = -1;
        setTooltip({});
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (!timeline) return;
        const auto pos = e.getPosition();

        // 1) Anchor hover takes priority — encode as -(2*i+2) (left) / -(2*i+3) (right).
        int hover = -1;
        {
            int side = -1;
            const int ai = anchorAt(pos, side);
            if (ai >= 0) {
                hover = (side == 0) ? -(2 * ai + 2) : -(2 * ai + 3);
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            } else {
                setMouseCursor(juce::MouseCursor::NormalCursor);
            }
        }

        if (hover == -1) {
            const int n = timeline->getTrackCount();
            const int t = (int)timeline->getTransitions().size();
            for (int i = 0; i + 1 < n && i < t; ++i) {
                auto* a = timeline->tryGetBlock(i);
                auto* b = timeline->tryGetBlock(i + 1);
                if (!a || !b) continue;
                const int x1 = a->getRight();
                const int x2 = b->getX();
                if (x2 <= x1) continue;
                if (pos.x >= x1 && pos.x < x2 && pos.y >= a->getY() && pos.y < a->getY() + a->getHeight()) {
                    hover = i;
                    break;
                }
            }
        }

        if (hover != timeline->m_hoverTransitionHandle) {
            timeline->m_hoverTransitionHandle = hover;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (timeline && timeline->m_hoverTransitionHandle != -1) {
            timeline->m_hoverTransitionHandle = -1;
            repaint();
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (timeline) timeline->mouseWheelMove(e, w);
    }
};

inline void TimelineContentComponent::showAnchorContextMenu(int ti, int side, juce::Point<int>)
{
    if (!timeline) return;
    auto& trans = timeline->getTransitions();
    if (ti < 0 || ti >= (int)trans.size()) return;

    juce::PopupMenu menu;
    menu.addSectionHeader(side == 0 ? BM_TJ("studio.transition.outroAnchor")
                                    : BM_TJ("studio.transition.introAnchor"));
    menu.addItem(1, BM_TJ("studio.menu.transition.cutEnd"));
    menu.addItem(2, BM_TJ("studio.menu.transition.softEnd"));
    menu.addItem(3, BM_TJ("studio.menu.transition.curveEnd"));
    menu.addSeparator();
    menu.addItem(4, BM_TJ("studio.menu.transition.snapToBeat"));

    juce::Component::SafePointer<TimelineContentComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe, ti, side](int r) {
            auto* self = safe.getComponent();
            if (!self || !self->timeline) return;
            auto& tt = self->timeline->getTransitions();
            if (ti >= (int)tt.size()) return;
            auto& info = tt[ti];
            using ES = MixLabView::EndShape;
            ES val = info.leftEnd;
            if      (r == 1) val = ES::Cut;
            else if (r == 2) val = ES::Soft;
            else if (r == 3) val = ES::Curve;
            else if (r == 4) {
                auto* a = self->timeline->tryGetBlock(ti);
                const double bpm = (a && a->getTrack().bpm > 0) ? (double)a->getTrack().bpm : 0.0;
                if (bpm <= 0.0) {
                    BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
                        juce::String::fromUTF8(u8"BPM inconnu"),
                        juce::String::fromUTF8(u8"Analysez la piste avant de snapper au beat."),
                        BeatMate::UI::Widgets::ToastNotifier::Kind::Warning);
                    return;
                }
                const double secPerBeat = 60.0 / bpm;
                const double snapped = std::round(info.durationSeconds / secPerBeat) * secPerBeat;
                info.durationSeconds = std::max(secPerBeat, snapped);
                if (auto* p = self->getParentComponent()) p->repaint();
                self->repaint();
                return;
            } else return;

            if (side == 0) info.leftEnd = val;
            else           info.rightEnd = val;
            if (auto* p = self->getParentComponent()) p->repaint();
            self->repaint();
        });
}

inline void TimelineContentComponent::showPointContextMenu(int ti, int pi, juce::Point<int>)
{
    if (!timeline) return;
    auto& trans = timeline->getTransitions();
    if (ti < 0 || ti >= (int)trans.size()) return;
    auto& pts = trans[ti].crossfadePoints;
    if (pi < 0 || pi >= (int)pts.size()) return;

    juce::PopupMenu menu;
    menu.addItem(1, BM_TJ("studio.menu.transition.deletePoint"));
    menu.addItem(2, BM_TJ("studio.menu.transition.resetPoint0dB"));
    menu.addItem(3, BM_TJ("studio.menu.transition.editPointValue"));

    juce::Component::SafePointer<TimelineContentComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe, ti, pi](int r) {
            auto* self = safe.getComponent();
            if (!self || !self->timeline) return;
            auto& tt = self->timeline->getTransitions();
            if (ti >= (int)tt.size()) return;
            auto& pts = tt[ti].crossfadePoints;
            if (pi >= (int)pts.size()) return;
            if (r == 1) {
                pts.erase(pts.begin() + pi);
            } else if (r == 2) {
                pts[pi].value = 1.0f;
            } else if (r == 3) {
                auto win = std::make_shared<juce::AlertWindow>(
                    BM_TJ("studio.menu.transition.editPointValue"),
                    juce::String("Volume (dB) :"),
                    juce::MessageBoxIconType::QuestionIcon);
                const float curDb = 20.0f * std::log10(juce::jmax(1.0e-4f, pts[pi].value));
                win->addTextEditor("v", juce::String(curDb, 2));
                win->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                win->addButton(BM_TJ("common.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                win->enterModalState(true, juce::ModalCallbackFunction::create(
                    [safe, ti, pi, win](int rr) {
                        auto* s2 = safe.getComponent();
                        if (rr != 1 || !s2 || !s2->timeline) return;
                        auto& tt2 = s2->timeline->getTransitions();
                        if (ti >= (int)tt2.size()) return;
                        auto& p2 = tt2[ti].crossfadePoints;
                        if (pi >= (int)p2.size()) return;
                        const double dB = win->getTextEditorContents("v").getDoubleValue();
                        p2[pi].value = juce::jlimit(0.0f, 1.0f, (float)std::pow(10.0, dB / 20.0));
                        if (auto* pp = s2->getParentComponent()) pp->repaint();
                        s2->repaint();
                    }));
                return;
            }
            if (auto* p = self->getParentComponent()) p->repaint();
            self->repaint();
        });
}

inline void TimelineContentComponent::showTransitionContextMenu(int ti, juce::Point<int> pos)
{
    if (!timeline) return;
    auto& trans = timeline->getTransitions();
    if (ti < 0 || ti >= (int)trans.size()) return;

    const auto rect = curveRectFor(ti);
    const float u = rect.isEmpty() ? 0.5f : juce::jlimit(0.0f, 1.0f,
        (float)(pos.x - rect.getX()) / std::max(1.0f, rect.getWidth()));
    const float v = rect.isEmpty() ? 0.5f : juce::jlimit(0.0f, 1.0f,
        1.0f - (float)(pos.y - rect.getY()) / std::max(1.0f, rect.getHeight()));

    juce::PopupMenu typeSub;
    typeSub.addItem(101, BM_TJ("studio.menu.transition.typeCut"));
    typeSub.addItem(102, BM_TJ("studio.menu.transition.typeSoft"));
    typeSub.addItem(103, BM_TJ("studio.menu.transition.typeCurve"));

    juce::PopupMenu lengthSub;
    const int curBars = trans[ti].durationBars;
    lengthSub.addItem(301, BM_TJ("studio.menu.transition.beatmix4"),  true, curBars == 4);
    lengthSub.addItem(302, BM_TJ("studio.menu.transition.beatmix8"),  true, curBars == 8);
    lengthSub.addItem(303, BM_TJ("studio.menu.transition.beatmix16"), true, curBars == 16);
    lengthSub.addItem(304, BM_TJ("studio.menu.transition.beatmix32"), true, curBars == 32);
    lengthSub.addSeparator();
    lengthSub.addItem(305, BM_TJ("studio.menu.transition.beatmixCut"));

    juce::PopupMenu stemsSub;
    const bool dM = trans[ti].muteStems[0];
    const bool bM = trans[ti].muteStems[1];
    const bool mM = trans[ti].muteStems[2];
    const bool vM = trans[ti].muteStems[3];
    stemsSub.addItem(201, BM_TJ("studio.menu.transition.stems.muteDrums"),  true, dM);
    stemsSub.addItem(202, BM_TJ("studio.menu.transition.stems.muteBass"),   true, bM);
    stemsSub.addItem(203, BM_TJ("studio.menu.transition.stems.muteMelody"), true, mM);
    stemsSub.addItem(204, BM_TJ("studio.menu.transition.stems.muteVocals"), true, vM);
    stemsSub.addSeparator();
    stemsSub.addItem(205, BM_TJ("studio.menu.transition.stems.acapella"));
    stemsSub.addItem(206, BM_TJ("studio.menu.transition.stems.instrumental"));
    stemsSub.addItem(207, BM_TJ("studio.menu.transition.stems.reset"));

    juce::PopupMenu menu;
    menu.addSubMenu(BM_TJ("studio.menu.transition.length"), lengthSub);
    menu.addSubMenu(BM_TJ("studio.menu.transition.type"),   typeSub);
    menu.addItem(60, BM_TJ("studio.menu.transition.syncTempo"), true, trans[ti].syncTempo);
    menu.addSeparator();
    menu.addItem(1, BM_TJ("studio.menu.cut")    + juce::String("   Ctrl+X"));
    menu.addItem(2, BM_TJ("studio.menu.copy")   + juce::String("   Ctrl+C"));
    menu.addItem(3, BM_TJ("studio.menu.paste")  + juce::String("   Ctrl+V"));
    menu.addItem(4, BM_TJ("studio.menu.delete") + juce::String("   Del"));
    menu.addSeparator();
    menu.addItem(11, BM_TJ("studio.menu.transition.addVolumeMarker")    + juce::String("   V"));
    menu.addItem(12, BM_TJ("studio.menu.transition.addBassMarker")      + juce::String("   B"));
    menu.addItem(13, BM_TJ("studio.menu.transition.addMidrangeMarker")  + juce::String("   M"));
    menu.addItem(14, BM_TJ("studio.menu.transition.addTrebleMarker")    + juce::String("   T"));
    menu.addItem(15, BM_TJ("studio.menu.transition.addLabelMarker"),   false /*disabled, future*/, false);
    menu.addSeparator();
    menu.addSubMenu(BM_TJ("studio.menu.transition.stems"), stemsSub);
    menu.addSeparator();
    menu.addItem(50, BM_TJ("studio.menu.reverse"));

    juce::Component::SafePointer<TimelineContentComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe, ti, u, v](int r) {
            auto* self = safe.getComponent();
            if (!self || !self->timeline) return;
            auto& tt = self->timeline->getTransitions();
            if (ti >= (int)tt.size()) return;
            auto& info = tt[ti];

            using ES = MixLabView::EndShape;
            auto setBothEnds = [&](ES e) { info.leftEnd = e; info.rightEnd = e; };

            auto addMarker = [&](std::vector<MixLabView::EQAutomationPoint>& list) {
                MixLabView::EQAutomationPoint p; p.time = u; p.value = v;
                auto it = std::lower_bound(list.begin(), list.end(), p,
                    [](const MixLabView::EQAutomationPoint& a,
                       const MixLabView::EQAutomationPoint& b){ return a.time < b.time; });
                list.insert(it, p);
            };

            auto applyBars = [&](int bars) {
                info.durationBars = std::max(1, bars);
                double bpm = 120.0;
                if (auto* a = self->timeline->tryGetBlock(ti)) {
                    if (a->getTrack().bpm > 0) bpm = (double)a->getTrack().bpm;
                }
                info.durationSeconds = info.durationBars * 4.0 * 60.0 / std::max(1.0, bpm);
            };

            switch (r) {
                case 101: setBothEnds(ES::Cut);   break;
                case 102: setBothEnds(ES::Soft);  break;
                case 103: setBothEnds(ES::Curve); break;
                case 301: applyBars(4);  break;
                case 302: applyBars(8);  break;
                case 303: applyBars(16); break;
                case 304: applyBars(32); break;
                case 305:
                    applyBars(2);
                    setBothEnds(ES::Cut);
                    break;
                case 60:  info.syncTempo = !info.syncTempo; break;
                case 1:   self->timeline->copySelection(); self->timeline->deleteSelection(); break;
                case 2:   self->timeline->copySelection(); break;
                case 3:   self->timeline->pasteSelection(); break;
                case 4:   self->timeline->deleteSelection(); break;
                case 11:  addMarker(info.crossfadePoints); break;
                case 12:  addMarker(info.eqLoPoints);  break;
                case 13:  addMarker(info.eqMidPoints); break;
                case 14:  addMarker(info.eqHiPoints); break;
                case 50: {
                    for (auto& p : info.crossfadePoints) p.time = 1.0 - p.time;
                    std::sort(info.crossfadePoints.begin(), info.crossfadePoints.end(),
                              [](const MixLabView::EQAutomationPoint& a,
                                 const MixLabView::EQAutomationPoint& b){ return a.time < b.time; });
                    break;
                }
                case 201: info.muteStems[0] = !info.muteStems[0]; break;
                case 202: info.muteStems[1] = !info.muteStems[1]; break;
                case 203: info.muteStems[2] = !info.muteStems[2]; break;
                case 204: info.muteStems[3] = !info.muteStems[3]; break;
                case 205:
                    info.muteStems[0] = info.muteStems[1] = info.muteStems[2] = true;
                    info.muteStems[3] = false;
                    break;
                case 206:
                    info.muteStems[0] = info.muteStems[1] = info.muteStems[2] = false;
                    info.muteStems[3] = true;
                    break;
                case 207:
                    info.muteStems[0] = info.muteStems[1] = info.muteStems[2] = info.muteStems[3] = false;
                    break;
                default: return;
            }
            if (auto* p = self->getParentComponent()) p->repaint();
            self->repaint();
        });
}


MixLabView::TimelineComponent::TimelineComponent()
{
    m_viewport = std::make_unique<juce::Viewport>();
    auto* content = new TimelineContentComponent();
    content->timeline = this;
    m_viewport->setViewedComponent(content, true);
    m_viewport->setScrollBarsShown(false, true);
    addAndMakeVisible(*m_viewport);
}

void MixLabView::TimelineComponent::addTrack(const Models::Track& track)
{
    // Resolve filePath from the TrackDatabase when the caller passed an
    Models::Track t = track;
    if (t.filePath.empty() && t.id > 0 && ::g_serviceLocator) {
        if (auto* db = ::g_serviceLocator->tryGet<Services::Library::TrackDatabase>()) {
            if (auto full = db->getTrack(t.id)) {
                t = *full;
            }
        }
    }

    if (t.filePath.empty()) {
        spdlog::warn("[MixLab::addTrack] id={} title='{}' has no filePath — waveform will be synthetic",
                     t.id, t.title);
    } else if (!juce::File(t.filePath).existsAsFile()) {
        spdlog::warn("[MixLab::addTrack] file missing on disk: '{}' — waveform will be synthetic",
                     t.filePath);
    } else {
        // SINGLE-FILE prefetch: peaks are generated ONLY for this track's
        spdlog::info("[MixLab::addTrack] Single-file analyze: '{}' (folder siblings NOT touched)",
                     t.filePath);
        (void) getCachedPeaksShared(t.filePath, nullptr);
    }

    double insertTime = m_blocks.empty() ? 0 : m_blocks.back()->getEndTime();

    auto block = std::make_unique<TimelineTrackBlock>(t);
    block->setStartTime(insertTime);
    block->setEndTime(insertTime + t.duration);

    if (auto* content = m_viewport->getViewedComponent())
        content->addAndMakeVisible(block.get());
    m_blocks.push_back(std::move(block));

    if (m_blocks.size() > 1)
    {
        TransitionInfo ti;
        ti.type = TransitionType::EQBlend;
        ti.durationBars = 8;
        ti.curve = CurveType::SCurve;
        ti.syncTempo = true;
        const double bpm = (t.bpm > 0) ? (double)t.bpm : 0.0;
        ti.durationSeconds = bpm > 0.0 ? ti.durationBars * 4.0 * 60.0 / bpm : 16.0;
        m_transitions.push_back(ti);
    }

    resized();
}

void MixLabView::TimelineComponent::removeTrack(int index)
{
    if (index < 0 || index >= (int)m_blocks.size()) return;
    if (auto* content = m_viewport->getViewedComponent())
        content->removeChildComponent(m_blocks[index].get());
    m_blocks.erase(m_blocks.begin() + index);

    if (index > 0 && index - 1 < (int)m_transitions.size())
        m_transitions.erase(m_transitions.begin() + index - 1);
    else if (!m_transitions.empty() && index < (int)m_transitions.size())
        m_transitions.erase(m_transitions.begin() + index);

    resized();
}

void MixLabView::TimelineComponent::reorderTrack(int from, int to)
{
    if (from < 0 || from >= (int)m_blocks.size()) return;
    if (to < 0 || to >= (int)m_blocks.size()) return;
    auto item = std::move(m_blocks[from]);
    m_blocks.erase(m_blocks.begin() + from);
    m_blocks.insert(m_blocks.begin() + to, std::move(item));
    resized();
}

void MixLabView::TimelineComponent::insertBlock(int index,
                                                 std::unique_ptr<TimelineTrackBlock> block)
{
    if (!block) return;
    index = juce::jlimit(0, (int) m_blocks.size(), index);
    m_blocks.insert(m_blocks.begin() + index, std::move(block));
    if (auto& last = m_blocks[static_cast<size_t>(index)]) {
        addAndMakeVisible(*last);
    }
    resized();
    repaint();
}

void MixLabView::TimelineComponent::deleteSelection()
{
    if (m_selectedBlock < 0 || m_selectedBlock >= (int)m_blocks.size()) return;
    auto& block = m_blocks[m_selectedBlock];
    if (!block->hasSelection()) {
        spdlog::info("[Timeline] Delete block #{}", m_selectedBlock);
        m_blocks.erase(m_blocks.begin() + m_selectedBlock);
        m_selectedBlock = -1;
        if (!m_transitions.empty() && m_selectedBlock < (int)m_transitions.size()) {
            m_transitions.erase(m_transitions.begin() + std::max(0, m_selectedBlock - 1));
        }
        resized();
        repaint();
        return;
    }

    const double selStart = block->getSelectionStart();
    const double selEnd   = block->getSelectionEnd();
    const double aStart = block->getAudioStartSec();
    const double aEnd   = (block->getAudioEndSec() > 0.0)
                          ? block->getAudioEndSec() : block->getTrack().duration;
    const double dur    = aEnd - aStart;
    spdlog::info("[Timeline] Delete selection {}-{} of audio window {}-{}",
                 selStart, selEnd, aStart, aEnd);

    if (selStart <= 0.001) {
        block->setAudioRange(aStart + selEnd, aEnd);
        block->clearSelection();
    }
    else if (selEnd >= dur - 0.001) {
        block->setAudioRange(aStart, aStart + selStart);
        block->clearSelection();
    }
    else {
        block->setAudioRange(aStart, aStart + selStart);
        block->clearSelection();
        auto newBlock = std::make_unique<TimelineTrackBlock>(block->getTrack());
        newBlock->setAudioRange(aStart + selEnd, aEnd);
        newBlock->setStartTime(block->getStartTime() + selStart);
        m_blocks.insert(m_blocks.begin() + m_selectedBlock + 1, std::move(newBlock));
    }
    resized();
    repaint();
}

void MixLabView::TimelineComponent::copySelection()
{
    if (m_selectedBlock < 0 || m_selectedBlock >= (int) m_blocks.size()) {
        spdlog::warn("[Timeline] copySelection ABORT: no block selected (m_selectedBlock={})",
                     m_selectedBlock);
        return;
    }
    MixLabView::s_blockClipboard.clear();

    auto pushEntry = [](const TimelineTrackBlock& b,
                        double anchor) -> MixLabView::BlockClipboardEntry {
        MixLabView::BlockClipboardEntry e;
        e.track          = b.getTrack();
        if (b.hasSelection()) {
            e.selStart = b.getSelectionStart();
            e.selEnd   = b.getSelectionEnd();
        } else {
            e.selStart = b.getAudioStartSec();
            e.selEnd   = b.getAudioEndSec();
        }
        e.startRelToFirst    = b.getStartTime() - anchor;
        e.fadeInSec          = b.fadeInSec;
        e.fadeOutSec         = b.fadeOutSec;
        e.fadeInCurve        = (int) b.fadeInCurve;
        e.fadeOutCurve       = (int) b.fadeOutCurve;
        e.loopInTime         = b.loopInTime;
        e.loopOutTime        = b.loopOutTime;
        e.loopEnabled        = b.loopEnabled;
        e.cuePoints          = b.cuePoints;
        e.beatGridPositions  = b.beatGridPositions;
        e.beatGridBarPositions = b.beatGridBarPositions;
        e.beatGridMode       = b.beatGridMode;
        e.showStems          = b.showStems;
        auto cpAuto = [](const std::vector<TimelineTrackBlock::AutoPt>& src,
                         std::vector<std::pair<double, float>>& dst) {
            dst.clear();
            dst.reserve(src.size());
            for (auto& p : src) dst.emplace_back(p.time, p.value);
        };
        cpAuto(b.autoVolume,      e.autoVolume);
        cpAuto(b.autoEqHi,        e.autoEqHi);
        cpAuto(b.autoEqMid,       e.autoEqMid);
        cpAuto(b.autoEqLo,        e.autoEqLo);
        cpAuto(b.autoFilter,      e.autoFilter);
        cpAuto(b.autoPitch,       e.autoPitch);
        cpAuto(b.autoStemDrums,   e.autoStemDrums);
        cpAuto(b.autoStemBass,    e.autoStemBass);
        cpAuto(b.autoStemMelody,  e.autoStemMelody);
        cpAuto(b.autoStemVocals,  e.autoStemVocals);
        return e;
    };

    const double anchor = m_blocks[m_selectedBlock]->getStartTime();
    MixLabView::s_blockClipboard.push_back(pushEntry(*m_blocks[m_selectedBlock], anchor));
    spdlog::info("[Timeline] copySelection OK: 1 block on clipboard (full state, "
                 "track='{}' range=[{:.2f}..{:.2f}])",
                 MixLabView::s_blockClipboard.front().track.title,
                 MixLabView::s_blockClipboard.front().selStart,
                 MixLabView::s_blockClipboard.front().selEnd);
}

void MixLabView::TimelineComponent::pasteSelection()
{
    if (MixLabView::s_blockClipboard.empty()) {
        spdlog::warn("[Timeline] pasteSelection ABORT: clipboard empty");
        return;
    }
    spdlog::info("[Timeline] pasteSelection: {} entry(ies), playhead={:.3f}",
                 (int) MixLabView::s_blockClipboard.size(), m_playheadPos);

    const double base = m_playheadPos;
    int insertAt = (int) m_blocks.size();
    for (int i = 0; i < (int) m_blocks.size(); ++i) {
        if (m_blocks[i]->getStartTime() > base) { insertAt = i; break; }
    }

    for (const auto& e : MixLabView::s_blockClipboard) {
        Models::Track t = e.track;
        const double sliceLen = std::max(0.05, e.selEnd - e.selStart);
        t.duration = sliceLen;

        auto newBlock = std::make_unique<TimelineTrackBlock>(t);
        const double newStart = base + e.startRelToFirst;
        newBlock->setStartTime(newStart);
        newBlock->setEndTime(newStart + sliceLen);
        newBlock->setAudioRange(e.selStart, e.selEnd);
        newBlock->setFadeIn (e.fadeInSec,
                             (TimelineTrackBlock::FadeCurve) e.fadeInCurve);
        newBlock->setFadeOut(e.fadeOutSec,
                             (TimelineTrackBlock::FadeCurve) e.fadeOutCurve);
        if (e.loopEnabled && e.loopInTime >= 0.0 && e.loopOutTime > e.loopInTime)
            newBlock->setLoop(e.loopInTime, e.loopOutTime, true);
        newBlock->setCuePoints(e.cuePoints);
        newBlock->beatGridPositions    = e.beatGridPositions;
        newBlock->beatGridBarPositions = e.beatGridBarPositions;
        newBlock->beatGridMode         = e.beatGridMode;
        newBlock->showStems            = e.showStems;
        auto restAuto = [](const std::vector<std::pair<double, float>>& src,
                           std::vector<TimelineTrackBlock::AutoPt>& dst) {
            dst.clear();
            dst.reserve(src.size());
            for (auto& p : src) dst.push_back({ p.first, p.second });
        };
        restAuto(e.autoVolume,      newBlock->autoVolume);
        restAuto(e.autoEqHi,        newBlock->autoEqHi);
        restAuto(e.autoEqMid,       newBlock->autoEqMid);
        restAuto(e.autoEqLo,        newBlock->autoEqLo);
        restAuto(e.autoFilter,      newBlock->autoFilter);
        restAuto(e.autoPitch,       newBlock->autoPitch);
        restAuto(e.autoStemDrums,   newBlock->autoStemDrums);
        restAuto(e.autoStemBass,    newBlock->autoStemBass);
        restAuto(e.autoStemMelody,  newBlock->autoStemMelody);
        restAuto(e.autoStemVocals,  newBlock->autoStemVocals);

        // CRITIQUE (Â§ MixLab) — ajouter au viewport content sinon block invisible
        if (auto* content = m_viewport ? m_viewport->getViewedComponent() : nullptr)
            content->addAndMakeVisible(newBlock.get());

        m_blocks.insert(m_blocks.begin() + insertAt, std::move(newBlock));
        ++insertAt;
    }
    resized();
    repaint();
    spdlog::info("[Timeline] pasteSelection OK: now {} blocks", (int) m_blocks.size());
}


int MixLabView::TimelineComponent::quantizeAllClipsToBeat()
{
    int moved = 0;
    for (auto& block : m_blocks) {
        if (!block) continue;
        const auto& grid = block->beatGridPositions;
        if (grid.empty()) continue;
        const double origStart = block->getStartTime();
        double bestBeatAbs = origStart;
        double bestDiff = std::numeric_limits<double>::max();
        for (double gOffset : grid) {
            const double absBeat = block->getStartTime() + gOffset;
            const double diff = std::abs(absBeat - std::round(absBeat));
            (void) diff; // Suppress unused if we change strategy later.
        }
        const double bpm = block->getTrack().bpm;
        if (bpm <= 30.0) continue;
        const double beatSec = 60.0 / bpm;
        const double newStart = std::round(origStart / beatSec) * beatSec;
        if (std::abs(newStart - origStart) > 0.01) {
            block->setStartTime(newStart);
            ++moved;
        }
        (void) bestBeatAbs;
    }
    resized();
    repaint();
    spdlog::info("[Timeline] Quantize-All moved {} clip(s) to nearest beat", moved);
    return moved;
}

void MixLabView::TimelineComponent::addToMultiSelection(int idx)
{
    if (idx < 0 || idx >= (int) m_blocks.size()) return;
    if (std::find(m_multiSelection.begin(), m_multiSelection.end(), idx) == m_multiSelection.end())
        m_multiSelection.push_back(idx);
    repaint();
}

void MixLabView::TimelineComponent::removeFromMultiSelection(int idx)
{
    auto it = std::find(m_multiSelection.begin(), m_multiSelection.end(), idx);
    if (it != m_multiSelection.end()) m_multiSelection.erase(it);
    repaint();
}

bool MixLabView::TimelineComponent::isInMultiSelection(int idx) const
{
    return std::find(m_multiSelection.begin(), m_multiSelection.end(), idx) != m_multiSelection.end();
}

void MixLabView::TimelineComponent::goToStart()
{
    m_playheadPos = 0.0;
    m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
    repaint();
}

void MixLabView::TimelineComponent::goToEnd()
{
    double end = 0.0;
    for (auto& b : m_blocks)
        end = std::max(end, b->getStartTime() + b->getTrack().duration);
    m_playheadPos = end;
    m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
    repaint();
}

void MixLabView::TimelineComponent::nudgePlayhead(double deltaSeconds)
{
    double end = 0.0;
    for (auto& b : m_blocks)
        end = std::max(end, b->getStartTime() + b->getTrack().duration);
    m_playheadPos = juce::jlimit(0.0, end, m_playheadPos + deltaSeconds);
    m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
    repaint();
}

void MixLabView::TimelineComponent::gotoNextTransition()
{
    // Transition start = end of block N (= start of block N+1 when adjacent).
    double target = std::numeric_limits<double>::max();
    for (size_t i = 0; i + 1 < m_blocks.size(); ++i) {
        const double bEnd = m_blocks[i]->getStartTime() + m_blocks[i]->getTrack().duration;
        if (bEnd > m_playheadPos + 0.01 && bEnd < target) target = bEnd;
    }
    if (target == std::numeric_limits<double>::max()) return;
    m_playheadPos = target;
    m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
    repaint();
}

void MixLabView::TimelineComponent::gotoPrevTransition()
{
    double target = -1.0;
    for (size_t i = 0; i + 1 < m_blocks.size(); ++i) {
        const double bEnd = m_blocks[i]->getStartTime() + m_blocks[i]->getTrack().duration;
        if (bEnd < m_playheadPos - 0.01 && bEnd > target) target = bEnd;
    }
    if (target < 0.0) return;
    m_playheadPos = target;
    m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
    repaint();
}

void MixLabView::TimelineComponent::splitAtPlayhead()
{
    if (m_blocks.empty()) return;
    int idx = -1;
    for (int i = 0; i < (int) m_blocks.size(); ++i) {
        const double bs = m_blocks[i]->getStartTime();
        const double be = bs + m_blocks[i]->getTrack().duration;
        if (m_playheadPos >= bs && m_playheadPos <= be) { idx = i; break; }
    }
    if (idx < 0) {
        spdlog::info("[Timeline] Split: playhead not on any block");
        return;
    }
    auto& block = m_blocks[idx];
    const double splitOffset = m_playheadPos - block->getStartTime();
    const double aStart = block->getAudioStartSec();
    const double aEnd   = (block->getAudioEndSec() > 0.0)
                          ? block->getAudioEndSec() : block->getTrack().duration;
    const double dur = aEnd - aStart;
    if (splitOffset <= 0.1 || splitOffset >= dur - 0.1) {
        spdlog::info("[Timeline] Split: too close to edge ({}s)", splitOffset);
        return;
    }
    block->setAudioRange(aStart, aStart + splitOffset);
    auto newBlock = std::make_unique<TimelineTrackBlock>(block->getTrack());
    newBlock->setAudioRange(aStart + splitOffset, aEnd);
    newBlock->setStartTime(m_playheadPos);
    m_blocks.insert(m_blocks.begin() + idx + 1, std::move(newBlock));
    spdlog::info("[Timeline] Split block #{} at offset {}s (audio {}â†’{}, {}â†’{})",
                 idx, splitOffset, aStart, aStart + splitOffset,
                 aStart + splitOffset, aEnd);
    resized();
    repaint();
}

void MixLabView::TimelineComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF080810));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b, 4.0f, 1.0f);

    if (m_blocks.empty())
    {
        g.setFont(juce::Font("Segoe UI", 16.0f, juce::Font::plain));
        g.setColour(juce::Colour(0xFF64748B));
        g.drawText(BM_TJ("studio.dragTracksFromLib"), b, juce::Justification::centred);
    }

    if (m_rubberBanding && !m_rubberBand.isEmpty()) {
        auto rb = m_rubberBand.toFloat();
        g.setColour(juce::Colour(0xFF3B82F6).withAlpha(0.18f));
        g.fillRect(rb);
        g.setColour(juce::Colour(0xFF3B82F6));
        g.drawRect(rb, 1.5f);
    }

    for (int idx : m_multiSelection) {
        if (idx < 0 || idx >= (int) m_blocks.size()) continue;
        auto r = m_blocks[idx]->getBounds().toFloat().expanded(2.f);
        g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.9f));
        g.drawRoundedRectangle(r, 6.f, 2.f);
    }
}

void MixLabView::TimelineComponent::resized()
{
    m_viewport->setBounds(getLocalBounds().reduced(2));
    if (m_blocks.empty()) return;

    auto* content = m_viewport->getViewedComponent();
    if (!content) return;

    int blockH = juce::jmax(160, getHeight() - 8);
    float x = 8.0f;
    float pixelsPerSecond = static_cast<float>(m_zoomLevel) * 10.0f; // 30px/s @ zoom=3
    float transZoneW = juce::jmax(140.0f, 100.0f * (float)m_zoomLevel);

    for (int i = 0; i < (int)m_blocks.size(); ++i)
    {
        auto& block = m_blocks[i];
        float duration = static_cast<float>(block->getTrack().duration);
        float blockW = juce::jmax(320.0f, duration * pixelsPerSecond);

        block->setBounds((int)x, 4, (int)blockW, blockH);
        x += blockW;

        if (i < (int)m_transitions.size())
            x += transZoneW;
    }

    int contentW = juce::jmax((int)x + 20, getWidth());
    content->setSize(contentW, blockH + 8);
    content->repaint();
}

void MixLabView::TimelineComponent::paintBeatGrid(juce::Graphics& g) const
{
    if (m_zoomLevel < 1.5) return;

    const float pixelsPerSecond = static_cast<float>(m_zoomLevel) * 10.0f;

    for (auto& block : m_blocks)
    {
        const double dur = block->getTrack().duration;
        if (dur <= 0.0) continue;

        const float blockX = (float)block->getX();
        const float blockY = (float)block->getY();
        const float blockH = (float)block->getHeight();
        const float blockW = (float)block->getWidth();
        const float gridTop = blockY + 36;
        const float gridBot = blockY + blockH - 14;

        juce::Colour downbeatCol(0xFFFCD34D); // AI default
        switch (block->beatGridMode) {
            case 0: downbeatCol = juce::Colour(0xFF22C55E); break; // Manual (green)
            case 1: downbeatCol = juce::Colour(0xFF94A3B8); break; // Fixed (grey)
            case 2: downbeatCol = juce::Colour(0xFFFCD34D); break; // AI (yellow)
            case 3: downbeatCol = juce::Colour(0xFFFB923C); break; // AIFlex (orange)
            case 4: downbeatCol = juce::Colour(0xFF06B6D4); break; // Rekordbox (cyan)
        }

        const std::vector<double>* beats = nullptr;
        const std::vector<double>* bars  = nullptr;
        std::vector<double> fallbackBeats;
        if (!block->beatGridPositions.empty()) {
            beats = &block->beatGridPositions;
            if (!block->beatGridBarPositions.empty())
                bars = &block->beatGridBarPositions;
        } else if (block->getTrack().bpm > 0.0) {
            const double bpm = block->getTrack().bpm;
            const double beatDuration = 60.0 / bpm;
            for (double t = 0.0; t < dur; t += beatDuration)
                fallbackBeats.push_back(t);
            beats = &fallbackBeats;
        } else {
            continue;
        }

        auto isDownbeat = [&](size_t index, double t) -> bool {
            if (bars && !bars->empty()) {
                // O(log n) lower_bound; pre-sorted by generator.
                auto it = std::lower_bound(bars->begin(), bars->end(), t - 0.005);
                if (it != bars->end() && std::fabs(*it - t) < 0.015) return true;
                return false;
            }
            return (index % 4) == 0;
        };

        int barNum = 1;
        for (size_t i = 0; i < beats->size(); ++i) {
            const double t = (*beats)[i];
            if (t < 0.0 || t >= dur) continue;
            const float beatX = blockX + (float)(t * pixelsPerSecond);
            if (beatX > blockX + blockW) break;
            if (beatX < blockX) continue;

            const bool downbeat = isDownbeat(i, t);

            if (downbeat) {
                if (m_zoomLevel >= 5.0) {
                    g.setColour(downbeatCol.withAlpha(0.18f));
                    g.fillRect(beatX - 0.5f, gridTop, 1.0f, gridBot - gridTop);
                }
                barNum++;
            }

            if (block->beatGridMode == 0) {
                g.setColour(juce::Colour(0xFF22C55E).withAlpha(0.9f));
                g.fillEllipse(beatX - 2.5f, gridTop - 4, 5.0f, 5.0f);
            }
        }
    }
}

namespace {
    inline juce::String iconForType(MixLabView::TransitionType t) {
        using T = MixLabView::TransitionType;
        switch (t) {
            case T::Cut:         return "[-]";
            case T::Fade:        return "~";
            case T::EQBlend:     return "=";
            case T::FilterSweep: return "/";
            case T::EchoOut:     return "<<";
            case T::Backspin:    return "<-";
            case T::Slam:        return "!";
        }
        return "*";
    }

    inline juce::String typeNameOf(MixLabView::TransitionType t) {
        using T = MixLabView::TransitionType;
        switch (t) {
            case T::Cut:         return "CUT";
            case T::Fade:        return "FADE";
            case T::EQBlend:     return "EQ BLEND";
            case T::FilterSweep: return "FILTER";
            case T::EchoOut:     return "ECHO";
            case T::Backspin:    return "BACKSPIN";
            case T::Slam:        return "SLAM";
        }
        return "TRANSITION";
    }

    // Returns gain in [0..1] for role A (outgoing) at relative time t in [0..1].
    inline float presetGain(MixLabView::CurveType c, float t, bool isA) {
        using C = MixLabView::CurveType;
        const float ta = juce::jlimit(0.0f, 1.0f, t);
        switch (c) {
            case C::Linear:
                return isA ? (1.0f - ta) : ta;
            case C::EqualPower:
                return isA ? std::cos(ta * 1.5707963f) : std::sin(ta * 1.5707963f);
            case C::SCurve: {
                const float a = 0.5f * (1.0f + std::cos(ta * 3.1415927f));
                return isA ? a : (1.0f - a);
            }
            case C::ConstantPower:
                return isA ? std::sqrt(1.0f - ta) : std::sqrt(ta);
        }
        return isA ? (1.0f - ta) : ta;
    }

    // EndShape::Cut clamp width on each edge (5% of the transition).
    constexpr float kEndShapeEdge = 0.05f;

    juce::Path buildCrossfadePathFromPreset(MixLabView::CurveType curve,
                                            juce::Rectangle<float> rect,
                                            bool isA,
                                            MixLabView::EndShape leftEnd  = MixLabView::EndShape::Soft,
                                            MixLabView::EndShape rightEnd = MixLabView::EndShape::Soft)
    {
        using ES = MixLabView::EndShape;
        juce::Path p;
        const int numPts = juce::jmax(16, (int)rect.getWidth() / 3);
        for (int i = 0; i <= numPts; ++i) {
            const float t    = (float)i / (float)numPts;
            float gain = juce::jlimit(0.0f, 1.0f, presetGain(curve, t, isA));
            if (t <= kEndShapeEdge && leftEnd == ES::Cut)
                gain = isA ? 1.0f : 0.0f;
            if (t >= 1.0f - kEndShapeEdge && rightEnd == ES::Cut)
                gain = isA ? 0.0f : 1.0f;
            const float x = rect.getX() + t * rect.getWidth();
            const float y = rect.getBottom() - gain * rect.getHeight();
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        return p;
    }

    juce::Path buildCrossfadePathFromPoints(const std::vector<MixLabView::EQAutomationPoint>& pts,
                                            juce::Rectangle<float> rect,
                                            bool isA,
                                            MixLabView::EndShape leftEnd  = MixLabView::EndShape::Soft,
                                            MixLabView::EndShape rightEnd = MixLabView::EndShape::Soft)
    {
        using ES = MixLabView::EndShape;
        juce::Path p;
        if (pts.empty()) return p;
        const int numPts = juce::jmax(24, (int)rect.getWidth() / 3);

        auto sample = [&](float t) -> float {
            if ((double)t <= pts.front().time) return (float)pts.front().value;
            for (size_t k = 1; k < pts.size(); ++k) {
                if ((double)t <= pts[k].time) {
                    const auto& p0 = pts[k - 1];
                    const auto& p1 = pts[k];
                    const float span = (float)(p1.time - p0.time);
                    if (span <= 1.0e-6f) return p1.value;
                    const float u = ((float)t - (float)p0.time) / span;
                    return juce::jmap(u, 0.0f, 1.0f, p0.value, p1.value);
                }
            }
            return (float)pts.back().value;
        };

        for (int i = 0; i <= numPts; ++i) {
            const float t     = (float)i / (float)numPts;
            const float gainA = juce::jlimit(0.0f, 1.0f, sample(t));
            float gain = isA ? gainA : (1.0f - gainA);
            if (t <= kEndShapeEdge && leftEnd == ES::Cut)
                gain = isA ? 1.0f : 0.0f;
            if (t >= 1.0f - kEndShapeEdge && rightEnd == ES::Cut)
                gain = isA ? 0.0f : 1.0f;
            const float x = rect.getX() + t * rect.getWidth();
            const float y = rect.getBottom() - gain * rect.getHeight();
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        return p;
    }
} // namespace

void MixLabView::TimelineComponent::paintTransitions(juce::Graphics& g) const
{
    if (m_blocks.size() < 2) return;

    static const juce::Colour kFrameBase   (0xFF1F2937);
    static const juce::Colour kFrameAccent (0xFFF472B6); // rose magenta — sélection
    static const juce::Colour kCurveA      (0xFF38BDF8); // cyan
    static const juce::Colour kCurveB      (0xFFF472B6); // rose magenta
    static const juce::Colour kAnchor      (0xFFF59E0B); // orange Studio
    static const juce::Colour kAnchorHover (0xFFFBBF24); // orange clair (hover)
    static const juce::Colour kBgGradL     (0x1A38BDF8);
    static const juce::Colour kBgGradR     (0x1AF472B6);

    const float pixelsPerSecond = static_cast<float>(m_zoomLevel) * 10.0f;

    for (int i = 0; i + 1 < (int)m_blocks.size() && i < (int)m_transitions.size(); ++i)
    {
        auto& blockA = m_blocks[i];
        auto& blockB = m_blocks[i + 1];
        if (!blockA || !blockB) continue; // defensive: never deref a moved-from slot
        const int transX    = blockA->getRight();
        const int transEndX = blockB->getX();
        const int transW    = transEndX - transX;
        if (transW <= 0) continue;

        const auto& trans   = m_transitions[i];
        const float transY  = (float)blockA->getY();
        const float transH  = (float)blockA->getHeight();
        const bool  selected = (i == m_selectedTransition);
        const bool  hovered  = (i == m_hoverTransitionHandle);

        const juce::Rectangle<float> zone((float)transX, transY, (float)transW, transH);

        if (transW < 40)
        {
            const float cx = zone.getCentreX();
            g.setColour(kFrameAccent.withAlpha(selected ? 0.95f : 0.55f));
            g.fillRect(cx - 1.5f, transY + 6.0f, 3.0f, transH - 12.0f);
            continue;
        }

        juce::ColourGradient bgGrad(kBgGradL, zone.getX(),     0.0f,
                                    kBgGradR, zone.getRight(), 0.0f, false);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(zone, 4.0f);

        if (selected)
        {
            g.setColour(kFrameAccent.withAlpha(0.12f));
            g.drawRoundedRectangle(zone.expanded(3.5f), 6.5f, 1.0f);
            g.setColour(kFrameAccent.withAlpha(0.28f));
            g.drawRoundedRectangle(zone.expanded(1.5f), 5.0f, 1.0f);
            g.setColour(kFrameAccent);
            g.drawRoundedRectangle(zone, 4.0f, 1.8f);
        }
        else
        {
            g.setColour(kFrameBase.withAlpha(hovered ? 1.0f : 0.85f));
            g.drawRoundedRectangle(zone, 4.0f, 1.0f);
        }

        const juce::Rectangle<float> curveRect = zone.reduced(8.0f, 0.0f)
                                                     .withTrimmedTop(22.0f)
                                                     .withTrimmedBottom(8.0f);
        if (curveRect.getWidth() > 6.0f && curveRect.getHeight() > 12.0f)
        {
            const bool useCustom = !trans.crossfadePoints.empty();

            const juce::Path pathA = useCustom
                ? buildCrossfadePathFromPoints(trans.crossfadePoints, curveRect, true,
                                                trans.leftEnd, trans.rightEnd)
                : buildCrossfadePathFromPreset(trans.curve, curveRect, true,
                                                trans.leftEnd, trans.rightEnd);
            const juce::Path pathB = useCustom
                ? buildCrossfadePathFromPoints(trans.crossfadePoints, curveRect, false,
                                                trans.leftEnd, trans.rightEnd)
                : buildCrossfadePathFromPreset(trans.curve, curveRect, false,
                                                trans.leftEnd, trans.rightEnd);

            auto fillUnder = [&](juce::Path p, juce::Colour col) {
                p.lineTo(curveRect.getRight(), curveRect.getBottom());
                p.lineTo(curveRect.getX(),     curveRect.getBottom());
                p.closeSubPath();
                juce::ColourGradient grad(col.withAlpha(0.32f), curveRect.getX(), curveRect.getY(),
                                          col.withAlpha(0.04f), curveRect.getX(), curveRect.getBottom(),
                                          false);
                g.setGradientFill(grad);
                g.fillPath(p);
            };
            fillUnder(pathA, kCurveA);
            fillUnder(pathB, kCurveB);

            const juce::PathStrokeType glow(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const juce::PathStrokeType line(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour(kCurveA.withAlpha(0.30f)); g.strokePath(pathA, glow);
            g.setColour(kCurveA);                  g.strokePath(pathA, line);
            g.setColour(kCurveB.withAlpha(0.30f)); g.strokePath(pathB, glow);
            g.setColour(kCurveB);                  g.strokePath(pathB, line);

            if (useCustom) {
                for (const auto& p : trans.crossfadePoints) {
                    const float px = curveRect.getX() + (float)p.time * curveRect.getWidth();
                    const float py = curveRect.getBottom() - (float)p.value * curveRect.getHeight();
                    g.setColour(juce::Colours::white.withAlpha(0.95f));
                    g.fillEllipse(px - 4.0f, py - 4.0f, 8.0f, 8.0f);
                    g.setColour(kFrameAccent);
                    g.drawEllipse(px - 4.0f, py - 4.0f, 8.0f, 8.0f, 1.5f);
                }
            }
        }

        const int leftAnchorCode  = -(2 * i + 2);
        const int rightAnchorCode = -(2 * i + 3);
        const bool hoverLeft  = (m_hoverTransitionHandle == leftAnchorCode);
        const bool hoverRight = (m_hoverTransitionHandle == rightAnchorCode);
        const float anchorY  = transY + 8.0f;
        const float anchorH  = juce::jmax(14.0f, transH - 16.0f);
        const float anchorW  = 10.0f;

        auto drawChevron = [&](float xCenter, bool pointsLeft, bool isHover)
        {
            const juce::Colour col = isHover ? kAnchorHover : kAnchor;
            g.setColour(col.withAlpha(isHover ? 0.45f : 0.25f));
            g.fillRoundedRectangle(xCenter - anchorW - 1.0f, anchorY - 1.0f,
                                   2.0f * anchorW + 2.0f, anchorH + 2.0f, 3.0f);
            g.setColour(col);
            g.fillRoundedRectangle(xCenter - 1.0f, anchorY, 2.0f, anchorH, 1.0f);
            juce::Path tri;
            const float midY = anchorY + anchorH * 0.5f;
            const float h2   = 5.0f;
            if (pointsLeft) {
                tri.addTriangle(xCenter - anchorW, midY,
                                xCenter,            midY - h2,
                                xCenter,            midY + h2);
            } else {
                tri.addTriangle(xCenter + anchorW, midY,
                                xCenter,            midY - h2,
                                xCenter,            midY + h2);
            }
            g.setColour(col);
            g.fillPath(tri);
            juce::Path tri2;
            if (pointsLeft) {
                tri2.addTriangle(xCenter + anchorW, midY,
                                 xCenter,             midY - h2,
                                 xCenter,             midY + h2);
            } else {
                tri2.addTriangle(xCenter - anchorW, midY,
                                 xCenter,             midY - h2,
                                 xCenter,             midY + h2);
            }
            g.setColour(col.withAlpha(0.55f));
            g.fillPath(tri2);
        };

        drawChevron(zone.getX(),     true,  hoverLeft);
        drawChevron(zone.getRight(), false, hoverRight);

        if (m_playheadPos > 0.0)
        {
            const float playheadX = 8.0f + (float)(m_playheadPos * pixelsPerSecond);
            if (playheadX > zone.getX() && playheadX <= zone.getRight())
            {
                g.setColour(kFrameAccent.withAlpha(0.18f));
                g.fillRect(zone.getX(), zone.getY(),
                           playheadX - zone.getX(), zone.getHeight());
            }
        }
    }

    if (m_playheadPos > 0 && !m_blocks.empty() && m_blocks[0])
    {
        const float phX  = 8.0f + (float)(m_playheadPos * pixelsPerSecond);
        const float topY = (float)m_blocks[0]->getY();
        const float botY = topY + (float)m_blocks[0]->getHeight();
        const juce::Colour ph(0xFFEF4444);

        g.setColour(ph.withAlpha(0.18f));
        g.fillRect(phX - 3.0f, topY, 6.0f, botY - topY);
        g.setColour(ph.withAlpha(0.40f));
        g.fillRect(phX - 1.5f, topY, 3.0f, botY - topY);
        g.setColour(ph);
        g.fillRect(phX - 0.6f, topY, 1.2f, botY - topY);

        juce::Path tri;
        tri.addTriangle(phX - 6.0f, topY, phX + 6.0f, topY, phX, topY + 9.0f);
        g.setColour(ph);
        g.fillPath(tri);
    }
}

void MixLabView::TimelineComponent::setSelectedBlock(int index)
{
    m_selectedBlock = (index >= 0 && index < static_cast<int>(m_blocks.size())) ? index : -1;
    m_selectedTransition = -1;
    for (int i = 0; i < static_cast<int>(m_blocks.size()); ++i) {
        if (!m_blocks[i]) continue;
        m_blocks[i]->getProperties().set("selected", (i == m_selectedBlock) ? "1" : "0");
        // Each block must repaint itself — repainting only the content
        m_blocks[i]->repaint();
    }
    if (auto* content = m_viewport ? m_viewport->getViewedComponent() : nullptr)
        content->repaint();
    else
        repaint();
}

void MixLabView::TimelineComponent::handleContentClick(const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    for (int i = 0; i + 1 < (int)m_blocks.size() && i < (int)m_transitions.size(); ++i)
    {
        int transX = m_blocks[i]->getRight();
        int transW = m_blocks[i + 1]->getX() - transX;
        if (transW > 0 && pos.x >= transX && pos.x < transX + transW)
        {
            m_selectedTransition = i;
            m_selectedBlock = -1;
            for (auto& bl : m_blocks) bl->getProperties().set("selected", "0");
            m_listeners.call([i](Listener& l) { l.transitionClicked(i); });
            if (auto* content = m_viewport->getViewedComponent()) content->repaint();
            return;
        }
    }

    if (!m_blocks.empty() && pos.x > 8)
    {
        float pixelsPerSecond = static_cast<float>(m_zoomLevel) * 10.0f;
        double clickTimeSec = (pos.x - 8.0) / pixelsPerSecond;
        m_playheadPos = juce::jmax(0.0, clickTimeSec);
        m_listeners.call([this](Listener& l) { l.playheadMoved(m_playheadPos); });
        if (auto* content = m_viewport->getViewedComponent()) content->repaint();
    }
}

void MixLabView::TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    bool onBlock = false;
    for (auto& blk : m_blocks) {
        if (blk && blk->getBounds().contains(e.getPosition())) { onBlock = true; break; }
    }
    if (!onBlock) {
        if (!e.mods.isCtrlDown() && !e.mods.isShiftDown())
            m_multiSelection.clear();
        m_rubberBanding = true;
        m_rubberBand = juce::Rectangle<int>(e.getPosition(), e.getPosition());
        repaint();
        return;
    }
    handleContentClick(e);
}

void MixLabView::TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!m_rubberBanding) return;
    m_rubberBand = juce::Rectangle<int>(e.getMouseDownPosition(), e.getPosition());
    for (int i = 0; i < (int) m_blocks.size(); ++i) {
        auto& blk = m_blocks[i];
        if (!blk) continue;
        const bool hit = blk->getBounds().intersects(m_rubberBand);
        const bool alreadyIn = isInMultiSelection(i);
        if (hit && !alreadyIn)           m_multiSelection.push_back(i);
        else if (!hit && alreadyIn) {
            auto it = std::find(m_multiSelection.begin(), m_multiSelection.end(), i);
            if (it != m_multiSelection.end()) m_multiSelection.erase(it);
        }
    }
    repaint();
}

void MixLabView::TimelineComponent::mouseUp(const juce::MouseEvent&)
{
    if (m_rubberBanding) {
        m_rubberBanding = false;
        m_rubberBand = {};
        repaint();
    }
}

void MixLabView::TimelineComponent::mouseWheelMove(const juce::MouseEvent& e,
                                                       const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        m_zoomLevel = juce::jlimit(0.1, 20.0, m_zoomLevel + wheel.deltaY * 0.5);
        resized();
    }
    else
    {
        if (m_viewport)
        {
            int curX = m_viewport->getViewPositionX();
            m_viewport->setViewPosition(curX - (int)(wheel.deltaY * 60), 0);
        }
    }
}

MixLabView::TransportBar::TransportBar()
{
    auto makeBtn = [this](const juce::String& label) {
        auto b = std::make_unique<juce::TextButton>(label);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1A1A24));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFE2E8F0));
        addAndMakeVisible(b.get());
        return b;
    };

    m_playBtn = makeBtn(juce::CharPointer_UTF8("\xe2\x96\xb6"));
    m_replayTransBtn = makeBtn("RPL");
    m_prevTransBtn = makeBtn(juce::CharPointer_UTF8("\xe2\x97\x80\xe2\x97\x80"));
    m_nextTransBtn = makeBtn(juce::CharPointer_UTF8("\xe2\x96\xb6\xe2\x96\xb6"));
    m_soloBtn = makeBtn("SOLO");
    m_metronomeBtn = makeBtn("METR");
    m_addTracksBtn = makeBtn("+ Tracks");
    m_harmonizeBtn = makeBtn("Harmonize");
    m_mashupAIBtn  = makeBtn("Mashup IA");
    m_megamixAIBtn = makeBtn("Megamix IA");
    m_medleyAIBtn  = makeBtn("Medley IA");
    m_undoBtn = makeBtn("Undo");
    m_redoBtn = makeBtn("Redo");
    m_splitBtn = makeBtn("Split");
    m_centerPlayheadBtn = makeBtn("Center");
    m_zoomInBtn = makeBtn("Zoom+");
    m_zoomOutBtn = makeBtn("Zoom-");
    m_liveBtn = makeBtn("LIVE");
    m_liveBtn->setClickingTogglesState(true);
    m_liveBtn->setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF1A1A24));
    m_liveBtn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFEF4444));
    m_liveBtn->setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
    m_liveBtn->setColour(juce::TextButton::textColourOffId,  juce::Colour(0xFFE2E8F0));
    m_liveBtn->setTooltip("LIVE : edits affect playback immediately (move, lane mute/solo/vol, "
                          "automation, transitions). OFF : bounce-then-play (default).");
    m_liveBtn->onClick = [this]() { if (onToggleLive) onToggleLive(); };

    m_playBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_playBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    m_playBtn->onClick = [this]() { if (onPlay) onPlay(); };
    m_replayTransBtn->onClick = [this]() { if (onReplayTrans) onReplayTrans(); };
    m_prevTransBtn->onClick = [this]() { if (onPrevTrans) onPrevTrans(); };
    m_nextTransBtn->onClick = [this]() { if (onNextTrans) onNextTrans(); };
    m_soloBtn->onClick = [this]() { if (onSolo) onSolo(); };
    m_metronomeBtn->onClick = [this]() { if (onMetronome) onMetronome(); };
    m_addTracksBtn->onClick = [this]() { if (onAddTracks) onAddTracks(); };
    m_harmonizeBtn->onClick = [this]() { if (onHarmonize) onHarmonize(); };
    m_mashupAIBtn ->onClick = [this]() { if (onMashupAI)  onMashupAI();  };
    m_megamixAIBtn->onClick = [this]() { if (onMegamixAI) onMegamixAI(); };
    m_medleyAIBtn ->onClick = [this]() { if (onMedleyAI)  onMedleyAI();  };

    m_mashupAIBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF0EA5E9));
    m_megamixAIBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFA855F7));
    m_medleyAIBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFEC4899));
    m_mashupAIBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_megamixAIBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_medleyAIBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_undoBtn->onClick = [this]() { if (onUndo) onUndo(); };
    m_redoBtn->onClick = [this]() { if (onRedo) onRedo(); };
    m_splitBtn->onClick = [this]() { if (onSplit) onSplit(); };
    m_centerPlayheadBtn->onClick = [this]() { if (onCenterPlayhead) onCenterPlayhead(); };
    m_zoomInBtn->onClick = [this]() { if (onZoomIn) onZoomIn(); };
    m_zoomOutBtn->onClick = [this]() { if (onZoomOut) onZoomOut(); };
}

void MixLabView::TransportBar::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xFF0A0A12));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 4.0f, 1.0f);
}

void MixLabView::TransportBar::resized()
{
    int x = 6, y = 4, h = 24, gap = 3;
    auto place = [&](juce::TextButton* btn, int w) {
        btn->setBounds(x, y, w, h);
        x += w + gap;
    };
    place(m_prevTransBtn.get(), 32);
    place(m_playBtn.get(), 38);
    place(m_nextTransBtn.get(), 32);
    place(m_replayTransBtn.get(), 32);
    x += 8;
    place(m_soloBtn.get(), 40);
    place(m_metronomeBtn.get(), 40);
    x += 8;
    place(m_addTracksBtn.get(), 70);
    place(m_harmonizeBtn.get(), 76);
    place(m_mashupAIBtn.get(),  72);
    place(m_megamixAIBtn.get(), 80);
    place(m_medleyAIBtn.get(),  72);
    x += 8;
    place(m_undoBtn.get(), 44);
    place(m_redoBtn.get(), 44);
    place(m_splitBtn.get(), 40);
    x += 8;
    place(m_centerPlayheadBtn.get(), 56);
    place(m_zoomInBtn.get(), 50);
    place(m_zoomOutBtn.get(), 50);
    x += 8;
    place(m_liveBtn.get(), 48);
}

MixLabView::StudioTabs::StudioTabs() : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {}

void MixLabView::StudioTabs::currentTabChanged(int newIdx, const juce::String&)
{
    if (onTabChanged) onTabChanged((StudioTab)newIdx);
}

MixLabView::TrackInfoPanel::TrackInfoPanel()
{
    auto makeEditor = [this](const juce::String& placeholder) {
        auto e = std::make_unique<juce::TextEditor>();
        e->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF1A1A24));
        e->setColour(juce::TextEditor::textColourId, juce::Colours::white);
        e->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF2A2A38));
        e->setTextToShowWhenEmpty(placeholder, juce::Colour(0xFF64748B));
        addAndMakeVisible(e.get());
        return e;
    };
    m_titleEdit = makeEditor(BM_TJ("studio.title"));
    m_artistEdit = makeEditor(BM_TJ("studio.artist"));
    m_genreEdit = makeEditor(BM_TJ("studio.genre"));
    m_notesEdit = makeEditor(BM_TJ("studio.notes"));

    m_pitchModeCombo = std::make_unique<juce::ComboBox>("pitchMode");
    m_pitchModeCombo->addItem(BM_TJ("studio.settings.pitchMode.vinyl"), 1);
    m_pitchModeCombo->addItem(BM_TJ("studio.settings.pitchMode.repitch"), 2);
    m_pitchModeCombo->addItem(BM_TJ("studio.settings.pitchMode.beatSlice"), 3);
    m_pitchModeCombo->addItem(BM_TJ("studio.settings.pitchMode.formant"), 4);
    m_pitchModeCombo->setSelectedId(1, juce::dontSendNotification);
    m_pitchModeCombo->setTooltip(BM_TJ("studio.settings.pitchModeTip"));
    m_pitchModeCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A24));
    m_pitchModeCombo->setColour(juce::ComboBox::textColourId, juce::Colours::white);
    addAndMakeVisible(*m_pitchModeCombo);

    m_beatGridModeCombo = std::make_unique<juce::ComboBox>("beatGrid");
    m_beatGridModeCombo->addItem(BM_TJ("studio.beatgrid.manual"), 1);
    m_beatGridModeCombo->addItem(BM_TJ("studio.beatgrid.fixed"), 2);
    m_beatGridModeCombo->addItem(BM_TJ("studio.beatgrid.ai"), 3);
    m_beatGridModeCombo->addItem(BM_TJ("studio.beatgrid.aiFlex"), 4);
    m_beatGridModeCombo->addItem(BM_TJ("studio.beatgrid.rekordbox"), 5);
    m_beatGridModeCombo->setSelectedId(3, juce::dontSendNotification);
    m_beatGridModeCombo->setTooltip(BM_TJ("studio.settings.beatgridTip"));
    m_beatGridModeCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A24));
    m_beatGridModeCombo->setColour(juce::ComboBox::textColourId, juce::Colours::white);
    addAndMakeVisible(*m_beatGridModeCombo);

    m_stabilizeTempoCheck = std::make_unique<juce::ToggleButton>(BM_TJ("studio.settings.stabilizeTempo"));
    m_stabilizeTempoCheck->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    m_stabilizeTempoCheck->setTooltip(BM_TJ("studio.settings.stabilizeTempoTip"));
    // When ON, the track is played at a locked target BPM (the current
    m_stabilizeTempoCheck->onClick = [this] {
        const bool on = m_stabilizeTempoCheck->getToggleState();
        if (::g_serviceLocator) {
            if (auto* sm = ::g_serviceLocator->tryGet<Services::Config::SettingsManager>()) {
                sm->set<bool>("studio.stabilizeTempo", on);
            }
        }
        spdlog::info("[MixLab] StabilizeTempo={}", on);
    };
    addAndMakeVisible(*m_stabilizeTempoCheck);

    m_ratingSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_ratingSlider->setRange(0, 5, 1);
    m_ratingSlider->setColour(juce::Slider::trackColourId, juce::Colour(0xFFF59E0B));
    addAndMakeVisible(*m_ratingSlider);

    static const char* stemLabels[4] = { "VOCALS", "DRUMS", "BASS", "OTHER" };
    const juce::Colour stemCols[4] = {
        juce::Colour(0xFFEC4899),
        juce::Colour(0xFF10B981),
        juce::Colour(0xFFF59E0B),
        juce::Colour(0xFF8B5CF6)
    };
    for (int i = 0; i < 4; ++i) {
        m_stemFaders[i] = std::make_unique<juce::Slider>(
            juce::Slider::LinearVertical, juce::Slider::NoTextBox);
        m_stemFaders[i]->setRange(0.0, 1.5, 0.01);
        m_stemFaders[i]->setValue(1.0, juce::dontSendNotification);
        m_stemFaders[i]->setColour(juce::Slider::trackColourId, stemCols[i]);
        m_stemFaders[i]->setColour(juce::Slider::thumbColourId, stemCols[i].brighter(0.2f));
        m_stemFaders[i]->onValueChange = [this, i] {
            if (onStemVolume) onStemVolume(i, static_cast<float>(m_stemFaders[i]->getValue()));
        };
        addAndMakeVisible(*m_stemFaders[i]);

        m_stemMute[i] = std::make_unique<juce::ToggleButton>("M");
        m_stemMute[i]->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        m_stemMute[i]->setTooltip(BM_TJ("studio.mute") + " " + stemLabels[i]);
        m_stemMute[i]->onClick = [this, i] {
            if (onStemMute) onStemMute(i, m_stemMute[i]->getToggleState());
        };
        addAndMakeVisible(*m_stemMute[i]);

        m_stemSolo[i] = std::make_unique<juce::ToggleButton>("S");
        m_stemSolo[i]->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        m_stemSolo[i]->setTooltip(BM_TJ("studio.solo") + " " + stemLabels[i]);
        m_stemSolo[i]->onClick = [this, i] {
            if (onStemSolo) onStemSolo(i, m_stemSolo[i]->getToggleState());
        };
        addAndMakeVisible(*m_stemSolo[i]);
    }
}

void MixLabView::TrackInfoPanel::loadTrack(const Models::Track& track)
{
    m_track = track;
    m_titleEdit->setText(juce::String(track.title));
    m_artistEdit->setText(juce::String(track.artist));
    m_genreEdit->setText(juce::String(track.genre));
    m_notesEdit->setText(juce::String(track.comment));
    m_ratingSlider->setValue(track.rating);
}

void MixLabView::TrackInfoPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF080810));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF64748B));
    g.drawText(BM_TJ("studio.titleLbl"),   8, 8, 60, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.artistLbl"), 8, 32, 60, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.genreLbl"),   8, 56, 60, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.notesLbl"),   8, 80, 60, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.pitchMode"),     8, 110, 80, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.beatGridMode"), 8, 134, 100, 18, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.rating"),   8, 182, 60, 18, juce::Justification::centredLeft);
}

void MixLabView::TrackInfoPanel::resized()
{
    int w = getWidth();
    m_titleEdit->setBounds(70, 8, w - 78, 20);
    m_artistEdit->setBounds(70, 32, w - 78, 20);
    m_genreEdit->setBounds(70, 56, w - 78, 20);
    m_notesEdit->setBounds(70, 80, w - 78, 20);
    m_pitchModeCombo->setBounds(100, 110, 200, 20);
    m_beatGridModeCombo->setBounds(120, 134, 200, 20);
    m_stabilizeTempoCheck->setBounds(8, 158, 200, 20);
    m_ratingSlider->setBounds(70, 182, w - 78, 20);

    const int stemRowY = 210;
    const int stemRowH = juce::jmax(80, getHeight() - stemRowY - 8);
    const int stripW = juce::jmin(80, (w - 16) / 4);
    for (int i = 0; i < 4; ++i) {
        const int sx = 8 + i * (stripW + 4);
        m_stemMute[i]->setBounds(sx,                    stemRowY,          stripW / 2, 16);
        m_stemSolo[i]->setBounds(sx + stripW / 2,       stemRowY,          stripW / 2, 16);
        m_stemFaders[i]->setBounds(sx + stripW / 2 - 16, stemRowY + 20,    32,         stemRowH - 24);
    }
}

MixLabView::EffectsPanel::EffectsPanel()
{
    auto makeKnob = [this]() {
        auto k = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        k->setRange(0.0, 1.0, 0.01);
        k->setValue(0.5);
        k->setColour(juce::Slider::rotarySliderFillColourId, Colors::accent());
        k->setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        k->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF1A1A24));
        addAndMakeVisible(k.get());
        return k;
    };

    m_gainKnob = makeKnob();
    m_compKnob = makeKnob();
    m_eqHi = makeKnob();
    m_eqMid = makeKnob();
    m_eqLo = makeKnob();
    m_pitchSlider = makeKnob();
    m_hpfKnob = makeKnob();
    m_lpfKnob = makeKnob();
    m_qKnob = makeKnob();
    m_echoKnob = makeKnob();
    m_reverbKnob = makeKnob();

    auto bindEq = [](juce::Slider* s, std::function<void(float)> applyDb) {
        if (!s) return;
        s->onValueChange = [s, applyDb]() {
            // 0..1 â†’ -12..+12 dB centered at 0.5
            const float db = (static_cast<float>(s->getValue()) - 0.5f) * 24.0f;
            applyDb(db);
        };
    };
    bindEq(m_eqHi.get(), [](float db) {
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterEqHigh(db);
        }
    });
    bindEq(m_eqMid.get(), [](float db) {
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterEqMid(db);
        }
    });
    bindEq(m_eqLo.get(), [](float db) {
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterEqLow(db);
        }
    });

    // Master compressor knob (0..1 â†’ 0..10 amount)
    m_compKnob->onValueChange = [this]() {
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterCompressorAmount(static_cast<float>(m_compKnob->getValue()) * 10.0f);
        }
    };

    // HPF: 0 (knob full left) = bypass, knob 1 = 1000 Hz, log-ish via squaring.
    m_hpfKnob->onValueChange = [this]() {
        const float k = static_cast<float>(m_hpfKnob->getValue());
        const float hz = (k <= 0.001f) ? 0.0f : (20.0f + k * k * 980.0f); // 20..1000 Hz
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterHpfFrequency(hz);
        }
    };
    // LPF: knob 1 = bypass (22 kHz), knob ~0 = 200 Hz.
    m_lpfKnob->onValueChange = [this]() {
        const float k = static_cast<float>(m_lpfKnob->getValue());
        const float hz = 200.0f + std::pow(k, 0.4f) * 21800.0f;
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterLpfFrequency(hz);
        }
    };
    // Master gain knob (0..1 â†’ 0..2 = -âˆž … +6 dB)
    m_gainKnob->onValueChange = [this]() {
        const float v = static_cast<float>(m_gainKnob->getValue()) * 2.0f;
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterVolume(v);
        }
    };

    // Q knob: 0..1 â†’ 0.7..6.0 (resonance for HPF/LPF)
    m_qKnob->onValueChange = [this]() {
        const float k = static_cast<float>(m_qKnob->getValue());
        const float q = 0.7f + k * 5.3f;
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterFilterQ(q);
        }
    };

    // Echo knob: 0..1 â†’ wet/dry mix â†’ AudioEngine master delay
    m_echoKnob->onValueChange = [this]() {
        const float a = static_cast<float>(m_echoKnob->getValue());
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterEchoAmount(a);
        }
    };

    // Reverb knob: 0..1 â†’ wet level â†’ AudioEngine master reverb
    m_reverbKnob->onValueChange = [this]() {
        const float a = static_cast<float>(m_reverbKnob->getValue());
        if (g_serviceLocator) {
            if (auto* e = g_serviceLocator->tryGet<Core::AudioEngine>())
                e->setMasterReverbAmount(a);
        }
    };

    m_fxPackCombo = std::make_unique<juce::ComboBox>("fxPack");
    m_fxPackCombo->addItem(BM_TJ("studio.settings.fxPack.none"), 1);
    m_fxPackCombo->addItem(BM_TJ("studio.settings.fxPack.club"), 2);
    m_fxPackCombo->addItem(BM_TJ("studio.settings.fxPack.festival"), 3);
    m_fxPackCombo->addItem(BM_TJ("studio.settings.fxPack.lounge"), 4);
    m_fxPackCombo->setSelectedId(Prefs::getInt("studio.fxPack", 1), juce::dontSendNotification);
    m_fxPackCombo->setTooltip(BM_TJ("studio.settings.fxPackTip"));
    m_fxPackCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A24));
    m_fxPackCombo->onChange = [this] {
        const int id = m_fxPackCombo->getSelectedId();
        // Preset values: [gain, comp, eqHi, eqMid, eqLo, pitch, hpf, lpf, q, echo, reverb]
        struct FXPreset { float gain, comp, hi, mid, lo, pitch, hpf, lpf, q, echo, reverb; };
        static const FXPreset presets[] = {
            { 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f }, // None (flat)
            { 0.6f, 0.55f, 0.6f, 0.5f, 0.65f, 0.0f, 0.15f, 0.85f, 0.5f, 0.25f, 0.15f }, // Club
            { 0.7f, 0.7f, 0.65f, 0.55f, 0.75f, 0.0f, 0.2f, 0.9f, 0.55f, 0.35f, 0.3f }, // Festival
            { 0.45f, 0.3f, 0.45f, 0.5f, 0.55f, 0.0f, 0.0f, 0.75f, 0.4f, 0.5f, 0.55f }  // Lounge
        };
        const int pIdx = juce::jlimit(1, 4, id) - 1;
        const auto& p = presets[pIdx];
        auto setK = [](juce::Slider* s, double v) {
            if (s) s->setValue(v, juce::sendNotificationSync);
        };
        setK(m_gainKnob.get(),    p.gain);
        setK(m_compKnob.get(),    p.comp);
        setK(m_eqHi.get(),        p.hi);
        setK(m_eqMid.get(),       p.mid);
        setK(m_eqLo.get(),        p.lo);
        setK(m_pitchSlider.get(), p.pitch);
        setK(m_hpfKnob.get(),     p.hpf);
        setK(m_lpfKnob.get(),     p.lpf);
        setK(m_qKnob.get(),       p.q);
        setK(m_echoKnob.get(),    p.echo);
        setK(m_reverbKnob.get(),  p.reverb);
        if (::g_serviceLocator) {
            if (auto* sm = ::g_serviceLocator->tryGet<Services::Config::SettingsManager>()) {
                sm->set<int>("studio.fxPack", id);
            }
        }
        spdlog::info("[MixLab] FX Pack preset #{} applied", id);
    };
    addAndMakeVisible(*m_fxPackCombo);
}

void MixLabView::EffectsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF080810));
    g.setFont(juce::Font(8.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFF64748B));
    static const char* labels[] = { "GAIN", "COMP", "HI", "MID", "LO", "PITCH", "HPF", "LPF", "Q", "ECHO", "REVERB" };
    int x = 8;
    for (int i = 0; i < 11; ++i)
    {
        g.drawText(labels[i], x, 64, 50, 12, juce::Justification::centred);
        x += 60;
    }
    g.drawText("FX PACK:", 8, 90, 60, 18, juce::Justification::centredLeft);
}

void MixLabView::EffectsPanel::resized()
{
    int x = 8, y = 8, sz = 50;
    juce::Slider* knobs[] = { m_gainKnob.get(), m_compKnob.get(), m_eqHi.get(), m_eqMid.get(),
                              m_eqLo.get(), m_pitchSlider.get(), m_hpfKnob.get(), m_lpfKnob.get(),
                              m_qKnob.get(), m_echoKnob.get(), m_reverbKnob.get() };
    for (auto* k : knobs)
    {
        k->setBounds(x, y, sz, sz + 16);
        x += 60;
    }
    m_fxPackCombo->setBounds(70, 90, 200, 20);
}

MixLabView::MasterPanel::MasterPanel()
{
    m_masterGainSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_masterGainSlider->setRange(0.0, 2.0, 0.001);   // 0 = mute, 1 = unity, 2 = +6 dB
    m_masterGainSlider->setValue(1.0, juce::dontSendNotification);
    m_masterGainSlider->setSkewFactorFromMidPoint(1.0); // log-ish around unity
    m_masterGainSlider->setTextValueSuffix("");
    m_masterGainSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_masterGainSlider->onValueChange = [this]() {
        const float v = static_cast<float>(m_masterGainSlider->getValue());
        if (g_serviceLocator) {
            if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>()) {
                engine->setMasterVolume(v);
                spdlog::info("[MasterPanel] master volume â†’ {:.3f} ({:.1f} dB)",
                             v, 20.0f * std::log10(std::max(0.0001f, v)));
            }
        }
    };
    if (g_serviceLocator) {
        if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>()) {
            m_masterGainSlider->setValue(engine->getMasterVolume(), juce::dontSendNotification);
        }
    }
    addAndMakeVisible(*m_masterGainSlider);

    m_limiterSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_limiterSlider->setRange(-12, 0, 0.1);
    m_limiterSlider->setValue(-3);
    m_limiterSlider->setColour(juce::Slider::trackColourId, Colors::error());
    m_limiterSlider->onValueChange = [this]() {
        const float thrDb = static_cast<float>(m_limiterSlider->getValue());
        // Map limiter threshold (-12..0 dB) â†’ master compressor amount (10..0)
        const float amount = juce::jlimit(0.0f, 10.0f, -thrDb / 1.2f);
        if (g_serviceLocator) {
            if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>())
                engine->setMasterCompressorAmount(amount);
        }
        spdlog::info("[MasterPanel] limiter threshold {:.1f} dB (amount={:.1f})", thrDb, amount);
    };
    addAndMakeVisible(*m_limiterSlider);

    m_compressorSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_compressorSlider->setRange(0, 10, 0.1);
    m_compressorSlider->setValue(0.0, juce::dontSendNotification); // bypassed by default
    m_compressorSlider->setColour(juce::Slider::trackColourId, Colors::warning());
    m_compressorSlider->onValueChange = [this]() {
        const float a = static_cast<float>(m_compressorSlider->getValue());
        if (g_serviceLocator) {
            if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>()) {
                engine->setMasterCompressorAmount(a);
                spdlog::info("[MasterPanel] master compressor amount â†’ {:.1f}/10 (gain reduction reading: {:.2f} dB)",
                             a, engine->getMasterCompressorGainReduction());
            }
        }
    };
    if (g_serviceLocator) {
        if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>()) {
            m_compressorSlider->setValue(engine->getMasterCompressorAmount(), juce::dontSendNotification);
        }
    }
    addAndMakeVisible(*m_compressorSlider);

    m_peakMeter = std::make_unique<PeakMeterDual>();
    addAndMakeVisible(*m_peakMeter);
    m_grMeter   = std::make_unique<GainReductionMeter>();
    addAndMakeVisible(*m_grMeter);

    startTimerHz(30);
}

MixLabView::MasterPanel::~MasterPanel()
{
    stopTimer();
}

void MixLabView::MasterPanel::setStudioTimeline(Studio::StudioTimeline* tl) noexcept
{
    m_studioTimeline = tl;
}

void MixLabView::MasterPanel::timerCallback()
{
    if (!m_studioTimeline) return;
    if (m_peakMeter) {
        m_peakMeter->setLevels(m_studioTimeline->getMasterPeakL(),
                               m_studioTimeline->getMasterPeakR());
    }
    if (m_grMeter) {
        // CompressorProcessor::getGainReduction() returns dB of reduction
        const float gr = m_studioTimeline->getMasterCompressorGainReduction();
        m_grMeter->setGainReductionDb(juce::jlimit(0.0f, 12.0f, gr));
    }
}

void MixLabView::MasterPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF080810));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFFE2E8F0));
    g.drawText("Master",                     8,  12, 100, 20, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.limiter"),      8,  38, 100, 20, juce::Justification::centredLeft);
    g.drawText(BM_TJ("studio.compressor"),   8,  64, 130, 20, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xFF1A1A24));
    g.fillRect(8, 96, getWidth() - 16, 80);
    g.setColour(Colors::primary());
    g.setFont(juce::Font(9.0f));
    g.drawText(BM_TJ("studio.freqSpectrum"), 16, 102, 200, 14, juce::Justification::centredLeft);
}

void MixLabView::MasterPanel::resized()
{
    const int w = getWidth();

    constexpr int kMeterZoneW = 80;
    constexpr int kMeterZoneH = 100;
    constexpr int kMargin     = 8;
    const int meterZoneX = std::max(0, w - kMeterZoneW - kMargin);
    const int meterZoneY = 8;

    const int sliderRight = meterZoneX - 8;
    const int sliderW     = std::max(80, sliderRight - 140);
    m_masterGainSlider->setBounds(140, 12, sliderW, 20);
    m_limiterSlider   ->setBounds(140, 38, sliderW, 20);
    m_compressorSlider->setBounds(140, 64, sliderW, 20);

    if (m_peakMeter && m_grMeter) {
        constexpr int kPeakW = 8 + 2 + 8;   // 18 (matches PeakMeterDual layout)
        constexpr int kGap   = 6;
        constexpr int kGrW   = 6;
        const int totalW = kPeakW + kGap + kGrW;
        int x = meterZoneX + (kMeterZoneW - totalW) / 2;
        m_peakMeter->setBounds(x, meterZoneY, kPeakW, kMeterZoneH);
        x += kPeakW + kGap;
        m_grMeter  ->setBounds(x, meterZoneY, kGrW, kMeterZoneH);
    }
}

// CrossfadeCurveEditor + TransitionEditorComponent ont été retirés.
#if 0
void MixLabView::CrossfadeCurveEditor::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF080810));
    g.fillRoundedRectangle(b, 3.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(b, 3.0f, 1.0f);

    if (!m_points || m_points->empty()) {
        // Default fade-in/out hint line.
        g.setColour(juce::Colour(0xFF334155));
        g.drawLine(b.getX() + 4, b.getBottom() - 4,
                   b.getRight() - 4, b.getY() + 12, 1.0f);
        return;
    }

    // Sorted copy for drawing the polyline (don't mutate user data here).
    auto pts = *m_points;
    std::sort(pts.begin(), pts.end(),
              [](const EQAutomationPoint& a, const EQAutomationPoint& b) {
                  return a.time < b.time;
              });

    juce::Path path;
    bool first = true;
    for (const auto& p : pts) {
        const auto px = pointToPx(p.time, p.value);
        if (first) { path.startNewSubPath(px.x, px.y); first = false; }
        else        path.lineTo(px.x, px.y);
    }
    g.setColour(juce::Colour(0xFF6C63FF));
    g.strokePath(path, juce::PathStrokeType(1.5f));

    // Handles.
    for (const auto& p : pts) {
        const auto px = pointToPx(p.time, p.value);
        g.setColour(juce::Colour(0xFFFB923C));
        g.fillEllipse(px.x - 3.5f, px.y - 3.5f, 7.0f, 7.0f);
        g.setColour(juce::Colours::white);
        g.drawEllipse(px.x - 3.5f, px.y - 3.5f, 7.0f, 7.0f, 1.0f);
    }
}

void MixLabView::CrossfadeCurveEditor::mouseDown(const juce::MouseEvent& e) {
    if (!m_points) return;

    if (e.mods.isPopupMenu()) {
        const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
        if (idx >= 0) {
            m_points->erase(m_points->begin() + idx);
            if (onChanged) onChanged();
            repaint();
        }
        return;
    }

    const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
    if (idx >= 0) {
        m_dragged = idx;
        return;
    }

    // Empty area â†’ add a new point and start dragging it.
    double t = 0.0; float g = 0.0f;
    if (pxToPoint(e.getPosition().x, e.getPosition().y, t, g)) {
        m_points->push_back({ t, g });
        m_dragged = static_cast<int>(m_points->size()) - 1;
        if (onChanged) onChanged();
        repaint();
    }
}

void MixLabView::CrossfadeCurveEditor::mouseDrag(const juce::MouseEvent& e) {
    if (!m_points || m_dragged < 0 || m_dragged >= (int) m_points->size()) return;
    double t = 0.0; float g = 0.0f;
    if (!pxToPoint(e.getPosition().x, e.getPosition().y, t, g)) return;
    (*m_points)[m_dragged].time  = t;
    (*m_points)[m_dragged].value = g;
    if (onChanged) onChanged();
    repaint();
}

void MixLabView::CrossfadeCurveEditor::mouseUp(const juce::MouseEvent&) {
    if (!m_points) { m_dragged = -1; return; }
    if (m_dragged >= 0) {
        // Re-sort by time (preserve relative point order if user crossed peers).
        std::sort(m_points->begin(), m_points->end(),
                  [](const EQAutomationPoint& a, const EQAutomationPoint& b) {
                      return a.time < b.time;
                  });
        m_dragged = -1;
        if (onChanged) onChanged();
        repaint();
    }
}

void MixLabView::CrossfadeCurveEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!m_points) return;
    const int idx = hitPoint(e.getPosition().x, e.getPosition().y);
    if (idx >= 0) {
        m_points->erase(m_points->begin() + idx);
        if (onChanged) onChanged();
        repaint();
    }
}

// TransitionEditorComponent
MixLabView::TransitionEditorComponent::TransitionEditorComponent()
{
    m_typeCombo = std::make_unique<juce::ComboBox>("type");
    const juce::String types[] = { BM_TJ("studio.trans.cut"), BM_TJ("studio.trans.fade"), BM_TJ("studio.trans.eqBlend"), BM_TJ("studio.trans.filterSweep"), BM_TJ("studio.trans.echoOut"), BM_TJ("studio.trans.backspin"), BM_TJ("studio.trans.slam") };
    for (int i = 0; i < 7; ++i) m_typeCombo->addItem(types[i], i + 1);
    m_typeCombo->setSelectedId(3, juce::dontSendNotification);
    m_typeCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A24));
    m_typeCombo->setColour(juce::ComboBox::textColourId, juce::Colours::white);
    // Type combo: writes back into the currently-loaded transition so the
    m_typeCombo->onChange = [this] {
        if (!m_currentTransition) return;
        m_currentTransition->type = (TransitionType)
            (m_typeCombo->getSelectedId() - 1);
        if (auto* parent = getParentComponent()) parent->repaint();
        spdlog::info("[MixLab] Transition type -> {}",
                     m_typeCombo->getText().toStdString());
    };
    addAndMakeVisible(*m_typeCombo);

    m_curveCombo = std::make_unique<juce::ComboBox>("curve");
    m_curveCombo->addItem(BM_TJ("studio.settings.curve.linear"), 1);
    m_curveCombo->addItem(BM_TJ("studio.settings.curve.equalPower"), 2);
    m_curveCombo->addItem(BM_TJ("studio.settings.curve.sCurve"), 3);
    m_curveCombo->addItem(BM_TJ("studio.settings.curve.constantPower"), 4);
    m_curveCombo->setSelectedId(3, juce::dontSendNotification);
    m_curveCombo->setTooltip(BM_TJ("studio.settings.curveTip"));
    m_curveCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1A1A24));
    m_curveCombo->setColour(juce::ComboBox::textColourId, juce::Colours::white);
    m_curveCombo->onChange = [this] {
        Prefs::setInt("mixlab.curveId", m_curveCombo->getSelectedId());
        if (m_currentTransition) {
            // 1=Linear 2=EqualPower 3=SCurve 4=ConstantPower -> CurveType enum
            m_currentTransition->curve = (CurveType)(m_curveCombo->getSelectedId() - 1);
            if (auto* parent = getParentComponent()) parent->repaint();
        }
    };
    addAndMakeVisible(*m_curveCombo);

    m_durationSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_durationSlider->setRange(4, 64, 4);
    m_durationSlider->setValue(16);
    m_durationSlider->setTextValueSuffix(" bars");
    m_durationSlider->setColour(juce::Slider::trackColourId, Colors::accent());
    addAndMakeVisible(*m_durationSlider);

    m_durationLabel = std::make_unique<juce::Label>("dl", BM_TJ("setPrep.duration") + ":");
    m_durationLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFE2E8F0));
    addAndMakeVisible(*m_durationLabel);

    m_compatLabel = std::make_unique<juce::Label>("cl", BM_TJ("studio.scoreNone"));
    m_compatLabel->setColour(juce::Label::textColourId, Colors::accent());
    m_compatLabel->setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(*m_compatLabel);

    m_previewButton = std::make_unique<juce::TextButton>(BM_TJ("studio.preview"));
    m_previewButton->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_previewButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_previewButton->onClick = [this]() { if (onPreview) onPreview(); };
    addAndMakeVisible(*m_previewButton);

    // P1.5 : Auto-Gen (IA) — demande SmartTransitionGen une suggestion type/durée.
    m_autoGenButton = std::make_unique<juce::TextButton>(BM_TJ("studio.autoGen"));
    m_autoGenButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFA855F7)); // violet AI
    m_autoGenButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_autoGenButton->onClick = [this]() { if (onAutoGen) onAutoGen(); };
    addAndMakeVisible(*m_autoGenButton);

    // === STEM PRESET (BeatMate orange) ===
    m_stemLabel = std::make_unique<juce::Label>("sl", "STEMS:");
    m_stemLabel->setFont(juce::Font(9.0f, juce::Font::bold));
    m_stemLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFFB923C));  // Orange
    addAndMakeVisible(*m_stemLabel);

    m_stemPresetCombo = std::make_unique<juce::ComboBox>("stemPreset");
    m_stemPresetCombo->addItem(BM_TJ("studio.settings.stemPreset.none"), 1);
    m_stemPresetCombo->addItem(BM_TJ("studio.settings.stemPreset.crossfade"), 2);
    m_stemPresetCombo->addItem(BM_TJ("studio.settings.stemPreset.swap"), 3);
    m_stemPresetCombo->addItem(BM_TJ("studio.settings.stemPreset.full"), 4);
    m_stemPresetCombo->addItem(BM_TJ("studio.settings.stemPreset.manual"), 5);
    m_stemPresetCombo->setSelectedId(1, juce::dontSendNotification);
    m_stemPresetCombo->setTooltip(BM_TJ("studio.settings.stemPresetTip"));
    m_stemPresetCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFFFB923C).withAlpha(0.2f));
    m_stemPresetCombo->setColour(juce::ComboBox::textColourId, juce::Colour(0xFFFB923C));
    m_stemPresetCombo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFFFB923C).withAlpha(0.5f));
    m_stemPresetCombo->onChange = [this] {
        Prefs::setInt("mixlab.stemPresetId", m_stemPresetCombo->getSelectedId());
    };
    addAndMakeVisible(*m_stemPresetCombo);

    // === LOCK button ===
    m_lockButton = std::make_unique<juce::TextButton>(BM_TJ("studio.lock"));
    m_lockButton->setClickingTogglesState(true);
    m_lockButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF1A1A24));
    m_lockButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF94A3B8));
    m_lockButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFEAB308).withAlpha(0.4f));
    m_lockButton->setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFEAB308));
    m_lockButton->onClick = [this] {
        Prefs::setBool("mixlab.transitionLocked", m_lockButton->getToggleState());
    };
    addAndMakeVisible(*m_lockButton);

    // === SAVE PRESET button (Pro+) ===
    m_savePresetBtn = std::make_unique<juce::TextButton>(BM_TJ("studio.savePreset"));
    m_savePresetBtn->setColour(juce::TextButton::buttonColourId, Colors::accent());
    m_savePresetBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    // FIX bug #35.2 : was a stub log. Now persists the current TransitionInfo
    m_savePresetBtn->onClick = [this] {
        if (!m_currentTransition) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Preset", juce::String::fromUTF8(u8"Sélectionnez d'abord une transition à sauvegarder."), "OK");
            return;
        }
        auto win = std::make_shared<juce::AlertWindow>(
            juce::String::fromUTF8(u8"Sauvegarder un preset de transition"),
            juce::String::fromUTF8(u8"Donnez un nom à ce preset :"),
            juce::MessageBoxIconType::QuestionIcon);
        win->addTextEditor("name", juce::String::fromUTF8(u8"Mon preset"), "Nom :");
        win->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
        win->addButton("Annuler",0, juce::KeyPress(juce::KeyPress::escapeKey));
        win->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, win](int r) {
                if (r != 1) return;
                const auto name = win->getTextEditorContents("name").trim();
                if (name.isEmpty()) return;
                if (!::g_serviceLocator) return;
                auto* sm = ::g_serviceLocator->tryGet<Services::Config::SettingsManager>();
                if (!sm) return;
                const std::string key = "mixlab.transitionPresets." + name.toStdString();
                nlohmann::json j;
                j["type"]            = static_cast<int>(m_currentTransition->type);
                j["curve"]           = static_cast<int>(m_currentTransition->curve);
                j["durationBars"]    = m_currentTransition->durationBars;
                j["durationSeconds"] = m_currentTransition->durationSeconds;
                sm->set<std::string>(key, j.dump());
                sm->save();
                spdlog::info("[MixLabView] Transition preset '{}' saved.", name.toStdString());
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                    "Preset", juce::String::fromUTF8(u8"Preset « ") + name +
                    juce::String::fromUTF8(u8" » sauvegardé."), "OK");
            }));
    };
    addAndMakeVisible(*m_savePresetBtn);

    // === Crossfade curve editor (#31) — interactive points canvas ===
    m_curveEditor = std::make_unique<CrossfadeCurveEditor>();
    m_curveEditor->onChanged = [this]() {
        // Persist : the editor mutates the live TransitionInfo.crossfadePoints
        repaint();
    };
    addAndMakeVisible(*m_curveEditor);

    // Reload persisted transition-editor selectors
    {
        const int curveId = Prefs::getInt("mixlab.curveId", 3);
        if (curveId >= 1 && curveId <= m_curveCombo->getNumItems())
            m_curveCombo->setSelectedId(curveId, juce::dontSendNotification);
        const int stemId = Prefs::getInt("mixlab.stemPresetId", 1);
        if (stemId >= 1 && stemId <= m_stemPresetCombo->getNumItems())
            m_stemPresetCombo->setSelectedId(stemId, juce::dontSendNotification);
        if (m_lockButton)
            m_lockButton->setToggleState(Prefs::getBool("mixlab.transitionLocked", false),
                                         juce::dontSendNotification);
    }
}

void MixLabView::TransitionEditorComponent::loadTransition(TransitionInfo& info)
{
    m_currentTransition = &info;
    m_typeCombo->setSelectedId((int)info.type + 1, juce::dontSendNotification);
    m_curveCombo->setSelectedId((int)info.curve + 1, juce::dontSendNotification);
    m_durationSlider->setValue(info.durationBars, juce::dontSendNotification);
    m_compatLabel->setText(BM_TJ("studio.scoreLabel") + " " + juce::String(info.compatibilityScore) + "%", juce::dontSendNotification);
    if (m_curveEditor) {
        m_curveEditor->bind(&info.crossfadePoints);
    }
    repaint();
}

void MixLabView::TransitionEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF080810));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(Colors::primary());
    g.drawText(juce::String::fromUTF8("ÉDITEUR DE TRANSITION"), 10, 6, 300, 16, juce::Justification::centredLeft);
}

void MixLabView::TransitionEditorComponent::resized()
{
    int y = 28;
    m_compatLabel->setBounds(10, y, 200, 20);
    if (m_lockButton) m_lockButton->setBounds(220, y, 60, 20);
    if (m_savePresetBtn) m_savePresetBtn->setBounds(290, y, 90, 20);
    y += 24;
    m_typeCombo->setBounds(10, y, 150, 22);
    m_curveCombo->setBounds(170, y, 150, 22);
    y += 26;
    m_durationLabel->setBounds(10, y, 50, 22);
    m_durationSlider->setBounds(60, y, 200, 22);
    m_previewButton->setBounds(270, y, 70, 22);
    if (m_autoGenButton) m_autoGenButton->setBounds(350, y, 80, 22);
    y += 26;
    // Stem preset row (orange)
    if (m_stemLabel) m_stemLabel->setBounds(10, y, 50, 22);
    if (m_stemPresetCombo) m_stemPresetCombo->setBounds(60, y, 200, 22);
    y += 28;
    // Crossfade curve editor takes the remaining vertical space.
    if (m_curveEditor) {
        const int remaining = std::max(40, getHeight() - y - 4);
        m_curveEditor->setBounds(10, y, getWidth() - 20, remaining);
    }
}
#endif // CrossfadeCurveEditor + TransitionEditorComponent (legacy)

MixLabView::StudioToolbar::StudioToolbar()
{
    auto makeBtn = [this](const juce::String& text, juce::Colour col = juce::Colour(0xFF1A1A24)) {
        auto b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, col);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b.get());
        return b;
    };

    m_newBtn = makeBtn(BM_TJ("studio.new"), Colors::primary());
    m_openBtn = makeBtn(BM_TJ("studio.open"));
    m_saveBtn = makeBtn(BM_TJ("studio.save"), Colors::success().withAlpha(0.7f));
    m_exportBtn = makeBtn(BM_TJ("studio.export"), Colors::accent());
    m_recordBtn = makeBtn(BM_TJ("studio.record"), Colors::error().withAlpha(0.7f));
    m_undoBtn = makeBtn("Undo");
    m_redoBtn = makeBtn("Redo");

    m_cutBtn   = makeBtn("Cut");
    m_copyBtn  = makeBtn("Copy");
    m_pasteBtn = makeBtn("Paste");
    m_splitBtn = makeBtn("Split");
    m_delBtn   = makeBtn(BM_TJ("studio.delete"), Colors::error().withAlpha(0.5f));

    m_stemsToggleBtn = makeBtn(BM_TJ("studio.stemsView"), juce::Colour(0xFFFB923C));
    m_stemsGenBtn    = makeBtn(BM_TJ("studio.genStems"), juce::Colour(0xFFFB923C).withAlpha(0.7f));

    // Rekordbox XML import — moved to Settings â†’ DJ Software per UX
    m_xmlImportBtn   = makeBtn(BM_TJ("studio.importRbXml"), juce::Colour(0xFF8B5CF6));
    m_xmlImportBtn->setVisible(false);

    m_newBtn->onClick    = [this]() { if (onNew) onNew(); };
    m_openBtn->onClick   = [this]() { if (onOpen) onOpen(); };
    m_saveBtn->onClick   = [this]() { if (onSave) onSave(); };
    m_exportBtn->onClick = [this]() { if (onExport) onExport(); };
    m_recordBtn->onClick = [this]() { if (onRecord) onRecord(); };
    m_undoBtn->onClick   = [this]() { if (onUndo) onUndo(); };
    m_redoBtn->onClick   = [this]() { if (onRedo) onRedo(); };
    m_cutBtn->onClick        = [this]() { if (onCut)            onCut(); };
    m_copyBtn->onClick       = [this]() { if (onCopy)           onCopy(); };
    m_pasteBtn->onClick      = [this]() { if (onPaste)          onPaste(); };
    m_splitBtn->onClick      = [this]() { if (onSplit)          onSplit(); };
    m_delBtn->onClick        = [this]() { if (onDelete)         onDelete(); };
    m_stemsToggleBtn->onClick = [this]() { if (onToggleStems)   onToggleStems(); };
    m_stemsGenBtn->onClick    = [this]() { if (onGenerateStems) onGenerateStems(); };
    m_xmlImportBtn->onClick   = [this]() { if (onImportXml)     onImportXml(); };

    m_quantizeBtn = makeBtn("Quantize All", juce::Colour(0xFF22C55E).withAlpha(0.7f));
    m_quantizeBtn->onClick = [this]() { if (onQuantizeAll) onQuantizeAll(); };

    m_aiMixBtn = makeBtn("AI Mix", juce::Colour(0xFF8B5CF6));
    m_aiMixBtn->onClick = [this]() { if (onAiMix) onAiMix(); };

    m_snapCombo = std::make_unique<juce::ComboBox>();
    m_snapCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF12121A));
    m_snapCombo->setColour(juce::ComboBox::textColourId,       juce::Colour(0xFFF1F5F9));
    m_snapCombo->setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xFF3B82F6));
    m_snapCombo->addItem("Snap: Off",     1);
    m_snapCombo->addItem("Snap: Beat",    2);
    m_snapCombo->addItem("Snap: Bar",     3);
    m_snapCombo->addItem("Snap: 1/2 Bar", 4);
    m_snapCombo->addItem("Snap: 1/16",    5);
    m_snapCombo->setSelectedId(2, juce::dontSendNotification);  // Default: Beat
    m_snapCombo->onChange = [this]() {
        if (onSnapModeChanged) onSnapModeChanged(m_snapCombo->getSelectedId() - 1);
    };
    addAndMakeVisible(*m_snapCombo);

    auto makeLabel = [this](const juce::String& text, juce::Colour col) {
        auto l = std::make_unique<juce::Label>("", text);
        l->setColour(juce::Label::textColourId, col);
        l->setFont(juce::Font(11.0f, juce::Font::bold));
        addAndMakeVisible(l.get());
        return l;
    };
    m_bpmLabel = makeLabel("BPM: ---", Colors::primary());
    m_keyLabel = makeLabel(BM_TJ("studio.keyNone"), juce::Colour(0xFF8B5CF6));
    m_totalDurationLabel = makeLabel(BM_TJ("studio.durationZero"), juce::Colour(0xFFE2E8F0));
}

void MixLabView::StudioToolbar::setBPMLabel(double bpm)
{
    m_bpmLabel->setText("BPM: " + (bpm > 0 ? juce::String(bpm, 1) : juce::String("---")),
                        juce::dontSendNotification);
}

void MixLabView::StudioToolbar::setKeyLabel(const juce::String& key)
{
    m_keyLabel->setText(BM_TJ("studio.key") + " " + (key.isNotEmpty() ? key : juce::String("---")),
                        juce::dontSendNotification);
}

void MixLabView::StudioToolbar::setTotalDurationLabel(double seconds)
{
    int h = (int)(seconds / 3600), m = (int)(seconds / 60) % 60, s = (int)seconds % 60;
    m_totalDurationLabel->setText(BM_TJ("studio.duration") + " " + juce::String::formatted("%d:%02d:%02d", h, m, s),
                                  juce::dontSendNotification);
}

void MixLabView::StudioToolbar::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xFF0F0F18));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(juce::Colour(0xFF2A2A38));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), 4.0f, 1.0f);
}

void MixLabView::StudioToolbar::resized()
{
    int x = 8, y = 4, h = 24;
    m_newBtn->setBounds(x, y, 80, h); x += 84;
    m_openBtn->setBounds(x, y, 60, h); x += 64;
    m_saveBtn->setBounds(x, y, 100, h); x += 104;
    m_exportBtn->setBounds(x, y, 80, h); x += 84;
    m_recordBtn->setBounds(x, y, 100, h); x += 104;
    x += 12;
    m_undoBtn->setBounds(x, y, 50, h); x += 54;
    m_redoBtn->setBounds(x, y, 50, h); x += 54;
    x += 12;
    m_cutBtn->setBounds(x, y, 44, h);   x += 48;
    m_copyBtn->setBounds(x, y, 48, h);  x += 52;
    m_pasteBtn->setBounds(x, y, 50, h); x += 54;
    m_splitBtn->setBounds(x, y, 48, h); x += 52;
    m_delBtn->setBounds(x, y, 56, h);   x += 60;
    x += 12;
    m_stemsToggleBtn->setBounds(x, y, 88, h); x += 92;
    m_stemsGenBtn->setBounds(x, y, 80, h);    x += 84;
    // m_xmlImportBtn moved to Settings â†’ DJ Software (hidden here).
    x += 12;
    if (m_quantizeBtn) { m_quantizeBtn->setBounds(x, y, 100, h); x += 104; }
    if (m_snapCombo)   { m_snapCombo->setBounds(x, y, 120, h);   x += 124; }
    if (m_aiMixBtn)    { m_aiMixBtn->setBounds(x, y, 80, h);     x += 84; }
    x += 12;
    m_bpmLabel->setBounds(x, y, 80, h); x += 84;
    m_keyLabel->setBounds(x, y, 70, h); x += 74;
    m_totalDurationLabel->setBounds(x, y, 130, h);
}


struct MixLabView::ListenerAdapters
{
    struct TimelineBridge : public MixLabView::TimelineComponent::Listener {
        MixLabView& owner;
        explicit TimelineBridge(MixLabView& o) : owner(o) {}
        void transitionClicked(int idx) override {
            // Sélection visuelle uniquement — l'édition se fait directement
            if (!owner.m_timeline) return;
            auto& trans = owner.m_timeline->getTransitions();
            if (idx >= 0 && idx < (int)trans.size()) {
                owner.m_timeline->m_selectedTransition = idx;
                owner.m_timeline->repaint();
            }
        }
    };

    struct LibraryBridge : public LibraryBrowserPanel::Listener {
        MixLabView& owner;
        explicit LibraryBridge(MixLabView& o) : owner(o) {}
        void trackDoubleClicked(const Models::Track& t) override { owner.onAddTrack(t); }
        void addTrackRequested(const Models::Track& t) override  { owner.onAddTrack(t); }
    };

    struct StudioTimelineBridge : public Studio::StudioTimeline::Listener {
        MixLabView& owner;
        explicit StudioTimelineBridge(MixLabView& o) : owner(o) {}
        // Defined out-of-line below ClipInspectorPanel so the inspector's
        void clipSelected(int index) override;
        void clipChanged (int index) override;
    };

    TimelineBridge       timeline;
    LibraryBridge        library;
    StudioTimelineBridge studio;
    explicit ListenerAdapters(MixLabView& o)
        : timeline(o), library(o), studio(o) {}
};

MixLabView::MixLabView() : m_provider(nullptr)
{
    if (::g_serviceLocator) {
        m_provider = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
    }
    m_listenerAdapters = std::make_unique<ListenerAdapters>(*this);
    setupUI();
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

MixLabView::MixLabView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    if (!m_provider && ::g_serviceLocator) {
        m_provider = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
    }
    m_listenerAdapters = std::make_unique<ListenerAdapters>(*this);
    setupUI();
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

// MixLabView::ClipInspectorPanel — Logic Pro / Studio One style left inspector.
class MixLabView::ClipInspectorPanel : public juce::Component
{
public:
    ClipInspectorPanel()
    {
        m_titleEditor.setMultiLine(false);
        m_titleEditor.setReturnKeyStartsNewLine(false);
        m_titleEditor.setFont(Type::body());
        m_titleEditor.setColour(juce::TextEditor::backgroundColourId, Colors::bgCard());
        m_titleEditor.setColour(juce::TextEditor::textColourId,       Colors::textPrimary());
        m_titleEditor.setColour(juce::TextEditor::outlineColourId,    Colors::border());
        m_titleEditor.setColour(juce::TextEditor::focusedOutlineColourId, Colors::borderFocus());
        m_titleEditor.onReturnKey = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            m_timeline->renameClip(m_clipIndex, m_titleEditor.getText());
        };
        m_titleEditor.onFocusLost = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            m_timeline->renameClip(m_clipIndex, m_titleEditor.getText());
        };
        addAndMakeVisible(m_titleEditor);

        m_inSlider .setRange(0.0, 1.0, 0.001);
        m_outSlider.setRange(0.0, 1.0, 0.001);
        configureSlider(m_inSlider);
        configureSlider(m_outSlider);
        m_inSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            if (auto* clip = m_timeline->getClip(m_clipIndex)) {
                const double in  = m_inSlider .getValue();
                const double out = juce::jmax(in + 0.05, m_outSlider.getValue());
                clip->setAudioRange(in, out);
                refreshLengthLabel();
            }
        };
        m_outSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            if (auto* clip = m_timeline->getClip(m_clipIndex)) {
                const double out = m_outSlider.getValue();
                const double in  = juce::jmin(m_inSlider.getValue(), out - 0.05);
                clip->setAudioRange(in, out);
                refreshLengthLabel();
            }
        };
        addAndMakeVisible(m_inSlider);
        addAndMakeVisible(m_outSlider);

        m_gainSlider.setRange(-60.0, 12.0, 0.1);
        m_gainSlider.setValue(0.0, juce::dontSendNotification);
        configureSlider(m_gainSlider);
        m_gainSlider.setTextValueSuffix(" dB");
        m_gainSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            const double db = m_gainSlider.getValue();
            // -60 dB clamps to silence (gain01 ~ 0).
            const float gain01 = (db <= -59.9)
                ? 0.0f
                : (float) juce::jlimit(0.0, 1.0,
                                       std::pow(10.0, db / 20.0) * 0.5);
            m_timeline->setClipVolume(m_clipIndex, gain01);
        };

        m_pitchSlider.setRange(-12.0, 12.0, 0.1);
        m_pitchSlider.setValue(0.0, juce::dontSendNotification);
        configureSlider(m_pitchSlider);
        m_pitchSlider.setTextValueSuffix(" st");
        m_pitchSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            if (auto* clip = m_timeline->getClip(m_clipIndex)) {
                clip->setPitchSemitones(m_pitchSlider.getValue());
            }
        };

        m_stretchSlider.setRange(0.5, 2.0, 0.01);
        m_stretchSlider.setValue(1.0, juce::dontSendNotification);
        configureSlider(m_stretchSlider);
        m_stretchSlider.setTextValueSuffix("x");
        m_stretchSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            if (auto* clip = m_timeline->getClip(m_clipIndex)) {
                clip->setTempoRatio(m_stretchSlider.getValue());
            }
        };

        addAndMakeVisible(m_gainSlider);
        addAndMakeVisible(m_pitchSlider);
        addAndMakeVisible(m_stretchSlider);

        m_fadeInSlider .setRange(0.0, 10.0, 0.01);
        m_fadeOutSlider.setRange(0.0, 10.0, 0.01);
        configureSlider(m_fadeInSlider);
        configureSlider(m_fadeOutSlider);
        m_fadeInSlider .setTextValueSuffix(" s");
        m_fadeOutSlider.setTextValueSuffix(" s");
        m_fadeInSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            m_timeline->setClipFadeIn(m_clipIndex, m_fadeInSlider.getValue());
        };
        m_fadeOutSlider.onValueChange = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            m_timeline->setClipFadeOut(m_clipIndex, m_fadeOutSlider.getValue());
        };
        addAndMakeVisible(m_fadeInSlider);
        addAndMakeVisible(m_fadeOutSlider);

        m_fadeInCurve .addItem("Linear", 1);
        m_fadeInCurve .addItem("Exp",    2);
        m_fadeInCurve .addItem("Log",    3);
        m_fadeInCurve .setSelectedId(1, juce::dontSendNotification);
        m_fadeOutCurve.addItem("Linear", 1);
        m_fadeOutCurve.addItem("Exp",    2);
        m_fadeOutCurve.addItem("Log",    3);
        m_fadeOutCurve.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(m_fadeInCurve);
        addAndMakeVisible(m_fadeOutCurve);

        m_fxModel.owner = this;
        m_fxList.setModel(&m_fxModel);
        m_fxList.setRowHeight(20);
        m_fxList.setColour(juce::ListBox::backgroundColourId, Colors::bgCard());
        m_fxList.setColour(juce::ListBox::outlineColourId,    Colors::border());
        m_fxList.setOutlineThickness(1);
        addAndMakeVisible(m_fxList);

        m_addFxButton.setButtonText("+ Add FX");
        m_addFxButton.setColour(juce::TextButton::buttonColourId, Colors::bgElevated());
        m_addFxButton.setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        m_addFxButton.onClick = [this]() {
            if (m_clipIndex < 0 || !m_timeline) return;
            juce::PopupMenu menu;
            const char* kTypes[] = { "Reverb", "Delay", "Filter", "EQ",
                                     "Distortion", "Phaser", "Chorus",
                                     "Compressor", "Pitch" };
            for (int i = 0; i < (int) (sizeof(kTypes) / sizeof(kTypes[0])); ++i)
                menu.addItem(i + 1, kTypes[i]);
            juce::Component::SafePointer<ClipInspectorPanel> self(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_addFxButton),
                [self, kTypes](int r) {
                    if (!self || r <= 0) return;
                    if (self->m_clipIndex < 0 || !self->m_timeline) return;
                    self->m_timeline->addClipEffect(self->m_clipIndex, kTypes[r - 1]);
                    self->refreshFxList();
                });
        };
        addAndMakeVisible(m_addFxButton);

        setEnabledControls(false);
    }

    ~ClipInspectorPanel() override = default;

    void setStudioTimeline(BeatMate::UI::Studio::StudioTimeline* tl) noexcept
    {
        m_timeline = tl;
    }

    void setClipIndex(int idx)
    {
        m_clipIndex = idx;
        if (idx < 0 || !m_timeline) {
            setEnabledControls(false);
            m_titleEditor.setText({}, juce::dontSendNotification);
            m_startLabel .setText({}, juce::dontSendNotification);
            m_lengthLabel.setText({}, juce::dontSendNotification);
            m_fxModel.items.clear();
            m_fxList.updateContent();
            repaint();
            return;
        }

        auto* clip = m_timeline->getClip(idx);
        if (!clip) {
            setClipIndex(-1);
            return;
        }

        setEnabledControls(true);

        m_titleEditor.setText(juce::String(clip->getTrack().title),
                              juce::dontSendNotification);
        m_startLabel .setText(formatTime(clip->getStartSec()),
                              juce::dontSendNotification);
        refreshLengthLabel();

        const double in  = clip->getAudioInSec();
        const double out = clip->getAudioOutSec();
        const double dur = juce::jmax(out, in + 0.1) + 1.0;
        m_inSlider .setRange(0.0, dur, 0.001);
        m_outSlider.setRange(0.0, dur, 0.001);
        m_inSlider .setValue(in,  juce::dontSendNotification);
        m_outSlider.setValue(out, juce::dontSendNotification);

        const float vol01 = clip->getVolume();
        const double db = (vol01 <= 1e-4f)
            ? -60.0
            : juce::jlimit(-60.0, 12.0, 20.0 * std::log10((double) vol01 * 2.0));
        m_gainSlider.setValue(db, juce::dontSendNotification);
        m_pitchSlider .setValue(clip->getPitchSemitones(), juce::dontSendNotification);
        m_stretchSlider.setValue(clip->getTempoRatio(),    juce::dontSendNotification);

        const auto fade = clip->getFade();
        m_fadeInSlider .setValue(fade.inSec,  juce::dontSendNotification);
        m_fadeOutSlider.setValue(fade.outSec, juce::dontSendNotification);

        refreshFxList();

        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Colors::bgSurface());

        g.setColour(Colors::border());
        g.fillRect(getWidth() - 1, 0, 1, getHeight());

        g.setFont(Type::label());
        g.setColour(Colors::textMuted());
        for (const auto& sec : m_sectionHeaders) {
            g.drawText(sec.title.toUpperCase(), sec.bounds,
                       juce::Justification::centredLeft, false);
        }

        g.setFont(Type::caption());
        g.setColour(Colors::textSecondary());
        g.drawText("Start",   m_startLabelRect,  juce::Justification::centredLeft, false);
        g.drawText("Length",  m_lengthLabelRect, juce::Justification::centredLeft, false);
        g.drawText("In",      m_inLabelRect,     juce::Justification::centredLeft, false);
        g.drawText("Out",     m_outLabelRect,    juce::Justification::centredLeft, false);
        g.drawText("Gain",    m_gainLabelRect,   juce::Justification::centredLeft, false);
        g.drawText("Pitch",   m_pitchLabelRect,  juce::Justification::centredLeft, false);
        g.drawText("Stretch", m_stretchLabelRect,juce::Justification::centredLeft, false);
        g.drawText("Fade In", m_fadeInLabelRect, juce::Justification::centredLeft, false);
        g.drawText("Fade Out",m_fadeOutLabelRect,juce::Justification::centredLeft, false);

        g.setColour(Colors::textPrimary());
        g.drawText(m_startLabel.getText(),  m_startValueRect,
                   juce::Justification::centredRight, false);
        g.drawText(m_lengthLabel.getText(), m_lengthValueRect,
                   juce::Justification::centredRight, false);
    }

    void resized() override
    {
        const int pad = Spacing::md;
        const int rowH = 20;
        const int hdrH = 16;
        int y = pad;

        m_sectionHeaders.clear();

        m_sectionHeaders.push_back({ "Clip",
            juce::Rectangle<int>(pad, y, getWidth() - pad * 2, hdrH) });
        y += hdrH + Spacing::xs;

        m_titleEditor.setBounds(pad, y, getWidth() - pad * 2, 24);
        y += 24 + Spacing::xs;

        const int half = (getWidth() - pad * 2) / 2;
        m_startLabelRect  = { pad,        y, 60, rowH };
        m_startValueRect  = { pad + 60,   y, getWidth() - pad * 2 - 60, rowH };
        y += rowH;
        m_lengthLabelRect = { pad,        y, 60, rowH };
        m_lengthValueRect = { pad + 60,   y, getWidth() - pad * 2 - 60, rowH };
        y += rowH + Spacing::xs;

        m_inLabelRect = { pad, y, 30, rowH };
        m_inSlider.setBounds(pad + 30, y, getWidth() - pad * 2 - 30, rowH);
        y += rowH;
        m_outLabelRect = { pad, y, 30, rowH };
        m_outSlider.setBounds(pad + 30, y, getWidth() - pad * 2 - 30, rowH);
        y += rowH + Spacing::md;
        juce::ignoreUnused(half);

        m_sectionHeaders.push_back({ "Volume / Pitch",
            juce::Rectangle<int>(pad, y, getWidth() - pad * 2, hdrH) });
        y += hdrH + Spacing::xs;

        m_gainLabelRect = { pad, y, 50, rowH };
        m_gainSlider.setBounds(pad + 50, y, getWidth() - pad * 2 - 50, rowH);
        y += rowH;
        m_pitchLabelRect = { pad, y, 50, rowH };
        m_pitchSlider.setBounds(pad + 50, y, getWidth() - pad * 2 - 50, rowH);
        y += rowH;
        m_stretchLabelRect = { pad, y, 50, rowH };
        m_stretchSlider.setBounds(pad + 50, y, getWidth() - pad * 2 - 50, rowH);
        y += rowH + Spacing::md;

        m_sectionHeaders.push_back({ "Fades",
            juce::Rectangle<int>(pad, y, getWidth() - pad * 2, hdrH) });
        y += hdrH + Spacing::xs;

        m_fadeInLabelRect = { pad, y, 60, rowH };
        m_fadeInSlider.setBounds(pad + 60, y, getWidth() - pad * 2 - 60, rowH);
        y += rowH;
        m_fadeInCurve.setBounds(pad + 60, y, getWidth() - pad * 2 - 60, rowH);
        y += rowH + Spacing::xs;

        m_fadeOutLabelRect = { pad, y, 60, rowH };
        m_fadeOutSlider.setBounds(pad + 60, y, getWidth() - pad * 2 - 60, rowH);
        y += rowH;
        m_fadeOutCurve.setBounds(pad + 60, y, getWidth() - pad * 2 - 60, rowH);
        y += rowH + Spacing::md;

        m_sectionHeaders.push_back({ "Effects",
            juce::Rectangle<int>(pad, y, getWidth() - pad * 2, hdrH) });
        y += hdrH + Spacing::xs;

        const int btnH = 24;
        const int listBottom = juce::jmax(y + 40, getHeight() - pad - btnH - Spacing::xs);
        m_fxList.setBounds(pad, y, getWidth() - pad * 2, listBottom - y);
        m_addFxButton.setBounds(pad, listBottom + Spacing::xs,
                                 getWidth() - pad * 2, btnH);
    }

private:
    static juce::String formatTime(double sec)
    {
        if (sec < 0.0) sec = 0.0;
        const int totalMs = (int) std::round(sec * 1000.0);
        const int mm = totalMs / 60000;
        const int ss = (totalMs / 1000) % 60;
        const int ms = totalMs % 1000;
        return juce::String::formatted("%02d:%02d.%03d", mm, ss, ms);
    }

    void configureSlider(juce::Slider& s)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
        s.setColour(juce::Slider::backgroundColourId, Colors::bgCard());
        s.setColour(juce::Slider::trackColourId,      Colors::primary());
        s.setColour(juce::Slider::thumbColourId,      Colors::primary());
        s.setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
        s.setColour(juce::Slider::textBoxOutlineColourId,
                    juce::Colours::transparentBlack);
    }

    void setEnabledControls(bool en)
    {
        m_titleEditor .setEnabled(en);
        m_inSlider    .setEnabled(en);
        m_outSlider   .setEnabled(en);
        m_gainSlider  .setEnabled(en);
        m_pitchSlider .setEnabled(en);
        m_stretchSlider.setEnabled(en);
        m_fadeInSlider .setEnabled(en);
        m_fadeOutSlider.setEnabled(en);
        m_fadeInCurve  .setEnabled(en);
        m_fadeOutCurve .setEnabled(en);
        m_fxList       .setEnabled(en);
        m_addFxButton  .setEnabled(en);
    }

    void refreshLengthLabel()
    {
        if (m_clipIndex < 0 || !m_timeline) return;
        if (auto* clip = m_timeline->getClip(m_clipIndex))
            m_lengthLabel.setText(formatTime(clip->getLengthSec()),
                                   juce::dontSendNotification);
        repaint();
    }

    void refreshFxList()
    {
        m_fxModel.items.clear();
        if (m_clipIndex >= 0 && m_timeline) {
            if (auto* clip = m_timeline->getClip(m_clipIndex)) {
                for (const auto& fx : clip->getEffects())
                    m_fxModel.items.push_back(juce::String(fx.type));
            }
        }
        m_fxList.updateContent();
        m_fxList.repaint();
    }

    BeatMate::UI::Studio::StudioTimeline* m_timeline { nullptr };
    int m_clipIndex { -1 };

    juce::TextEditor m_titleEditor;
    juce::Label      m_startLabel, m_lengthLabel;
    juce::Slider     m_inSlider, m_outSlider;

    juce::Slider m_gainSlider, m_pitchSlider, m_stretchSlider;

    juce::Slider   m_fadeInSlider, m_fadeOutSlider;
    juce::ComboBox m_fadeInCurve,  m_fadeOutCurve;

    struct FxModel : public juce::ListBoxModel {
        std::vector<juce::String> items;
        ClipInspectorPanel* owner { nullptr };

        int getNumRows() override { return (int) items.size(); }

        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                               int width, int height, bool selected) override
        {
            if (rowNumber < 0 || rowNumber >= (int) items.size()) return;
            if (selected) g.fillAll(Colors::primary().withAlpha(0.3f));
            g.setColour(Colors::textPrimary());
            g.setFont(Type::body());
            g.drawText(items[rowNumber],
                       Spacing::sm, 0, width - Spacing::sm * 2, height,
                       juce::Justification::centredLeft, true);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent& e) override
        {
            if (!owner || row < 0 || row >= (int) items.size()) return;
            if (e.mods.isRightButtonDown() && owner->m_timeline
                && owner->m_clipIndex >= 0)
            {
                owner->m_timeline->removeClipEffect(owner->m_clipIndex, row);
                owner->refreshFxList();
            }
        }
    };
    FxModel       m_fxModel;
    juce::ListBox m_fxList { "ClipFX", nullptr };
    juce::TextButton m_addFxButton;

    struct Section { juce::String title; juce::Rectangle<int> bounds; };
    std::vector<Section> m_sectionHeaders;
    juce::Rectangle<int> m_startLabelRect, m_startValueRect;
    juce::Rectangle<int> m_lengthLabelRect, m_lengthValueRect;
    juce::Rectangle<int> m_inLabelRect, m_outLabelRect;
    juce::Rectangle<int> m_gainLabelRect, m_pitchLabelRect, m_stretchLabelRect;
    juce::Rectangle<int> m_fadeInLabelRect, m_fadeOutLabelRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipInspectorPanel)
};

// Bridge bodies — needed here so ClipInspectorPanel's full type is visible.
void MixLabView::ListenerAdapters::StudioTimelineBridge::clipSelected(int index)
{
    if (owner.m_inspector)
        owner.m_inspector->setClipIndex(index);
}

void MixLabView::ListenerAdapters::StudioTimelineBridge::clipChanged(int index)
{
    if (owner.m_inspector
        && owner.m_studioTimeline
        && owner.m_studioTimeline->getSelectedIndex() == index)
    {
        owner.m_inspector->setClipIndex(index);
    }
}

MixLabView::~MixLabView()
{
    // Détacher les listeners avant que m_listenerAdapters ne détruise les bridges
    if (m_timeline && m_listenerAdapters)
        m_timeline->removeListener(&m_listenerAdapters->timeline);
    if (m_libraryBrowser && m_listenerAdapters)
        m_libraryBrowser->removeListener(&m_listenerAdapters->library);
    if (m_studioTimeline && m_listenerAdapters)
        m_studioTimeline->removeListener(&m_listenerAdapters->studio);
}

bool MixLabView::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    using K = juce::KeyPress;
    auto mods = key.getModifiers();
    bool ctrl = mods.isCtrlDown();

    if (key == K::spaceKey)
    {
        if (m_transportBar && m_transportBar->onPlay) m_transportBar->onPlay();
        return true;
    }

    if (ctrl && key.getKeyCode() == 'Z') {
        if (m_studioTimeline) m_studioTimeline->undo();
        m_undoManager.undo();
        return true;
    }
    if (ctrl && (key.getKeyCode() == 'Y' || (key.getKeyCode() == 'Z' && mods.isShiftDown()))) {
        if (m_studioTimeline) m_studioTimeline->redo();
        m_undoManager.redo();
        return true;
    }

    if (ctrl && key.getKeyCode() == 'X') {
        const bool onMixLab = m_timeline && m_timeline->m_selectedBlock >= 0
                              && m_timeline->m_selectedBlock < m_timeline->getTrackCount();
        if (onMixLab) {
            m_timeline->copySelection();
            m_timeline->deleteSelection();
        } else if (m_studioTimeline) {
            m_studioTimeline->cutSelection();
        }
        return true;
    }
    if (ctrl && key.getKeyCode() == 'C') {
        const bool onMixLab = m_timeline && m_timeline->m_selectedBlock >= 0
                              && m_timeline->m_selectedBlock < m_timeline->getTrackCount();
        if (onMixLab) {
            m_timeline->copySelection();
        } else if (m_studioTimeline) {
            m_studioTimeline->copySelection();
        }
        return true;
    }
    if (ctrl && key.getKeyCode() == 'V') {
        // Paste : prefere la cible qui a un clipboard non vide. MixLab
        if (! MixLabView::s_blockClipboard.empty() && m_timeline) {
            m_timeline->pasteSelection();
        } else if (m_studioTimeline) {
            m_studioTimeline->pasteAtPlayhead();
        }
        return true;
    }

    if (key == K::deleteKey || key == K::backspaceKey) {
        if (m_studioTimeline) m_studioTimeline->deleteSelection();
        return true;
    }

    if (ctrl && key.getKeyCode() == 'B') {
        if (m_studioTimeline) m_studioTimeline->splitAtPlayhead();
        return true;
    }
    if (ctrl && key.getKeyCode() == 'A') {
        if (m_studioTimeline) m_studioTimeline->selectAll();
        return true;
    }
    if (ctrl && key.getKeyCode() == 'D') {
        if (m_studioTimeline) {
            const int s = m_studioTimeline->getSelectedIndex();
            if (s >= 0) m_studioTimeline->duplicateClip(s);
        }
        return true;
    }
    if (ctrl && key.getKeyCode() == 'Q') {
        if (m_studioTimeline) m_studioTimeline->quantizeAllClips();
        return true;
    }

    if (key == K::escapeKey) {
        if (m_studioTimeline) m_studioTimeline->setCenterMode(!m_studioTimeline->isCenterMode());
        return true;
    }
    if (key == K::homeKey)   { if (m_studioTimeline) m_studioTimeline->jumpToStart(); return true; }
    if (key == K::endKey)    { if (m_studioTimeline) m_studioTimeline->jumpToEnd();   return true; }
    if (key == K::leftKey) {
        if (m_studioTimeline) m_studioTimeline->nudgePlayhead(mods.isShiftDown() ? -4.0 : -1.0);
        return true;
    }
    if (key == K::rightKey) {
        if (m_studioTimeline) m_studioTimeline->nudgePlayhead(mods.isShiftDown() ?  4.0 :  1.0);
        return true;
    }
    if (mods.isAltDown() && m_studioTimeline) {
        using TK = Studio::TransitionKind;
        if (key.getKeyCode() == '1') {
            m_studioTimeline->applyTransitionPresetAll(TK::Cut, 0);
            return true;
        }
        if (key.getKeyCode() == '2') {
            m_studioTimeline->applyTransitionPresetAll(TK::EqualPower, 8);
            return true;
        }
        if (key.getKeyCode() == '3') {
            m_studioTimeline->applyTransitionPresetAll(TK::EqualPower, 16);
            return true;
        }
        if (key.getKeyCode() == '4') {
            m_studioTimeline->applyTransitionPresetAll(TK::EqualPower, 32);
            return true;
        }
        if (key.getKeyCode() == '5') {
            m_studioTimeline->applyTransitionPresetAll(TK::EqualPower, 64);
            return true;
        }
    }

    if (key.getKeyCode() == K::numberPadAdd      || key.getTextCharacter() == '+'
        || key.getTextCharacter() == '=') {
        if (m_studioTimeline) m_studioTimeline->zoomIn();
        return true;
    }
    if (key.getKeyCode() == K::numberPadSubtract || key.getTextCharacter() == '-'
        || key.getTextCharacter() == '_') {
        if (m_studioTimeline) m_studioTimeline->zoomOut();
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'N') {
        if (m_studioTimeline) m_studioTimeline->jumpToNextTransition();
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'P') {
        if (m_studioTimeline) m_studioTimeline->jumpToPrevTransition();
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'G') {
        if (m_studioTimeline) m_studioTimeline->setShowBeatGrid(!m_studioTimeline->showsBeatGrid());
        return true;
    }

    if (ctrl && key.getKeyCode() == 'T' && !mods.isShiftDown())
    {
        if (m_timeline) {
            m_timeline->toggleStemsView();
            spdlog::info("[Studio] Stems view toggled ({})",
                         m_timeline->showStems ? "on" : "off");
        }
        return true;
    }

    if (ctrl && mods.isShiftDown() && key.getKeyCode() == 'T')
    {
        if (m_timeline && m_timeline->m_selectedBlock >= 0 &&
            m_timeline->m_selectedBlock < m_timeline->getTrackCount())
        {
            auto& block = m_timeline->getBlock(m_timeline->m_selectedBlock);
            const auto track = block.getTrack();
            spdlog::info("[Studio] Generating stems for '{}' (file={})",
                         track.title, track.filePath);
            auto* pool = BeatMate::getBackgroundPool();
            if (!pool) {
                spdlog::error("[Studio] stems: background pool unavailable");
                return true;
            }
            juce::Component::SafePointer<Studio::StudioTimeline> safeTl(m_studioTimeline.get());
            pool->addJob([trk = track, safeTl]() mutable {
                juce::AudioFormatManager fmgr;
                fmgr.registerBasicFormats();
                juce::File f(juce::String(trk.filePath));
                std::unique_ptr<juce::AudioFormatReader> reader(fmgr.createReaderFor(f));
                if (!reader) {
                    spdlog::error("[Studio] stems: cannot open {}", trk.filePath);
                    return;
                }
                int channels = (int) reader->numChannels;
                int sampleRate = (int) reader->sampleRate;
                int64_t totalFrames = reader->lengthInSamples;
                if (channels < 1 || sampleRate < 1 || totalFrames < 1) {
                    spdlog::error("[Studio] stems: bad header for {}", trk.filePath);
                    return;
                }
                juce::AudioBuffer<float> buf(channels, (int) totalFrames);
                reader->read(&buf, 0, (int) totalFrames, 0, true, true);
                std::vector<float> pcm;
                pcm.reserve(channels * totalFrames);
                for (int64_t i = 0; i < totalFrames; ++i) {
                    for (int c = 0; c < channels; ++c) {
                        pcm.push_back(buf.getSample(c, (int) i));
                    }
                }
                Core::AudioTrack at;
                at.loadData(std::move(pcm), sampleRate, channels);
                at.setFilePath(trk.filePath);

                Core::StemSeparator sep;
                auto result = sep.separate(at, [](float p, const std::string& stage) {
                    spdlog::info("[Studio] stems: {} {:.0f}%", stage, p * 100);
                });
                if (!result.success) {
                    spdlog::warn("[Studio] stems separation reported no success");
                    return;
                }
                Core::StemCache cache("StemsCache");
                std::string trackId = std::to_string(trk.id);
                if (cache.save(trackId, result)) {
                    spdlog::info("[Studio] stems cached for trackId={}", trackId);
                } else {
                    spdlog::warn("[Studio] stems cache.save failed for {}", trackId);
                }

                auto atToBuffer = [](Core::AudioTrack* t) -> juce::AudioBuffer<float> {
                    if (!t || !t->isLoaded()) return {};
                    const int ch = std::max(1, t->getChannels());
                    const int n  = (int) t->getNumFrames();
                    juce::AudioBuffer<float> b(ch, n);
                    for (int i = 0; i < n; ++i)
                        for (int c = 0; c < ch; ++c)
                            b.setSample(c, i, t->getSample((size_t) i, c));
                    return b;
                };
                // V phase B i7 — shared_ptr car juce::MessageManager::callAsync
                auto stemData = std::make_shared<Studio::StudioClip::StemData>();
                stemData->vocals     = atToBuffer(result.vocals.get());
                stemData->drums      = atToBuffer(result.drums.get());
                stemData->bass       = atToBuffer(result.bass.get());
                stemData->other      = atToBuffer(result.other.get());
                stemData->sampleRate = (double) sampleRate;
                stemData->ready      = true;

                juce::MessageManager::callAsync(
                    [safeTl, trkId = trk.id, sd = stemData]() {
                        auto* tl = safeTl.getComponent();
                        if (!tl) return;
                        const int nClips = tl->getNumClips();
                        for (int i = 0; i < nClips; ++i) {
                            auto* cp = tl->getClip(i);
                            if (cp && cp->getTrack().id == trkId) {
                                auto u = std::make_unique<Studio::StudioClip::StemData>(*sd);
                                cp->attachStems(std::move(u));
                                tl->repaint();
                                spdlog::info("[Studio] stems attached to clip {} (trackId={})",
                                             i, trkId);
                                return;
                            }
                        }
                        spdlog::warn("[Studio] stems generated but no Studio clip "
                                     "with trackId={} found to attach", trkId);
                    });
            });
            if (!m_timeline->showStems) m_timeline->toggleStemsView();
        } else {
            spdlog::info("[Studio] stems: select a track block first");
        }
        return true;
    }

    if (ctrl && key == K::backspaceKey)
    {
        spdlog::info("[Studio] Reset track automation");
        return true;
    }

    if (key == K::escapeKey)
    {
        spdlog::info("[Studio] Toggle center playhead");
        return true;
    }

    if (ctrl && key.getKeyCode() == '-')
    {
        if (m_timeline) m_timeline->setZoomLevel(m_timeline->getZoomLevel() * 0.8);
        return true;
    }
    if (ctrl && (key.getKeyCode() == '=' || key.getKeyCode() == '+'))
    {
        if (m_timeline) m_timeline->setZoomLevel(m_timeline->getZoomLevel() * 1.25);
        return true;
    }

    // V/F/H/K/L = ajouter point automation au playhead sur le block selectionne
    if (!ctrl && !mods.isShiftDown() && m_timeline)
    {
        int sel = m_timeline->m_selectedBlock;
        if (sel >= 0 && sel < m_timeline->getTrackCount())
        {
            auto& block = m_timeline->getBlock(sel);
            double playheadInBlock = m_timeline->getPlayheadPosition() - block.getStartTime();
            if (playheadInBlock >= 0 && playheadInBlock <= block.getTrack().duration)
            {
                int laneId = -1;
                switch (key.getKeyCode())
                {
                    case 'V': laneId = 0; break; // Volume
                    case 'H': laneId = 1; break; // Hi EQ
                    case 'K': laneId = 2; break; // Mid EQ
                    case 'L': laneId = 3; break; // Lo EQ
                    case 'F': laneId = 4; break; // Filter
                    default: break;
                }
                if (laneId >= 0)
                {
                    block.addAutomationPoint(laneId, playheadInBlock, 0.5f);
                    spdlog::info("[Studio] Auto point lane={} at t={:.2f}s", laneId, playheadInBlock);
                    return true;
                }
            }
        }
    }

    if (m_timeline && !ctrl && !mods.isShiftDown()) {
        if (key.getKeyCode() == 'S') {
            if (m_timeline->m_selectedBlock >= 0
                && m_timeline->m_selectedBlock < m_timeline->getTrackCount()) {
                auto& block = m_timeline->getBlock(m_timeline->m_selectedBlock);
                const bool cur = block.getProperties().getWithDefault("solo", false);
                block.getProperties().set("solo", !cur);
                block.repaint();
                spdlog::info("[Studio] Solo track={} -> {}",
                             m_timeline->m_selectedBlock, !cur ? "on" : "off");
                return true;
            }
        }
        if (key.getKeyCode() == 'M') {
            if (m_timeline->m_selectedBlock >= 0
                && m_timeline->m_selectedBlock < m_timeline->getTrackCount()) {
                auto& block = m_timeline->getBlock(m_timeline->m_selectedBlock);
                const bool cur = block.getProperties().getWithDefault("mute", false);
                block.getProperties().set("mute", !cur);
                block.repaint();
                spdlog::info("[Studio] Mute track={} -> {}",
                             m_timeline->m_selectedBlock, !cur ? "on" : "off");
                return true;
            }
        }
        if (key == juce::KeyPress::homeKey) {
            if (m_navBar && m_navBar->onSeek)
                m_navBar->onSeek(0.0);
            return true;
        }
    }

    if (ctrl && (key.getKeyCode() == '+' || key.getKeyCode() == '='
                 || key == juce::KeyPress::numberPadAdd)) {
        if (m_timeline) m_timeline->setZoomLevel(m_timeline->getZoomLevel() * 1.25);
        return true;
    }
    if (ctrl && (key.getKeyCode() == '-' || key == juce::KeyPress::numberPadSubtract)) {
        if (m_timeline) m_timeline->setZoomLevel(m_timeline->getZoomLevel() * 0.8);
        return true;
    }

    // Last-resort: forward unhandled keys to StudioTimeline so its own
    if (m_studioTimeline && m_studioTimeline->keyPressed(key))
        return true;

    return false;
}

void MixLabView::setupUI()
{
    m_toolbar = std::make_unique<StudioToolbar>();
    m_toolbar->onNew = [this]() { onNewProject(); };
    m_toolbar->onSave = [this]() { onSaveProject(); };
    m_toolbar->onOpen = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Ouvrir projet BeatMate",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.bmproj");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                if (m_studioTimeline) m_studioTimeline->loadProject(file);
            });
    };
    m_toolbar->onExport = [this]() {
        juce::PopupMenu menu;
        menu.addItem(1, BM_TJ("studio.export.wav"));
        menu.addItem(2, BM_TJ("studio.export.ableton"));
        menu.addSeparator();
        menu.addItem(3, BM_TJ("studio.export.mixcloud"));
        menu.addItem(4, BM_TJ("studio.export.youtube"));
        menu.addItem(5, BM_TJ("studio.export.soundcloud"));
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this](int result) {
                switch (result) {
                    case 1: onExportMix(); break;
                    case 2: onExportAbleton(); break;
                    case 3: onExportPackage("Mixcloud");  break;
                    case 4: onExportPackage("YouTube");   break;
                    case 5: onExportPackage("SoundCloud"); break;
                    default: break;
                }
            });
    };
    m_toolbar->onRecord = [this]() { onRecord(); };
    m_toolbar->onUndo = [this]() { if (m_studioTimeline) m_studioTimeline->undo(); };
    m_toolbar->onRedo = [this]() { if (m_studioTimeline) m_studioTimeline->redo(); };
    m_toolbar->onCut    = [this]() { if (m_studioTimeline) m_studioTimeline->cutSelection(); };
    m_toolbar->onCopy   = [this]() { if (m_studioTimeline) m_studioTimeline->copySelection(); };
    m_toolbar->onPaste  = [this]() { if (m_studioTimeline) m_studioTimeline->pasteAtPlayhead(); };
    m_toolbar->onSplit  = [this]() { if (m_studioTimeline) m_studioTimeline->splitAtPlayhead(); };
    m_toolbar->onDelete = [this]() { if (m_studioTimeline) m_studioTimeline->deleteSelection(); };
    m_toolbar->onQuantizeAll = [this]() {
        if (m_studioTimeline) m_studioTimeline->quantizeAllClips();
    };
    m_toolbar->onAiMix = [this]() {
        std::vector<Models::Track> lib;
        Services::Library::TrackDataProvider* prov = m_provider;
        if (!prov && ::g_serviceLocator)
            prov = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
        if (prov) lib = prov->getAllTracks();
        juce::Component::SafePointer<MixLabView> self(this);
        Studio::AIMixWizardDialog::showDialog(std::move(lib),
            [self](Services::AI::MashupResult result) {
                if (!self) return;
                if (!result.ok || !self->m_studioTimeline) return;
                self->m_studioTimeline->applyMashupPlan(result);
                spdlog::info("[Studio] AIMixWizard applied plan: {} clips, {} transitions ({})",
                             result.clips.size(),
                             result.transitions.size(),
                             result.reasoning.toStdString());
            });
    };
    m_toolbar->onSnapModeChanged = [this](int mode) {
        if (!m_studioTimeline) return;
        using SM = Studio::SnapMode;
        SM s = SM::Beat;
        switch (mode) {
            case 0: s = SM::Off;     break;
            case 1: s = SM::Beat;    break;
            case 2: s = SM::Bar;     break;
            case 3: s = SM::HalfBar; break;
            case 4: s = SM::HalfBeat;break;
        }
        m_studioTimeline->setSnap(s);
    };
    m_toolbar->onToggleStems = [this]() {
        if (m_studioTimeline) m_studioTimeline->setShowStems(true);
    };
    m_toolbar->onGenerateStems = [this]() {
        if (!m_timeline || m_timeline->m_selectedBlock < 0) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Stems",
                juce::String::fromUTF8(u8"Sélectionnez d'abord une piste sur la timeline."),
                "OK");
            spdlog::info("[Studio] gen stems: select a track first");
            return;
        }
        const auto trk = m_timeline->getBlock(m_timeline->m_selectedBlock).getTrack();
        spdlog::info("[Studio] gen stems for '{}'", trk.title);
        auto* pool2 = BeatMate::getBackgroundPool();
        if (!pool2) {
            spdlog::error("[Studio] gen stems: background pool unavailable");
            return;
        }

        const int toastId = BeatMate::UI::Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8(u8"Stems en cours…"),
            juce::String::fromUTF8(u8"Génération pour « ") + juce::String(trk.title) + juce::String::fromUTF8(u8" »"),
            BeatMate::UI::Widgets::ToastNotifier::Kind::Progress,
            0);

        // Capture self via a weak reference: if MixLabView is destroyed before the
        juce::Component::SafePointer<MixLabView> self(this);
        pool2->addJob([trk, self, toastId]() {
            juce::AudioFormatManager fmgr;
            fmgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(
                fmgr.createReaderFor(juce::File(juce::String(trk.filePath))));
            if (!reader) {
                juce::MessageManager::callAsync([self, trk, toastId]() {
                    if (!self) return;
                    BeatMate::UI::Widgets::ToastNotifier::getInstance().update(toastId,
                        juce::String::fromUTF8(u8"Lecture impossible : ") + juce::String(trk.filePath),
                        BeatMate::UI::Widgets::ToastNotifier::Kind::Error);
                });
                return;
            }
            int channels = (int) reader->numChannels;
            int sr = (int) reader->sampleRate;
            int64_t total = reader->lengthInSamples;
            if (channels < 1 || sr < 1 || total < 1) return;
            juce::AudioBuffer<float> buf(channels, (int) total);
            reader->read(&buf, 0, (int) total, 0, true, true);
            std::vector<float> pcm; pcm.reserve(channels * total);
            for (int64_t i = 0; i < total; ++i)
                for (int c = 0; c < channels; ++c)
                    pcm.push_back(buf.getSample(c, (int) i));
            Core::AudioTrack at;
            at.loadData(std::move(pcm), sr, channels);
            at.setFilePath(trk.filePath);
            Core::StemSeparator sep;
            auto res = sep.separate(at, [](float p, const std::string& s) {
                spdlog::info("[Studio] stems: {} {:.0f}%", s, p * 100);
            });
            const bool ok = res.success;
            if (ok) {
                Core::StemCache cache("StemsCache");
                cache.save(std::to_string(trk.id), res);
            }
            juce::MessageManager::callAsync([self, trk, ok, toastId]() {
                if (!self) return;
                auto& toast = BeatMate::UI::Widgets::ToastNotifier::getInstance();
                if (ok) {
                    if (self->m_timeline && !self->m_timeline->showStems)
                        self->m_timeline->toggleStemsView();
                    toast.update(toastId,
                        juce::String::fromUTF8(u8"Stems prêts pour « ") + juce::String(trk.title) + juce::String::fromUTF8(u8" »"),
                        BeatMate::UI::Widgets::ToastNotifier::Kind::Success);
                } else {
                    toast.update(toastId,
                        juce::String::fromUTF8(u8"Échec stems « ") + juce::String(trk.title) + juce::String::fromUTF8(u8" » — voir logs"),
                        BeatMate::UI::Widgets::ToastNotifier::Kind::Error);
                }
            });
        });
    };
    m_toolbar->onImportXml = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            BM_TJ("studio.importRekordboxXml"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.xml");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
            [chooser](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f.existsAsFile()) {
                    spdlog::info("[Studio] Import RB XML: {}",
                                 f.getFullPathName().toStdString());
                    // Parsing is handled downstream by RekordboxService /
                }
            });
    };
    addAndMakeVisible(*m_toolbar);

    m_navBar = std::make_unique<NavigationBar>();

    m_transportBar = std::make_unique<TransportBar>();
    m_transportBar->onPlay = [this]() {
        spdlog::info("[MixLab] Transport Play clicked: studioTL={}, projectTracks={}, m_isPlaying={}",
                     (m_studioTimeline ? "yes" : "no"), m_projectTracks.size(), m_isPlaying);
        if (m_studioTimeline) {
            if (m_studioTimeline->isPlaying()) m_studioTimeline->stop();
            else                                m_studioTimeline->play();
        }
        togglePlayPause();
    };
    m_transportBar->onPrevTrans = [this]() { if (m_studioTimeline) m_studioTimeline->jumpToPrevTransition(); };
    m_transportBar->onNextTrans = [this]() { if (m_studioTimeline) m_studioTimeline->jumpToNextTransition(); };
    m_transportBar->onMetronome = [this]() { if (m_studioTimeline) m_studioTimeline->toggleMetronome(); };
    m_transportBar->onCenterPlayhead = [this]() {
        if (m_studioTimeline) m_studioTimeline->setCenterMode(!m_studioTimeline->isCenterMode());
    };
    m_transportBar->onAddTracks = [this]() {
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8(u8"Ajouter des pistes"),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.m4a;*.ogg;*.wma");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
            [this, chooser](const juce::FileChooser& fc) {
                auto results = fc.getResults();
                if (results.isEmpty()) return;

                int added = 0;
                for (auto& f : results) {
                    if (!f.existsAsFile()) continue;
                    Models::Track t;
                    t.id          = static_cast<int64_t>(juce::Random::getSystemRandom().nextInt64());
                    t.title       = f.getFileNameWithoutExtension().toStdString();
                    t.filePath    = f.getFullPathName().toStdString();
                    juce::AudioFormatManager fmgr; fmgr.registerBasicFormats();
                    if (auto* reader = fmgr.createReaderFor(f)) {
                        if (reader->sampleRate > 0)
                            t.duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                        delete reader;
                    }
                    if (m_timeline) m_timeline->addTrack(t);
                    if (m_studioTimeline) m_studioTimeline->addClip(t);
                    m_projectTracks.push_back(t);
                    ++added;
                }
                if (added > 0) {
                    if (m_navBar && m_timeline) {
                        std::vector<TimelineTrackBlock*> blockPtrs;
                        for (int i = 0; i < m_timeline->getTrackCount(); ++i)
                            blockPtrs.push_back(&m_timeline->getBlock(i));
                        m_navBar->setTracks(blockPtrs);
                    }
                    updateToolbarInfo();
                    spdlog::info("[MixLab] onAddTracks: added {} track(s)", added);
                }
            });
    };
    m_transportBar->onHarmonize = [this]() { onHarmonize(); };
    m_transportBar->onMashupAI  = [this]() { if (m_studioTimeline) m_studioTimeline->runMashupAI();  };
    m_transportBar->onMegamixAI = [this]() { if (m_studioTimeline) m_studioTimeline->runMegamixAI(); };
    m_transportBar->onMedleyAI  = [this]() { if (m_studioTimeline) m_studioTimeline->runMedleyAI();  };
    m_transportBar->onRemixAI   = [this]() { if (m_studioTimeline) m_studioTimeline->runRemixAI();   };
    m_transportBar->onUndo = [this]() {
        if (m_studioTimeline) m_studioTimeline->undo();
        m_undoManager.undo();
    };
    m_transportBar->onRedo = [this]() {
        if (m_studioTimeline) m_studioTimeline->redo();
        m_undoManager.redo();
    };
    m_transportBar->onSplit = [this]() {
        if (m_studioTimeline) m_studioTimeline->splitAtPlayhead();
    };
    m_transportBar->onToggleLive = [this]() {
        if (!m_studioTimeline) return;
        const bool enable = m_transportBar->m_liveBtn
                            ? m_transportBar->m_liveBtn->getToggleState()
                            : false;
        m_studioTimeline->setLivePlaybackEnabled(enable);
    };
    m_transportBar->onSolo = [this]() {
        if (!m_studioTimeline) return;
        const int sel = m_studioTimeline->getSelectedIndex();
        if (sel < 0) return;
        if (auto* c = m_studioTimeline->getClip(sel)) {
            const bool wasMuted = c->isMuted();
            for (int i = 0; i < m_studioTimeline->getNumClips(); ++i) {
                if (auto* k = m_studioTimeline->getClip(i)) k->setMuted(i != sel || wasMuted);
            }
            m_studioTimeline->repaint();
            spdlog::info("[Studio] Solo clip {}", sel);
        }
    };
    m_transportBar->onReplayTrans = [this]() {
        if (m_studioTimeline) {
            m_studioTimeline->jumpToPrevTransition();
            if (!m_studioTimeline->isPlaying()) m_studioTimeline->play();
        }
    };
    m_transportBar->onZoomIn  = [this]() { if (m_studioTimeline) m_studioTimeline->zoomIn(); };
    m_transportBar->onZoomOut = [this]() { if (m_studioTimeline) m_studioTimeline->zoomOut(); };
    addAndMakeVisible(*m_transportBar);

    m_mixerA = std::make_unique<MixerStrip>("DECK A");
    m_mixerB = std::make_unique<MixerStrip>("DECK B");
    m_tempoLane = std::make_unique<TempoLane>();

    m_timeline = std::make_unique<TimelineComponent>();
    m_timeline->addListener(&m_listenerAdapters->timeline);

    m_studioTimeline = std::make_unique<Studio::StudioTimeline>();
    addAndMakeVisible(*m_studioTimeline);
    m_studioTimeline->toFront(false);

    m_inspector = std::make_unique<ClipInspectorPanel>();
    m_inspector->setStudioTimeline(m_studioTimeline.get());
    addAndMakeVisible(*m_inspector);
    if (m_listenerAdapters)
        m_studioTimeline->addListener(&m_listenerAdapters->studio);

    m_tabs = std::make_unique<StudioTabs>();
    m_trackInfoPanel = std::make_unique<TrackInfoPanel>();
    m_effectsPanel = std::make_unique<EffectsPanel>();
    m_masterPanel = std::make_unique<MasterPanel>();
    if (m_masterPanel && m_studioTimeline)
        m_masterPanel->setStudioTimeline(m_studioTimeline.get());

    // Layered MasterPanel wiring: forward gain/compressor to StudioTimeline too,
    if (m_masterPanel && m_masterPanel->m_masterGainSlider) {
        auto* s = m_masterPanel->m_masterGainSlider.get();
        s->onValueChange = [this, s]() {
            const float v = static_cast<float>(s->getValue());
            if (g_serviceLocator) {
                if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>())
                    engine->setMasterVolume(v);
            }
            if (m_studioTimeline) m_studioTimeline->setMasterVolume(juce::jlimit(0.0f, 2.0f, v));
        };
        if (m_studioTimeline)
            s->setValue(m_studioTimeline->getMasterVolume(), juce::dontSendNotification);
    }
    if (m_masterPanel && m_masterPanel->m_compressorSlider) {
        auto* s = m_masterPanel->m_compressorSlider.get();
        s->onValueChange = [this, s]() {
            const float a = static_cast<float>(s->getValue());
            if (g_serviceLocator) {
                if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>())
                    engine->setMasterCompressorAmount(a);
            }
            if (m_studioTimeline) m_studioTimeline->setMasterCompressorAmount(juce::jlimit(0.0f, 10.0f, a));
        };
    }

    if (m_trackInfoPanel && m_trackInfoPanel->m_pitchModeCombo) {
        m_trackInfoPanel->m_pitchModeCombo->onChange = [this]() {
            if (!m_trackInfoPanel || !m_trackInfoPanel->m_pitchModeCombo) return;
            const int id = m_trackInfoPanel->m_pitchModeCombo->getSelectedId();
            const PitchMode pm = (id == 2) ? PitchMode::RePitch
                               : (id == 3) ? PitchMode::BeatSlice
                               : (id == 4) ? PitchMode::FormantCorrection
                                           : PitchMode::Vinyl;
            m_pitchMode.store(pm);
            applyPitchModeFromUI();
            Prefs::setInt("mixlab.pitchModeId", id);
            spdlog::info("[Studio] PitchMode -> {}",
                         pm == PitchMode::Vinyl ? "Vinyl"
                         : pm == PitchMode::RePitch ? "RePitch"
                         : pm == PitchMode::BeatSlice ? "BeatSlice"
                                                      : "FormantCorrection");
        };
        const int pmId = Prefs::getInt("mixlab.pitchModeId", 1);
        if (pmId >= 1 && pmId <= m_trackInfoPanel->m_pitchModeCombo->getNumItems())
            m_trackInfoPanel->m_pitchModeCombo->setSelectedId(pmId, juce::sendNotificationSync);
    }

    if (m_effectsPanel && m_effectsPanel->m_pitchSlider) {
        auto* s = m_effectsPanel->m_pitchSlider.get();
        s->setRange(-12.0, 12.0, 0.01);
        s->setValue(0.0, juce::dontSendNotification);
        s->onValueChange = [this]() {
            if (!m_effectsPanel || !m_effectsPanel->m_pitchSlider) return;
            const double st = m_effectsPanel->m_pitchSlider->getValue();
            m_pitchSemitones.store(st);
            applyPitchModeFromUI();
        };
    }

    if (m_trackInfoPanel && m_trackInfoPanel->m_beatGridModeCombo) {
        m_trackInfoPanel->m_beatGridModeCombo->onChange = [this]() {
            if (!m_trackInfoPanel || !m_trackInfoPanel->m_beatGridModeCombo) return;
            Prefs::setInt("mixlab.beatGridModeId", m_trackInfoPanel->m_beatGridModeCombo->getSelectedId());
            if (!m_timeline || m_timeline->getTrackCount() == 0) return;
            int idx = (m_timeline->m_selectedBlock >= 0 &&
                       m_timeline->m_selectedBlock < m_timeline->getTrackCount())
                          ? m_timeline->m_selectedBlock : 0;
            regenerateBeatGrid(idx);
        };
        const int bgId = Prefs::getInt("mixlab.beatGridModeId", 3);
        if (bgId >= 1 && bgId <= m_trackInfoPanel->m_beatGridModeCombo->getNumItems())
            m_trackInfoPanel->m_beatGridModeCombo->setSelectedId(bgId, juce::dontSendNotification);
    }

    // L'onglet "Transition" est retiré — toute l'édition se fait à la souris


    class ZoomTab : public juce::Component {
    public:
        explicit ZoomTab(MixLabView* owner) : m_owner(owner) {
            m_label = std::make_unique<juce::Label>("zl", BM_TJ("studio.zoomLevel"));
            m_label->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*m_label);

            m_zoomSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            m_zoomSlider->setRange(0.1, 50.0, 0.05);
            m_zoomSlider->setValue(m_owner && m_owner->m_studioTimeline
                                       ? m_owner->m_studioTimeline->getZoom() : 1.0,
                                   juce::dontSendNotification);
            m_zoomSlider->setTextValueSuffix(" x");
            m_zoomSlider->onValueChange = [this]() {
                if (m_owner && m_owner->m_studioTimeline)
                    m_owner->m_studioTimeline->setZoom(m_zoomSlider->getValue());
            };
            addAndMakeVisible(*m_zoomSlider);

            auto makeBtn = [this](const juce::String& t, double factor) {
                auto b = std::make_unique<juce::TextButton>(t);
                b->onClick = [this, factor]() {
                    if (!m_owner || !m_owner->m_studioTimeline) return;
                    const double newLevel = juce::jlimit(0.1, 50.0,
                        m_owner->m_studioTimeline->getZoom() * factor);
                    m_zoomSlider->setValue(newLevel);
                };
                addAndMakeVisible(*b);
                return b;
            };
            m_zoomInBtn  = makeBtn("Zoom +", 2.0);
            m_zoomOutBtn = makeBtn("Zoom -", 0.5);
            m_fitBtn     = std::make_unique<juce::TextButton>("Ajuster");
            m_fitBtn->onClick = [this]() {
                if (m_owner && m_owner->m_studioTimeline)
                    m_owner->m_studioTimeline->fitView();
            };
            addAndMakeVisible(*m_fitBtn);
        }
        void resized() override {
            int y = 12;
            m_label->setBounds(12, y, 300, 22); y += 24;
            m_zoomSlider->setBounds(12, y, getWidth() - 24, 24); y += 30;
            m_zoomOutBtn->setBounds(12, y, 80, 28);
            m_zoomInBtn->setBounds(100, y, 80, 28);
            m_fitBtn->setBounds(188, y, 100, 28);
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xFF080810));

            auto r = getLocalBounds();
            r.removeFromTop(72);
            if (r.getHeight() < 30 || r.getWidth() < 40) return;

            const int sel = (m_owner && m_owner->m_timeline)
                                ? m_owner->m_timeline->getSelectedBlock() : -1;
            const int count = (m_owner && m_owner->m_timeline)
                                  ? m_owner->m_timeline->getTrackCount() : 0;
            if (count <= 0) {
                g.setColour(juce::Colour(0xFF475569));
                g.setFont(juce::Font(12.0f));
                g.drawText(BM_TJ("studio.noTracksDrag"), r, juce::Justification::centred);
                return;
            }
            const int idx = (sel >= 0 && sel < count) ? sel : 0;
            const auto& trk = m_owner->m_timeline->getBlock(idx).getTrack();

            const auto* peaks = getCachedPeaksShared(trk.filePath, this);
            auto waveR = r.reduced(12, 8);
            const float midLine = waveR.getCentreY();

            g.setColour(juce::Colour(0xFF0A0A12));
            g.fillRoundedRectangle(waveR.toFloat(), 4.0f);

            if (peaks && peaks->segmentCount > 0) {
                const int segs = peaks->segmentCount;
                const bool hasColor = !peaks->lowFreq.empty();
                float globalMax = 0.0001f;
                for (int s = 0; s < segs; ++s) {
                    float p = std::abs(peaks->peaksPositive[s]);
                    if (p > globalMax) globalMax = p;
                }
                const float invMax = 1.0f / globalMax;
                const int numBars = juce::jmax(60, waveR.getWidth());
                const float halfH = waveR.getHeight() * 0.5f - 2.0f;
                const float pxPerBar = (float) waveR.getWidth() / (float) numBars;

                for (int i = 0; i < numBars; ++i) {
                    const float t0 = (float) i / numBars;
                    const float t1 = (float)(i + 1) / numBars;
                    const int i0 = juce::jlimit(0, segs - 1, (int)(t0 * segs));
                    const int i1 = juce::jlimit(i0 + 1, segs, (int)(t1 * segs));
                    float peak = 0.0f;
                    float lo = 0.0f, mi = 0.0f, hi = 0.0f;
                    for (int s = i0; s < i1; ++s) {
                        peak = std::max(peak, std::abs(peaks->peaksPositive[s]));
                        if (hasColor) {
                            lo += std::abs(peaks->lowFreq[s]);
                            mi += std::abs(peaks->midFreq[s]);
                            hi += std::abs(peaks->highFreq[s]);
                        }
                    }
                    const float amp01 = juce::jlimit(0.0f, 1.0f, peak * invMax);
                    if (amp01 < 0.01f) continue;
                    const float gamma = std::pow(amp01, 0.65f);
                    const float barH = juce::jmax(1.0f, gamma * halfH);

                    float r0 = 1.0f, g0 = 0.78f, b0 = 0.27f;
                    if (hasColor) {
                        const float sc = lo + mi + hi + 1e-6f;
                        r0 = lo / sc; g0 = mi / sc; b0 = hi / sc;
                    }
                    const float bri = 0.55f + 0.45f * gamma;
                    g.setColour(juce::Colour::fromFloatRGBA(
                        juce::jlimit(0.0f, 1.0f, r0 * bri * 1.4f),
                        juce::jlimit(0.0f, 1.0f, g0 * bri * 1.2f),
                        juce::jlimit(0.0f, 1.0f, b0 * bri * 1.4f),
                        1.0f));
                    const float x = waveR.getX() + (float) i * pxPerBar;
                    const float w = juce::jmax(1.0f, pxPerBar - 0.3f);
                    g.fillRect(x, midLine - barH, w, barH);
                    g.setColour(juce::Colour::fromFloatRGBA(
                        r0 * bri * 0.7f, g0 * bri * 0.6f, b0 * bri * 0.7f, 1.0f));
                    g.fillRect(x, midLine, w, barH);
                }
                const int percent = (int) std::round(m_zoomSlider
                    ? (m_zoomSlider->getValue() / 20.0 * 100.0) : 0);
                g.setColour(juce::Colour(0xFF22D3EE));
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText(juce::String(percent) + " %",
                           waveR.removeFromTop(14), juce::Justification::centredRight);
            } else {
                auto* thumb = getOrCreateJuceThumb(trk.filePath, this);
                if (thumb && thumb->getNumChannels() > 0
                    && thumb->getNumSamplesFinished() > 0) {
                    const double totalSec = thumb->getTotalLength() > 0.0
                                                ? thumb->getTotalLength()
                                                : juce::jmax(1.0, trk.duration);
                    g.setColour(juce::Colour(0xFFFFD400));
                    thumb->drawChannels(g, waveR, 0.0, totalSec, 0.9f);
                } else {
                    g.setColour(juce::Colour(0xFF475569));
                    g.setFont(juce::Font(12.0f));
                    g.drawText("Decodage...", waveR, juce::Justification::centred);
                }
            }
        }
    private:
        MixLabView* m_owner;
        std::unique_ptr<juce::Label> m_label;
        std::unique_ptr<juce::Slider> m_zoomSlider;
        std::unique_ptr<juce::TextButton> m_zoomInBtn, m_zoomOutBtn, m_fitBtn;
    };

    class PlaylistTab : public juce::Component {
    public:
        explicit PlaylistTab(MixLabView* owner) : m_owner(owner) {}
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xFF080810));
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(13.0f, juce::Font::bold));
            g.drawText(BM_TJ("studio.mixTracksPrefix") + " (" +
                       juce::String((int)(m_owner ? m_owner->m_projectTracks.size() : 0)) +
                       ")", 12, 8, getWidth() - 24, 22, juce::Justification::centredLeft);

            if (!m_owner || m_owner->m_projectTracks.empty()) {
                g.setColour(juce::Colour(0xFF94A3B8));
                g.setFont(juce::Font(11.0f));
                g.drawText(BM_TJ("studio.noTracksDrag"),
                           12, 40, getWidth() - 24, 20, juce::Justification::centredLeft);
                return;
            }

            int y = 36;
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xFFA855F7));
            g.drawText("#",        12, y, 24, 16, juce::Justification::centredLeft);
            g.drawText("TITRE",    40, y, 200, 16, juce::Justification::centredLeft);
            g.drawText("ARTISTE",  245, y, 160, 16, juce::Justification::centredLeft);
            g.drawText("BPM",      410, y, 50, 16, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("DURÉE"), 465, y, 60, 16, juce::Justification::centredLeft);
            y += 20;

            g.setFont(juce::Font(11.0f));
            for (size_t i = 0; i < m_owner->m_projectTracks.size(); ++i) {
                const auto& t = m_owner->m_projectTracks[i];
                g.setColour(i % 2 == 0 ? juce::Colour(0xFF1A1A24) : juce::Colour(0xFF141420));
                g.fillRect(8, y - 2, getWidth() - 16, 20);
                g.setColour(juce::Colours::white);
                g.drawText(juce::String((int)i + 1), 12, y, 24, 16, juce::Justification::centredLeft);
                g.drawText(juce::String(t.title),    40, y, 200, 16, juce::Justification::centredLeft, true);
                g.drawText(juce::String(t.artist),   245, y, 160, 16, juce::Justification::centredLeft, true);
                if (t.bpm > 0) g.drawText(juce::String(t.bpm, 1), 410, y, 50, 16, juce::Justification::centredLeft);
                const int mins = (int)(t.duration / 60);
                const int secs = (int)t.duration % 60;
                g.drawText(juce::String::formatted("%d:%02d", mins, secs),
                           465, y, 60, 16, juce::Justification::centredLeft);
                y += 20;
                if (y > getHeight() - 20) break;
            }
        }
    private:
        MixLabView* m_owner;
    };

    class SamplesTab : public juce::Component,
                       public juce::FileDragAndDropTarget {
    public:
        SamplesTab() {
            for (int i = 0; i < kNumSlots; ++i) {
                m_labels[i] = std::make_unique<juce::Label>(
                    "pad" + juce::String(i),
                    juce::String("PAD ") + juce::String(i + 1));
                m_labels[i]->setJustificationType(juce::Justification::centred);
                m_labels[i]->setColour(juce::Label::textColourId, juce::Colour(0xFFE2E8F0));
                m_labels[i]->setFont(juce::Font(11.0f, juce::Font::bold));
                addAndMakeVisible(*m_labels[i]);
            }
        }

        bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
        void filesDropped(const juce::StringArray& files, int x, int y) override {
            const int idx = slotAt(x, y);
            if (idx < 0 || idx >= kNumSlots || files.isEmpty()) return;
            m_slotPaths[idx] = files[0].toStdString();
            m_labels[idx]->setText(juce::File(files[0]).getFileNameWithoutExtension(),
                                   juce::dontSendNotification);
            if (onSampleLoaded) onSampleLoaded(idx, m_slotPaths[idx]);
            repaint();
        }
        void mouseDown(const juce::MouseEvent& e) override {
            const int idx = slotAt(e.x, e.y);
            if (idx >= 0 && idx < kNumSlots && !m_slotPaths[idx].empty()) {
                if (onSampleTrigger) onSampleTrigger(idx);
            }
        }

        void resized() override {
            auto r = getLocalBounds().reduced(12);
            const int cols = 4;
            const int rows = (kNumSlots + cols - 1) / cols;
            const int cw = r.getWidth() / cols;
            const int ch = r.getHeight() / rows;
            for (int i = 0; i < kNumSlots; ++i) {
                const int col = i % cols;
                const int row = i / cols;
                auto cell = juce::Rectangle<int>(
                    r.getX() + col * cw, r.getY() + row * ch, cw, ch).reduced(4);
                m_labels[i]->setBounds(cell);
            }
        }

        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xFF080810));
            auto r = getLocalBounds().reduced(12);
            const int cols = 4;
            const int rows = (kNumSlots + cols - 1) / cols;
            const int cw = r.getWidth() / cols;
            const int ch = r.getHeight() / rows;
            for (int i = 0; i < kNumSlots; ++i) {
                const int col = i % cols;
                const int row = i / cols;
                auto cell = juce::Rectangle<float>(
                    (float)(r.getX() + col * cw), (float)(r.getY() + row * ch),
                    (float) cw, (float) ch).reduced(4.0f);
                const bool hasSample = !m_slotPaths[i].empty();
                g.setColour(hasSample ? juce::Colour(0xFF1F2937) : juce::Colour(0xFF141420));
                g.fillRoundedRectangle(cell, 6.0f);
                g.setColour(hasSample ? juce::Colour(0xFF22D3EE) : juce::Colour(0xFF3A3A3A));
                g.drawRoundedRectangle(cell, 6.0f, 1.5f);
            }
        }

        std::function<void(int, const std::string&)> onSampleLoaded;
        std::function<void(int)>                     onSampleTrigger;

    private:
        enum { kNumSlots = 8 };
        int slotAt(int x, int y) const {
            auto r = getLocalBounds().reduced(12);
            if (!r.contains(x, y)) return -1;
            const int cols = 4;
            const int rows = (kNumSlots + cols - 1) / cols;
            const int cw = r.getWidth() / cols;
            const int ch = r.getHeight() / rows;
            const int col = (x - r.getX()) / std::max(1, cw);
            const int row = (y - r.getY()) / std::max(1, ch);
            const int idx = row * cols + col;
            return (idx >= 0 && idx < kNumSlots) ? idx : -1;
        }

        std::array<std::unique_ptr<juce::Label>, kNumSlots> m_labels;
        std::array<std::string, kNumSlots> m_slotPaths{};
    };

    m_tabs->addTab(BM_TJ("studio.tab.zoom"),       juce::Colour(0xFF080810), new ZoomTab(this),    true);
    m_tabs->addTab(BM_TJ("studio.tab.playlist"),   juce::Colour(0xFF080810), new PlaylistTab(this), true);
    m_tabs->addTab(BM_TJ("studio.tab.track"),      juce::Colour(0xFF080810), m_trackInfoPanel.get(),   false);
    m_tabs->addTab(BM_TJ("studio.tab.effects"),    juce::Colour(0xFF080810), m_effectsPanel.get(),     false);
    m_tabs->addTab(BM_TJ("studio.tab.master"),     juce::Colour(0xFF080810), m_masterPanel.get(),      false);
    m_tabs->addTab(BM_TJ("studio.tab.samples"),    juce::Colour(0xFF080810), new SamplesTab(),     true);
    m_tabs->setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xFF080810));
    m_tabs->setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xFF2A2A38));
    addAndMakeVisible(*m_tabs);

    if (!m_provider && ::g_serviceLocator) {
        m_provider = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
    }
    m_libraryBrowser = std::make_unique<LibraryBrowserPanel>(m_provider);
    if (m_libraryBrowser) {
        juce::MessageManager::callAsync([wp = juce::Component::SafePointer<LibraryBrowserPanel>(m_libraryBrowser.get())]() {
            if (auto* p = wp.getComponent()) p->refreshResults();
        });
    }
    // Listener géré par m_listenerAdapters (pas de leak)
    m_libraryBrowser->addListener(&m_listenerAdapters->library);
    addAndMakeVisible(*m_libraryBrowser);
}

void MixLabView::onAddTrack(const Models::Track& track)
{
    m_projectTracks.push_back(track);
    if (m_studioTimeline) m_studioTimeline->addClip(track);
    if (m_timeline->getTrackCount() > 0) {
        auto& block = m_timeline->getBlock(m_timeline->getTrackCount() - 1);
        block.onClipChanged = [this]() { updateToolbarInfo(); };
    }
    if (m_navBar)
    {
        std::vector<TimelineTrackBlock*> blockPtrs;
        for (int i = 0; i < m_timeline->getTrackCount(); ++i)
            blockPtrs.push_back(&m_timeline->getBlock(i));
        m_navBar->setTracks(blockPtrs);
    }
    updateToolbarInfo();
}

void MixLabView::onNewProject()
{
    m_projectTracks.clear();
    while (m_timeline->getTrackCount() > 0)
        m_timeline->removeTrack(0);
    if (m_studioTimeline) {
        while (m_studioTimeline->getNumClips() > 0)
            m_studioTimeline->removeClip(0);
    }
    updateToolbarInfo();

    // Le LibraryBrowserPanel à droite reste indépendant du projet : il lit
    if (m_libraryBrowser) {
        if (!m_libraryBrowser->getProvider() && ::g_serviceLocator) {
            if (auto* prov = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>())
                m_libraryBrowser->setProvider(prov);
        }
        spdlog::info("[MixLab] onNewProject : provider={} — forcing refreshResults",
                     (void*)m_libraryBrowser->getProvider());
        juce::MessageManager::callAsync(
            [wp = juce::Component::SafePointer<LibraryBrowserPanel>(m_libraryBrowser.get())]() {
                if (auto* p = wp.getComponent()) {
                    p->refreshResults();
                    p->resized();   // re-layout au cas où la viewport a été détruite
                    p->repaint();
                }
            });
    }
}

void MixLabView::onSaveProject()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("studio.saveProject"),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.bmproj");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return;
            if (m_studioTimeline) m_studioTimeline->saveProject(file);

            nlohmann::json j;
            j["tracks"] = nlohmann::json::array();
            for (auto& t : m_projectTracks) {
                nlohmann::json jt;
                jt["id"] = t.id;
                jt["title"] = t.title;
                jt["filePath"] = t.filePath;
                j["tracks"].push_back(jt);
            }
            try {
                auto sidecar = file.withFileExtension("bmproj.legacy.json");
                std::ofstream out(sidecar.getFullPathName().toStdString());
                out << j.dump(2);
            } catch (...) {}
        });
}

void MixLabView::onExportMix()
{
    if (m_studioTimeline && m_studioTimeline->getNumClips() > 0) {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export Mix WAV",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("BeatMateMix_" + juce::Time::getCurrentTime()
                                                   .formatted("%Y%m%d_%H%M%S") + ".wav"),
            "*.wav");
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc) {
                auto out = fc.getResult();
                if (out == juce::File{}) return;
                if (out.getFileExtension().isEmpty()) out = out.withFileExtension("wav");
                std::thread([this, out]() {
                    m_studioTimeline->exportMixToWav(out);
                    juce::MessageManager::callAsync([out]() {
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon, "Export OK",
                            "Mix WAV exporte:\n" + out.getFullPathName(), "OK");
                    });
                }).detach();
            });
        return;
    }

    if (!m_timeline || m_timeline->getTrackCount() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("studio.exportMix"),
            BM_TJ("studio.noTracksTimeline"),
            "OK");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("studio.exportMixWav"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile("BeatMateMix_" + juce::Time::getCurrentTime()
                                               .formatted("%Y%m%d_%H%M%S") + ".wav"),
        "*.wav");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto outFile = fc.getResult();
            if (outFile == juce::File{}) return;
            if (outFile.getFileExtension().isEmpty())
                outFile = outFile.withFileExtension("wav");

            double timelineEndSec = 0.0;
            for (int i = 0; i < m_timeline->getTrackCount(); ++i) {
                auto& b = m_timeline->getBlock(i);
                const double windowLen =
                    (b.getAudioEndSec() > 0.0 ? b.getAudioEndSec() : b.getTrack().duration)
                    - b.getAudioStartSec();
                timelineEndSec = std::max(timelineEndSec, b.getStartTime() + windowLen);
            }
            if (timelineEndSec <= 0.0) return;

            auto outPathStr = outFile.getFullPathName().toStdString();
            auto* tl = m_timeline.get();
            std::thread([tl, outPathStr, timelineEndSec]() {
                const double sr = 44100.0;
                const int ch = 2;
                const int64_t totalFrames =
                    static_cast<int64_t>(std::ceil(timelineEndSec * sr)) + 2;
                juce::AudioBuffer<float> mix(ch, static_cast<int>(totalFrames));
                mix.clear();

                for (int i = 0; i < tl->getTrackCount(); ++i) {
                    auto& block  = tl->getBlock(i);
                    const auto&  trk = block.getTrack();
                    Core::AudioFileReader reader;
                    auto src = reader.readFile(trk.filePath);
                    if (!src) {
                        spdlog::warn("[Export] skip block {} : decode failed ({})",
                                     i, trk.filePath);
                        continue;
                    }
                    const int    srcCh   = src->getChannels();
                    const int    srcSR   = src->getSampleRate();
                    const float* srcData = src->getRawData();
                    const size_t srcFrames = src->getTotalSamples() / (srcCh > 0 ? srcCh : 1);

                    const double aStart = block.getAudioStartSec();
                    const double aEnd   = block.getAudioEndSec() > 0.0
                        ? block.getAudioEndSec() : trk.duration;
                    const int64_t startFrame = static_cast<int64_t>(aStart * srcSR);
                    const int64_t endFrame   = static_cast<int64_t>(aEnd   * srcSR);
                    const int64_t sliceFrames = std::min<int64_t>(
                        endFrame - startFrame,
                        static_cast<int64_t>(srcFrames) - startFrame);
                    if (sliceFrames <= 0) continue;

                    const double blockStartOnMix = block.getStartTime();
                    const int64_t dstStart =
                        static_cast<int64_t>(blockStartOnMix * sr);
                    const double resampleRatio = static_cast<double>(srcSR) / sr;

                    const int fadeFrames = static_cast<int>(0.5 * sr);
                    for (int64_t k = 0; k < static_cast<int64_t>(sliceFrames / resampleRatio); ++k) {
                        const int64_t dst = dstStart + k;
                        if (dst < 0 || dst >= totalFrames) continue;
                        const int64_t srcPos = startFrame +
                            static_cast<int64_t>(static_cast<double>(k) * resampleRatio);
                        if (srcPos >= static_cast<int64_t>(srcFrames)) break;

                        float gain = 1.0f;
                        if (i > 0 && k < fadeFrames)
                            gain = static_cast<float>(k) / static_cast<float>(fadeFrames);

                        for (int c = 0; c < ch; ++c) {
                            const int useSrcCh = (c < srcCh) ? c : (srcCh - 1);
                            const float s = srcData[srcPos * srcCh + useSrcCh] * gain;
                            mix.addSample(c, static_cast<int>(dst), s);
                        }
                    }
                    spdlog::info("[Export] block {} ({}) rendered : {} frames",
                                 i, trk.title, sliceFrames);
                }

                // Normalise to -1 dBFS to avoid clipping after additive mix.
                float peak = 0.0f;
                for (int c = 0; c < ch; ++c) {
                    const float* d = mix.getReadPointer(c);
                    for (int k = 0; k < mix.getNumSamples(); ++k)
                        peak = std::max(peak, std::abs(d[k]));
                }
                if (peak > 0.0f) {
                    const float target = std::pow(10.0f, -1.0f / 20.0f);
                    if (peak > target)
                        mix.applyGain(target / peak);
                }

                juce::String outPathJuce(outPathStr);
                juce::File outFile2 { outPathJuce };
                outFile2.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::FileOutputStream> stream(outFile2.createOutputStream());
                if (!stream) {
                    spdlog::error("[Export] cannot open {}", outPathStr);
                    return;
                }
                juce::StringPairArray meta;
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wav.createWriterFor(stream.get(), sr, ch, 16, meta, 0));
                if (!writer) {
                    spdlog::error("[Export] WAV writer init failed");
                    return;
                }
                stream.release();
                const bool ok = writer->writeFromAudioSampleBuffer(
                    mix, 0, mix.getNumSamples());
                writer.reset();
                spdlog::info("[Export] WAV written ({}): {} frames, ok={}",
                             outPathStr, mix.getNumSamples(), ok);

                juce::MessageManager::callAsync([outPathStr, ok]() {
                    juce::AlertWindow::showMessageBoxAsync(
                        ok ? juce::MessageBoxIconType::InfoIcon
                           : juce::MessageBoxIconType::WarningIcon,
                        ok ? "Export Mix OK" : "Export Mix Failed",
                        juce::String(outPathStr), "OK");
                });
            }).detach();

            m_listeners.call([](Listener& l) { l.mixExportRequested(); });
        });
}

void MixLabView::onExportAbleton()
{
    if (m_studioTimeline && m_studioTimeline->getNumClips() > 0) {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export Ableton .als",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("BeatMateMix_" + juce::Time::getCurrentTime()
                                                   .formatted("%Y%m%d_%H%M%S") + ".als"),
            "*.als");
        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, chooser](const juce::FileChooser& fc) {
                auto out = fc.getResult();
                if (out == juce::File{}) return;
                if (out.getFileExtension().isEmpty()) out = out.withFileExtension("als");
                m_studioTimeline->exportToAbleton(out);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "Ableton .als",
                    "Export OK : " + out.getFullPathName(), "OK");
            });
        return;
    }

    if (!m_timeline || m_timeline->getTrackCount() == 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("studio.exportAbleton"),
            BM_TJ("studio.noTracksTimelineShort"), "OK");
        return;
    }
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("studio.exportToAbleton"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile("BeatMateMix_" + juce::Time::getCurrentTime()
                                               .formatted("%Y%m%d_%H%M%S") + ".als"),
        "*.als");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto outFile = fc.getResult();
            if (outFile == juce::File{}) return;

            std::vector<Models::Track> djTracks;
            std::vector<double>        startTimes;
            double masterBpm = 0.0;
            for (int i = 0; i < m_timeline->getTrackCount(); ++i) {
                auto& b = m_timeline->getBlock(i);
                djTracks.push_back(b.getTrack());
                startTimes.push_back(b.getStartTime());
                if (masterBpm <= 0.0 && b.getTrack().bpm > 0.0) masterBpm = b.getTrack().bpm;
            }
            if (masterBpm <= 0.0) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    juce::String::fromUTF8(u8"BPM inconnu"),
                    juce::String::fromUTF8(u8"Aucune piste de la timeline n'a de BPM analysé. Analysez les pistes avant l'export Ableton."),
                    "OK");
                return;
            }
            const bool ok = Services::Export::AbletonExportService()
                                .exportFromDJSet(outFile.getFullPathName().toStdString(),
                                                 djTracks, startTimes, masterBpm);
            juce::AlertWindow::showMessageBoxAsync(
                ok ? juce::MessageBoxIconType::InfoIcon
                   : juce::MessageBoxIconType::WarningIcon,
                "Ableton .als",
                juce::String(ok ? "Export OK : " : "Export failed : ")
                    + outFile.getFullPathName(),
                "OK");
        });
}

void MixLabView::onExportPackage(const juce::String& platformName)
{
    if (!m_timeline || m_timeline->getTrackCount() == 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("studio.package") + " " + platformName,
            BM_TJ("studio.noTracksTimelineShort"), "OK");
        return;
    }
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("studio.outputFolder") + " " + platformName,
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode
          | juce::FileBrowserComponent::canSelectDirectories,
        [this, platformName, chooser](const juce::FileChooser& fc) {
            auto outDir = fc.getResult();
            if (outDir == juce::File{}) return;
            outDir.createDirectory();

            juce::String tracklist;
            tracklist << "BeatMate " << platformName << " tracklist\n"
                      << "Generated "
                      << juce::Time::getCurrentTime().toISO8601(true) << "\n\n";
            for (int i = 0; i < m_timeline->getTrackCount(); ++i) {
                auto& b = m_timeline->getBlock(i);
                const auto& t = b.getTrack();
                const int tSec = static_cast<int>(b.getStartTime());
                tracklist << juce::String::formatted("%02d. [%02d:%02d] ",
                                                     i + 1, tSec / 60, tSec % 60)
                          << t.artist << " - " << t.title << "\n";
            }
            outDir.getChildFile("tracklist.txt").replaceWithText(tracklist);

            if (platformName == "YouTube") {
                juce::Image thumb(juce::Image::RGB, 1280, 720, true);
                juce::Graphics g(thumb);
                g.fillAll(juce::Colour(0xFF1E1E2E));
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(72.0f, juce::Font::bold));
                g.drawText("BeatMate Mix", thumb.getBounds().reduced(80),
                           juce::Justification::centred, true);
                juce::PNGImageFormat png;
                juce::File pngFile = outDir.getChildFile("thumbnail.png");
                pngFile.deleteFile();
                juce::FileOutputStream pngStream(pngFile);
                if (pngStream.openedOk())
                    png.writeImageToStream(thumb, pngStream);
            }

            juce::String readme;
            readme << "BeatMate package for " << platformName << "\n"
                   << "===============================================\n\n"
                   << "Contenu :\n"
                   << "  - tracklist.txt : liste des pistes avec timecodes\n"
                   << "  - mix.wav       : rendu offline (lancez Exporter > WAV pour le produire)\n";
            if (platformName == "YouTube")
                readme << "  - thumbnail.png : placeholder 1280x720 à remplacer\n";
            readme << "\nUpload manuel sur " << platformName
                   << " recommandé (les client_id OAuth ne sont pas embarqués).\n";
            outDir.getChildFile("README.txt").replaceWithText(readme);

            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                BM_TJ("studio.package") + " " + platformName,
                BM_TJ("studio.package.written") + " " + outDir.getFullPathName() + "\n\n" + BM_TJ("studio.package.thenExport"),
                "OK");
            spdlog::info("[Export] {} package -> {}",
                         platformName.toStdString(),
                         outDir.getFullPathName().toStdString());
        });
}

void MixLabView::onRecord()
{
    if (m_recorder && m_recorder->isRecording()) {
        const auto path = m_recorder->stopRecording();
        if (!path.empty()) {
            const auto cueSheetPath = juce::File(juce::String(path))
                                          .withFileExtension("cue").getFullPathName();
            m_recorder->exportToFile(cueSheetPath.toStdString(), "cue");
        }
        m_lastRecordingPath = juce::String(path);
        spdlog::info("[Studio] Recording stopped â†’ {}", path);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("studio.recordingDone"),
            BM_TJ("studio.mixRecordedAt") + "\n" + juce::String(path) + "\n" + BM_TJ("studio.cueSheetWritten"),
            "OK");
        return;
    }

    auto defaultDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                          .getChildFile("BeatMate Recordings");
    defaultDir.createDirectory();
    auto defaultName = juce::String("BeatMateMix_") +
                       juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".wav";
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("studio.recordMixTo"),
        defaultDir.getChildFile(defaultName),
        "*.wav");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File{}) return;

            if (!m_recorder) m_recorder = std::make_unique<Core::LiveSetRecorder>();
            const double sr = m_audioEngine ? m_audioEngine->getSampleRate() : 44100.0;
            const int channels = m_audioEngine ? m_audioEngine->getChannels() : 2;
            m_recorder->setSetName(file.getFileNameWithoutExtension().toStdString());
            if (!m_recorder->startRecording(file.getFullPathName().toStdString(),
                                            "wav", (int)sr, channels)) {
                spdlog::error("[Studio] Recording : failed to start {}", file.getFullPathName().toStdString());
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    BM_TJ("studio.recordError"),
                    BM_TJ("studio.recordError.detail") + "\n" + file.getFullPathName(),
                    "OK");
                return;
            }
            spdlog::info("[Studio] Recording armed â†’ {} ({} Hz, {} ch)",
                         file.getFullPathName().toStdString(), sr, channels);
        });
}

void MixLabView::togglePlayPause()
{
    spdlog::info("[MixLab] togglePlayPause: m_isPlaying={}", m_isPlaying);
    if (m_isPlaying) stopMix();
    else playMix();
}

static Core::AudioEngine* resolveAudioEngine()
{
    if (!::g_serviceLocator) return nullptr;
    return ::g_serviceLocator->tryGet<Core::AudioEngine>();
}

bool MixLabView::loadBlockIntoPlayer(int idx, double offsetSeconds)
{
    if (idx < 0 || idx >= (int)m_projectTracks.size()) return false;
    const auto& track = m_projectTracks[idx];
    if (track.filePath.empty()) {
        spdlog::warn("[Studio] playMix: block {} has empty file path", idx);
        return false;
    }
    if (!juce::File(track.filePath).existsAsFile()) {
        spdlog::warn("[Studio] playMix: block {} file not found: {}", idx, track.filePath);
        return false;
    }

    // AudioFileReader is a singleton service with an internal decode cache.
    auto* reader = ::g_serviceLocator ? ::g_serviceLocator->tryGet<Core::AudioFileReader>() : nullptr;
    if (!reader) {
        spdlog::error("[Studio] playMix: AudioFileReader service unavailable");
        return false;
    }

    auto audioTrack = reader->readFile(track.filePath);
    if (!audioTrack) {
        spdlog::error("[Studio] playMix: could not decode {}", track.filePath);
        return false;
    }

    if (!m_player) m_player = std::make_unique<Core::AudioPlayer>();
    m_player->loadTrack(audioTrack);
    if (offsetSeconds > 0.0) m_player->seek(offsetSeconds);
    m_currentPlayingBlock = idx;

    if (!m_pitchProcessor) m_pitchProcessor = std::make_unique<Core::RealtimePitchProcessor>();
    m_pitchProcessor->setPlayer(m_player.get());
    m_pitchProcessor->prepare(audioTrack->getSampleRate(), audioTrack->getChannels());

    std::vector<double> beats;
    if (track.bpm > 1.0 && track.duration > 0.0) {
        const double period = 60.0 / track.bpm;
        beats.reserve(static_cast<size_t>(track.duration / period) + 4);
        for (double t = 0.0; t < track.duration; t += period) beats.push_back(t);
    }
    m_pitchProcessor->setBeatPositions(std::move(beats));
    applyPitchModeFromUI();

    spdlog::info("[Studio] Loaded block {} ('{}') @ {:.2f}s", idx, track.title, offsetSeconds);
    return true;
}


double MixLabView::computeTransitionDurationSec(int blockIdx) const
{
    if (!m_timeline) return 0.0;
    const auto& trans = m_timeline->getTransitions();
    if (blockIdx < 0 || blockIdx >= (int)trans.size()) return 0.0;
    const auto& t = trans[blockIdx];
    if (t.durationSeconds > 0.0)
        return juce::jlimit(0.5, 30.0, t.durationSeconds);

    double bpm = 0.0;
    if (blockIdx < (int)m_projectTracks.size())
        bpm = m_projectTracks[blockIdx].bpm;
    if (bpm <= 1.0) return 8.0;
    const double beats = juce::jmax(1, t.durationBars) * 4.0;
    return juce::jlimit(0.5, 30.0, (beats * 60.0) / bpm);
}

bool MixLabView::startCrossfade(int nextBlockIdx, double posSec)
{
    if (m_crossfading.load()) return false;
    if (nextBlockIdx < 0 || nextBlockIdx >= (int)m_projectTracks.size()) return false;
    const auto& track = m_projectTracks[nextBlockIdx];
    if (track.filePath.empty() || !juce::File(track.filePath).existsAsFile())
        return false;

    auto* reader = ::g_serviceLocator ? ::g_serviceLocator->tryGet<Core::AudioFileReader>() : nullptr;
    if (!reader) return false;
    auto audioTrack = reader->readFile(track.filePath);
    if (!audioTrack) return false;

    if (!m_playerB) m_playerB = std::make_unique<Core::AudioPlayer>();
    m_playerB->loadTrack(audioTrack);
    m_playerB->seek(0.0);
    // Gain control happens in the audio callback (sample-accurate). Both
    m_playerB->setVolume(1.0f);
    m_playerB->play();

    if (m_player) m_player->setVolume(1.0f);

    int curveIdx = 0;
    if (m_timeline) {
        const auto& tl = m_timeline->getTransitions();
        if (m_currentPlayingBlock >= 0 && m_currentPlayingBlock < (int)tl.size())
            curveIdx = static_cast<int>(tl[m_currentPlayingBlock].curve);
    }

    const double durSec = computeTransitionDurationSec(m_currentPlayingBlock);
    m_crossfadeStartPosA.store(posSec);
    m_crossfadeDurSec.store(durSec);
    m_crossfadeCurve.store(curveIdx);
    m_crossfading.store(true);

    spdlog::info("[Studio] Crossfade BEGIN: A={} -> B={}, curve={}, dur={:.2f}s @ posA={:.2f}s",
                 m_currentPlayingBlock, nextBlockIdx, curveIdx, durSec, posSec);
    return true;
}

void MixLabView::computeXfadeGains(double t, float& gA, float& gB) const
{
    t = juce::jlimit(0.0, 1.0, t);
    bool customUsed = false;
    EndShape leftEnd  = EndShape::Soft;
    EndShape rightEnd = EndShape::Soft;
    if (m_timeline) {
        const auto& trans = m_timeline->getTransitions();
        if (m_currentPlayingBlock >= 0 && m_currentPlayingBlock < (int)trans.size()) {
            leftEnd  = trans[m_currentPlayingBlock].leftEnd;
            rightEnd = trans[m_currentPlayingBlock].rightEnd;
            const auto& pts = trans[m_currentPlayingBlock].crossfadePoints;
            if (!pts.empty()) {
                const float u = (float)t;
                auto sample = [&]() -> float {
                    if ((double)u <= pts.front().time) return (float)pts.front().value;
                    for (size_t k = 1; k < pts.size(); ++k) {
                        if ((double)u <= pts[k].time) {
                            const auto& p0 = pts[k - 1];
                            const auto& p1 = pts[k];
                            const float span = (float)(p1.time - p0.time);
                            if (span <= 1.0e-6f) return p1.value;
                            const float v = (u - (float)p0.time) / span;
                            return juce::jmap(v, 0.0f, 1.0f, p0.value, p1.value);
                        }
                    }
                    return (float)pts.back().value;
                };
                gA = juce::jlimit(0.0f, 1.0f, sample());
                gB = 1.0f - gA;
                customUsed = true;
            }
        }
    }
    if (!customUsed) {
        const float ft = (float)t;
        switch (m_crossfadeCurve.load()) {
            case 0: gA = 1.0f - ft;                gB = ft; break;
            case 1: gA = std::cos(ft * 1.5707963f);
                    gB = std::sin(ft * 1.5707963f); break;
            case 2: { float a = 0.5f * (1.0f + std::cos(ft * 3.1415927f));
                      gA = a; gB = 1.0f - a; break; }
            case 3: gA = std::sqrt(juce::jmax(0.0f, 1.0f - ft));
                    gB = std::sqrt(ft);             break;
            default: gA = 1.0f - ft; gB = ft;       break;
        }
    }

    constexpr double kEdge = 0.05;
    if (t <= kEdge && leftEnd == EndShape::Cut) {
        gA = 1.0f; gB = 0.0f;
    } else if (t >= 1.0 - kEdge && rightEnd == EndShape::Cut) {
        gA = 0.0f; gB = 1.0f;
    }
}

void MixLabView::updateCrossfadeGains(double posSec)
{
    // Sample-accurate gain is now applied directly inside the audio callback
    if (!m_crossfading.load()) return;
    const double dur = m_crossfadeDurSec.load();
    if (dur <= 0.0) { finalizeCrossfade(); return; }
    const double t = juce::jlimit(0.0, 1.0,
                                  (posSec - m_crossfadeStartPosA.load()) / dur);
    if (t >= 1.0) finalizeCrossfade();
}

void MixLabView::finalizeCrossfade()
{
    if (!m_crossfading.load() && !m_playerB) return;

    spdlog::info("[Studio] Crossfade END: promoting B -> A (block {} -> {})",
                 m_currentPlayingBlock, m_currentPlayingBlock + 1);

    // Stop the old A so its now-silent processBlock no longer runs.
    if (m_player) m_player->stop();

    m_player = std::move(m_playerB);
    m_playerB.reset();

    if (m_player) {
        m_player->setVolume(1.0f);
        if (m_pitchProcessor) {
            m_pitchProcessor->setPlayer(m_player.get());
            if (auto t = m_player->getTrack())
                m_pitchProcessor->prepare(t->getSampleRate(), t->getChannels());
        }
        // Re-arm the position callback on the new active player so end-of-block
        juce::Component::SafePointer<MixLabView> safeSelf(this);
        m_player->setPositionCallback(
            [safeSelf](double posSec) {
                juce::MessageManager::callAsync([safeSelf, posSec]() {
                    if (auto* self = safeSelf.getComponent())
                        self->syncPlayheadFromPlayer();
                });
            });
    }

    m_currentPlayingBlock += 1;
    m_crossfading.store(false);
    m_crossfadeStartPosA.store(0.0);
}

void MixLabView::handlePlaybackTick(double posSec)
{
    if (!m_player) return;
    const double duration = m_player->getDuration();

    if (!m_crossfading.load() && duration > 0.0) {
        const int next = m_currentPlayingBlock + 1;
        if (next < (int)m_projectTracks.size()) {
            const double transDur = computeTransitionDurationSec(m_currentPlayingBlock);
            if (transDur > 0.0
                && posSec >= duration - transDur
                && posSec < duration) {
                startCrossfade(next, posSec);
            }
        }
    }

    if (m_crossfading.load())
        updateCrossfadeGains(posSec);

    if (!m_crossfading.load() && duration > 0.0 && posSec >= duration - 0.05)
        onBlockFinished();
    else
        syncPlayheadFromPlayer();
}

void MixLabView::playMix()
{
    if (m_projectTracks.empty()) {
        spdlog::info("[Studio] playMix: no tracks on the timeline");
        return;
    }

    if (m_playbackState.load() == PlaybackState::Paused && m_player) {
        m_player->play();
        m_playbackState.store(PlaybackState::Playing);
        m_isPlaying = true;
        spdlog::info("[Studio] Resume playback at block {}", m_currentPlayingBlock);
        return;
    }

    const double playhead = m_timeline ? m_timeline->getPlayheadPosition() : 0.0;
    double cumTime = 0.0;
    int blockIdx = -1;
    double offsetInBlock = 0.0;
    for (int i = 0; i < (int)m_projectTracks.size(); ++i) {
        const double dur = m_projectTracks[i].duration;
        if (playhead >= cumTime && playhead < cumTime + dur) {
            blockIdx = i;
            offsetInBlock = playhead - cumTime;
            break;
        }
        cumTime += dur;
    }
    if (blockIdx < 0) { blockIdx = 0; offsetInBlock = 0.0; }

    if (!loadBlockIntoPlayer(blockIdx, offsetInBlock)) {
        spdlog::warn("[Studio] playMix: load failed, aborting");
        return;
    }

    juce::Component::SafePointer<MixLabView> safeSelf(this);
    m_player->setPositionCallback(
        [safeSelf](double posSec) {
            juce::MessageManager::callAsync([safeSelf, posSec]() {
                if (auto* self = safeSelf.getComponent())
                    self->handlePlaybackTick(posSec);
            });
        });

    m_audioEngine = resolveAudioEngine();
    if (!m_audioEngine) {
        spdlog::error("[Studio] playMix: AudioEngine service unavailable");
        return;
    }
    m_audioEngine->setCallback(
        [this](float* out, const float* /*in*/, unsigned long frames, int channels) {
            if (m_playbackState.load() == PlaybackState::Playing && m_player) {
                if (m_pitchProcessor)
                    m_pitchProcessor->processBlock(out, static_cast<int>(frames), channels);
                else
                    m_player->processBlock(out, static_cast<int>(frames), channels);

                if (m_crossfading.load() && m_playerB && m_player) {
                    const double dur = m_crossfadeDurSec.load();
                    const double posA = m_player->getPosition();
                    const double t = (dur > 0.0)
                        ? juce::jlimit(0.0, 1.0,
                                       (posA - m_crossfadeStartPosA.load()) / dur)
                        : 0.0;
                    float gA = 1.0f, gB = 0.0f;
                    computeXfadeGains(t, gA, gB);

                    static thread_local std::vector<float> bufB;
                    const int N = static_cast<int>(frames) * channels;
                    if ((int)bufB.size() < N) bufB.resize(N);
                    std::fill(bufB.begin(), bufB.begin() + N, 0.0f);
                    m_playerB->processBlock(bufB.data(), static_cast<int>(frames), channels);

                    for (int i = 0; i < N; ++i)
                        out[i] = out[i] * gA + bufB[i] * gB;
                }
            } else {
                // Silence when not playing — prevents glitches between blocks.
                std::fill(out, out + frames * channels, 0.0f);
            }
            if (m_recorder && m_recorder->isRecording()) {
                m_recorder->feedAudio(out, static_cast<int>(frames), channels);
            }
        });
    if (!m_audioEngine->isRunning()) m_audioEngine->start();

    m_player->play();
    m_playbackState.store(PlaybackState::Playing);
    m_isPlaying = true;
    spdlog::info("[Studio] Play mix from block {} @ {:.2f}s via AudioEngine", blockIdx, offsetInBlock);
}

void MixLabView::stopMix()
{
    if (m_player) m_player->pause();
    m_playbackState.store(PlaybackState::Stopped);
    m_isPlaying = false;
    // Don't unset the AudioEngine callback — leave silence in place so other
    spdlog::info("[Studio] Stop mix");
}

void MixLabView::onBlockFinished()
{
    const int next = m_currentPlayingBlock + 1;
    if (next >= (int)m_projectTracks.size()) {
        spdlog::info("[Studio] Reached end of timeline — stopping");
        stopMix();
        return;
    }
    if (!loadBlockIntoPlayer(next, 0.0)) {
        spdlog::warn("[Studio] End-of-block advance: load failed for block {}", next);
        stopMix();
        return;
    }
    if (m_player) m_player->play();
    m_playbackState.store(PlaybackState::Playing);
}

void MixLabView::syncPlayheadFromPlayer()
{
    if (!m_player || !m_timeline) return;
    double base = 0.0;
    for (int i = 0; i < m_currentPlayingBlock && i < (int)m_projectTracks.size(); ++i)
        base += m_projectTracks[i].duration;
    const double absPos = base + m_player->getPosition();
    m_timeline->setPlayheadPosition(absPos);
}

void MixLabView::applyPitchModeFromUI()
{
    if (!m_pitchProcessor) return;
    using RTMode = Core::RealtimePitchProcessor::Mode;
    const PitchMode pm = m_pitchMode.load();
    const RTMode target = (pm == PitchMode::RePitch)          ? RTMode::RePitch
                        : (pm == PitchMode::BeatSlice)        ? RTMode::BeatSlice
                        : (pm == PitchMode::FormantCorrection)? RTMode::FormantCorrection
                                                              : RTMode::Vinyl;
    m_pitchProcessor->setMode(target);
    m_pitchProcessor->setPitchSemitones(m_pitchSemitones.load());
    // Tempo ratio: the timeline engine does not expose a global tempo slider
    m_pitchProcessor->setTempoRatio(1.0);
}

void MixLabView::regenerateBeatGrid(int blockIndex)
{
    if (!m_timeline || !m_trackInfoPanel || !m_trackInfoPanel->m_beatGridModeCombo) return;
    if (blockIndex < 0 || blockIndex >= m_timeline->getTrackCount()) return;

    // ComboBox ItemID = enum value + 1 (addItem 1..5 for Manual..Rekordbox).
    const int comboId = m_trackInfoPanel->m_beatGridModeCombo->getSelectedId();
    const int modeInt = juce::jlimit(0, 4, comboId - 1);
    const auto mode = static_cast<Core::BeatGridMode>(modeInt);

    auto& block = m_timeline->getBlock(blockIndex);
    const auto track = block.getTrack();

    // Stamp the mode immediately so paintBeatGrid can color downbeats
    block.beatGridMode = modeInt;
    block.manualBeatDragIndex = -1;

    // Fixed/Manual are cheap (no audio decode) â†’ run synchronously.
    const bool needsAudio = (mode == Core::BeatGridMode::AI ||
                             mode == Core::BeatGridMode::AIFlex);
    auto runGen = [track, mode]() -> Core::BeatGridResult {
        Core::BeatGridGenerator gen;
        return gen.generateForTrack(mode, track);
    };
    auto applyResult = [this, blockIndex](Core::BeatGridResult res) {
        if (!m_timeline || blockIndex >= m_timeline->getTrackCount()) return;
        auto& b = m_timeline->getBlock(blockIndex);
        b.beatGridPositions = std::move(res.grid.beatPositions);
        b.beatGridBarPositions = std::move(res.grid.barPositions);
        b.beatGridIsVariable = res.isVariableTempo;
        if (!res.ok) {
            b.beatGridMode = static_cast<int>(res.modeUsed);
            spdlog::warn("[Studio] Beat Grid fallback: {}", res.error);
            juce::MessageManager::callAsync([err = res.error]() {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    BM_TJ("studio.beatGrid"),
                    BM_TJ("studio.beatGrid.fallback") + "\n" + err,
                    "OK");
            });
        }
        if (auto* vp = m_timeline->findChildWithID("")) (void)vp;
        m_timeline->repaint();
    };

    if (!needsAudio) {
        applyResult(runGen());
        return;
    }

    // AI / AIFlex : decode + analyse on a background thread. SafePointer
    juce::Component::SafePointer<MixLabView> safe(this);
    juce::Thread::launch([safe, blockIndex, runGen, applyResult]() {
        auto res = runGen();
        juce::MessageManager::callAsync([safe, blockIndex, res = std::move(res), applyResult]() mutable {
            if (auto* self = safe.getComponent()) {
                applyResult(std::move(res));
            }
        });
    });
}

void MixLabView::onHarmonize()
{
    if (m_projectTracks.size() < 2) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("studio.harmonize"),
            BM_TJ("studio.harmonize.min2"),
            "OK");
        return;
    }

    Services::Preparation::SetCompatibilityScorer scorer;
    auto orderedIds = scorer.autoOrder(m_projectTracks);

    if (orderedIds.size() != m_projectTracks.size()) {
        spdlog::warn("[MixLab] Harmonize: autoOrder returned {} ids for {} tracks — aborting",
                     orderedIds.size(), m_projectTracks.size());
        return;
    }

    std::vector<Models::Track> reordered;
    reordered.reserve(m_projectTracks.size());
    for (int64_t id : orderedIds) {
        auto it = std::find_if(m_projectTracks.begin(), m_projectTracks.end(),
            [id](const Models::Track& t) { return t.id == id; });
        if (it != m_projectTracks.end()) reordered.push_back(*it);
    }
    if (reordered.size() != m_projectTracks.size()) {
        spdlog::warn("[MixLab] Harmonize: some tracks lost during reorder — aborting");
        return;
    }

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        BM_TJ("studio.harmonize"),
        BM_TJ("studio.harmonize.reorderPrefix") + " " + juce::String((int)reordered.size()) + " " + BM_TJ("studio.harmonize.reorderSuffix"),
        BM_TJ("studio.harmonizeBtn"),
        BM_TJ("studio.cancel"),
        nullptr,
        juce::ModalCallbackFunction::create(
            [this, reordered = std::move(reordered)](int result) mutable {
                if (result != 1) return;

                // Stop playback before tearing down the timeline blocks.
                if (m_isPlaying) stopMix();

                m_projectTracks = std::move(reordered);

                if (m_timeline) {
                    while (m_timeline->getTrackCount() > 0)
                        m_timeline->removeTrack(0);
                    for (const auto& t : m_projectTracks)
                        m_timeline->addTrack(t);
                }

                if (m_navBar && m_timeline) {
                    std::vector<TimelineTrackBlock*> blockPtrs;
                    for (int i = 0; i < m_timeline->getTrackCount(); ++i)
                        blockPtrs.push_back(&m_timeline->getBlock(i));
                    m_navBar->setTracks(blockPtrs);
                }

                updateToolbarInfo();
                spdlog::info("[MixLab] Harmonize: reordered {} tracks", m_projectTracks.size());
            }));
}

void MixLabView::updateToolbarInfo()
{
    if (!m_toolbar) return;
    double totalDur = 0, avgBpm = 0;
    int bpmCount = 0;
    for (auto& t : m_projectTracks)
    {
        totalDur += t.duration;
        if (t.bpm > 0) { avgBpm += t.bpm; bpmCount++; }
    }
    if (bpmCount > 0) avgBpm /= bpmCount;
    m_toolbar->setBPMLabel(avgBpm);
    m_toolbar->setTotalDurationLabel(totalDur);
    if (m_studioTimeline && avgBpm > 0)
        m_studioTimeline->setMasterBpm(avgBpm);
}

void MixLabView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF050510));
}

void MixLabView::resized()
{
    int w = getWidth();
    int h = getHeight();
    int margin = 6;

    int toolbarH = 32;
    int navBarH = 22;
    int transportH = 32;
    int tempoLaneH = 50;
    int libraryH = 180;
    int tabsH = 200;
    int mixerW = 60;

    int y = margin;

    m_toolbar->setBounds(margin, y, w - margin * 2, toolbarH);
    y += toolbarH + margin;

    // Navigation bar (overview) — hidden: redundant with the Zoom tab overview.
    m_navBar->setBounds(0, 0, 0, 0);
    m_navBar->setVisible(false);

    m_transportBar->setBounds(margin, y, w - margin * 2, transportH);
    y += transportH + margin;

    m_tempoLane->setBounds(margin + mixerW * 2 + 8, y, w - margin * 2 - mixerW * 2 - 8, tempoLaneH);
    y += tempoLaneH + 2;

    int centerH = h - y - libraryH - tabsH - margin * 3;
    if (centerH < 200) centerH = 200;

    m_mixerA->setBounds(0, 0, 0, 0);
    m_mixerB->setBounds(0, 0, 0, 0);
    m_mixerA->setVisible(false);
    m_mixerB->setVisible(false);

    const int sidebarW = juce::jmax(260, w / 4);
    const int inspectorW = (m_inspector && m_inspector->isVisible()) ? 200 : 0;
    const int tlX        = margin + inspectorW + (inspectorW > 0 ? margin : 0);
    const int tlW        = w - margin * 2 - sidebarW - margin - inspectorW
                              - (inspectorW > 0 ? margin : 0);
    const auto tlBounds  = juce::Rectangle<int>(tlX, y, tlW, centerH);
    const int sidebarX   = tlX + tlW + margin;

    if (m_inspector) {
        m_inspector->setBounds(margin, y, 200, centerH);
        m_inspector->toFront(false);
    }
    if (m_studioTimeline) {
        m_studioTimeline->setBounds(tlBounds);
        m_studioTimeline->toFront(false);
        m_studioTimeline->setWantsKeyboardFocus(true);
        m_studioTimeline->setMouseClickGrabsKeyboardFocus(true);
        // Defer the focus grab so it doesn't fight the layout pass.
        juce::Component::SafePointer<juce::Component> tl(m_studioTimeline.get());
        juce::MessageManager::callAsync([tl]() {
            if (auto* c = tl.getComponent())
                if (!c->hasKeyboardFocus(true)) c->grabKeyboardFocus();
        });
    }
    if (m_libraryBrowser) {
        m_libraryBrowser->setVisible(true);
        m_libraryBrowser->setBounds(sidebarX, y, sidebarW, centerH);
        m_libraryBrowser->toFront(false);

        Services::Library::TrackDataProvider* prov = m_provider;
        if (!prov && ::g_serviceLocator)
            prov = ::g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
        if (prov && m_libraryBrowser->getProvider() != prov) {
            m_libraryBrowser->setProvider(prov);
            m_provider = prov;
        }
        m_libraryBrowser->refreshResults();
    }
    y += centerH + margin;

    m_tabs->setBounds(margin, y, w - margin * 2, tabsH);
    y += tabsH + margin;
    juce::ignoreUnused(libraryH);
}

} // namespace BeatMate::UI
