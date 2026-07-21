#include "StudioTimeline.h"

#include "../../styles/ColorPalette.h"
#include "../../../core/dsp/CompressorProcessor.h"
#include "../../../core/dsp/ReverbProcessor.h"
#include "../../../core/dsp/DelayProcessor.h"
#include "../../../core/dsp/EQProcessor.h"
#include "../../../core/dsp/FilterProcessor.h"
#include "../../../core/effects/TransitionEngine.h"
#include "../../../core/effects/CrossfadeEngine.h"
#include "../../../core/stems/DemucsStemService.h"
#include "../../../core/stems/StemSepSotaService.h"
#include "../../../core/stems/StemSidecar.h"
#include "../../../services/config/SettingsManager.h"
#include "../../../app/ServiceLocator.h"
#include "../../../services/library/PeakFileService.h"
#include "../../../services/ai/SmartTransitionGen.h"

#include <SoundTouch.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace BeatMate::UI::Studio {

namespace {

juce::AudioFormatManager& sharedFormatManager() {
    static juce::AudioFormatManager mgr;
    static bool inited = false;
    if (!inited) { mgr.registerBasicFormats(); inited = true; }
    return mgr;
}

juce::AudioThumbnailCache& sharedThumbCache() {
    static juce::AudioThumbnailCache cache{ 512 };
    return cache;
}

constexpr int    kHeaderH          = 22;
constexpr float  kEdgeHitPx        = 7.0f;
constexpr float  kFadeHitPx        = 14.0f;
constexpr float  kClipMinWidthPx   = 36.0f;
constexpr double kMinClipLengthSec = 0.25;

juce::Colour bandLow () { return juce::Colour::fromRGB(240,  60,  60); }
juce::Colour bandMid () { return juce::Colour::fromRGB( 60, 220,  90); }
juce::Colour bandHigh() { return juce::Colour::fromRGB( 70, 130, 255); }

// LRU peaks RGB en memoire ; backing .bmpk disque, generation async, ne bloque jamais le message thread.
constexpr size_t kStudioPeakCacheMax = 200;

struct StudioPeakCacheEntry {
    bool resolved { false };
    Services::Library::PeakData data;
    std::list<std::string>::iterator lruIter {};
};
static std::unordered_map<std::string, StudioPeakCacheEntry> g_studioPeakCache;
static std::list<std::string> g_studioPeakLru;
static std::mutex g_studioPeakMutex;

static std::mutex g_studioPeakSvcMutex;
static std::unique_ptr<Services::Library::PeakFileService> g_studioPeakSvc;

static Services::Library::PeakFileService& getStudioPeakSvc() {
    std::lock_guard<std::mutex> lk(g_studioPeakSvcMutex);
    if (!g_studioPeakSvc) {
        g_studioPeakSvc = std::make_unique<Services::Library::PeakFileService>();
        Services::Library::PeakConfig cfg;
        cfg.cacheDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("BeatMate").getChildFile("Peaks")
                                 .getFullPathName().toStdString();
        cfg.useCache          = true;
        cfg.generateColorData = true;
        cfg.segmentsPerTrack  = 2000;
        g_studioPeakSvc->initialize(cfg);
    }
    return *g_studioPeakSvc;
}

static juce::ThreadPool& getStudioPeakPool() {
    static juce::ThreadPool pool { 2 };
    return pool;
}

static void studioTouchLruLocked(const std::string& path) {
    auto it = g_studioPeakCache.find(path);
    if (it == g_studioPeakCache.end()) return;
    g_studioPeakLru.erase(it->second.lruIter);
    g_studioPeakLru.push_front(path);
    it->second.lruIter = g_studioPeakLru.begin();
}

static void studioEnforceLruLocked() {
    while (g_studioPeakCache.size() > kStudioPeakCacheMax && !g_studioPeakLru.empty()) {
        const std::string& oldest = g_studioPeakLru.back();
        g_studioPeakCache.erase(oldest);
        g_studioPeakLru.pop_back();
    }
}

static const Services::Library::PeakData*
getStudioCachedPeaks(const std::string& path,
                     juce::Component::SafePointer<juce::Component> repaintTarget)
{
    if (path.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lk(g_studioPeakMutex);
        auto it = g_studioPeakCache.find(path);
        if (it != g_studioPeakCache.end()) {
            studioTouchLruLocked(path);
            return it->second.resolved ? &it->second.data : nullptr;
        }
        g_studioPeakLru.push_front(path);
        StudioPeakCacheEntry e;
        e.lruIter = g_studioPeakLru.begin();
        g_studioPeakCache.emplace(path, std::move(e));
    }

    auto bmpk = getStudioPeakSvc().getPeaksByPath(path);
    const bool bmpkRgbReady = bmpk && bmpk->isValid()
                              && !bmpk->lowFreq.empty()
                              && !bmpk->midFreq.empty()
                              && !bmpk->highFreq.empty();
    if (bmpkRgbReady) {
        std::lock_guard<std::mutex> lk(g_studioPeakMutex);
        auto it = g_studioPeakCache.find(path);
        if (it != g_studioPeakCache.end()) {
            it->second.data     = *bmpk;
            it->second.resolved = true;
            studioEnforceLruLocked();
            return &it->second.data;
        }
        return nullptr;
    }

    getStudioPeakPool().addJob([path, repaintTarget]() mutable {
        auto peaks = getStudioPeakSvc().generatePeaks(path, 0);
        const bool ok = peaks && peaks->isValid();
        {
            std::lock_guard<std::mutex> lk(g_studioPeakMutex);
            auto it = g_studioPeakCache.find(path);
            if (it != g_studioPeakCache.end() && ok) {
                it->second.data     = *peaks;
                it->second.resolved = true;
                studioEnforceLruLocked();
            }
        }
        juce::MessageManager::callAsync([repaintTarget]() mutable {
            if (repaintTarget) repaintTarget->repaint();
        });
    });
    return nullptr;
}

// Forward-decl — défini dans l'anon ns secondaire en bas du fichier.
void applyClipEffectsToBuffer(juce::AudioBuffer<float>& buf,
                               double sampleRate,
                               const std::vector<ClipEffect>& effects);

} // namespace

StudioClip::StudioClip(const Models::Track& t, StudioTimeline& owner)
    : m_track(t), m_owner(owner)
{
    m_audioInSec  = 0.0;
    m_audioOutSec = std::max(kMinClipLengthSec, t.duration > 0.0 ? t.duration : 0.0);

    m_thumb = std::make_unique<juce::AudioThumbnail>(
        512, sharedFormatManager(), sharedThumbCache());
    m_thumb->addChangeListener(this);

    if (!t.filePath.empty()) {
        juce::File f{ juce::String::fromUTF8(t.filePath.c_str()) };
        if (f.existsAsFile())
            m_thumb->setSource(new juce::FileInputSource(f));
    }

    juce::String tip;
    tip << juce::String(t.title) << "\n";
    if (!t.artist.empty()) tip << juce::String(t.artist) << "\n";
    if (t.bpm > 0)  tip << juce::String(t.bpm, 1) << " BPM   ";
    if (!t.camelotKey.empty()) tip << juce::String(t.camelotKey);
    else if (!t.key.empty())   tip << juce::String(t.key);
    if (t.duration > 0) {
        const int m = (int) t.duration / 60;
        const int s = (int) t.duration % 60;
        tip << "\n" << juce::String::formatted("%d:%02d", m, s);
    }
    setTooltip(tip);

    setMouseCursor(juce::MouseCursor::NormalCursor);
    setInterceptsMouseClicks(true, true);
}

StudioClip::~StudioClip() {
    if (m_thumb) m_thumb->removeChangeListener(this);
}

void StudioClip::changeListenerCallback(juce::ChangeBroadcaster*) {
    invalidateWaveCache();
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::Component>(this)]() mutable {
        if (safe) safe->repaint();
    });
}

void StudioClip::setStartSec(double s) {
    const double prev = m_startSec;
    m_startSec = std::max(0.0, s);
    if (m_dragMode != DragMode::None) {
        const float pps = (float) m_owner.pixelsPerSec();
        const int x  = (int) std::round(m_startSec * pps);
        const int yB = getY();
        const int wB = getWidth();
        const int hB = getHeight();
        if (getX() != x) setBounds(x, yB, wB, hB);

        if (m_groupId > 0) {
            const double delta = m_startSec - prev;
            if (std::abs(delta) > 1e-6) m_owner.propagateGroupMove(m_groupId, delta, this);
        }
    } else if (onChanged) {
        onChanged();
    }
}

void StudioClip::setAudioRange(double inSec, double outSec) {
    const double dur = m_track.duration > 0.0 ? m_track.duration : outSec;
    inSec  = juce::jlimit(0.0, dur, inSec);
    outSec = juce::jlimit(inSec + kMinClipLengthSec, dur, outSec);
    m_audioInSec  = inSec;
    m_audioOutSec = outSec;
    // Pendant un drag de resize, onChanged() serait trop couteux : coalesce au mouseUp.
    if (m_dragMode != DragMode::None) {
        const float pps = (float) m_owner.pixelsPerSec();
        const int w = std::max((int) kClipMinWidthPx,
                               (int) std::round(getLengthSec() * pps));
        if (getWidth() != w) setSize(w, getHeight());
        invalidateWaveCache();
        repaint(getLocalBounds());
        return;
    }
    if (onChanged) onChanged();
    invalidateWaveCache();
    repaint(getLocalBounds());
}

void StudioClip::setFadeIn(double s) {
    m_fade.inSec = std::max(0.0, s);
    repaint(getLocalBounds());
    if (m_dragMode == DragMode::None && onChanged) onChanged();
}
void StudioClip::setFadeOut(double s) {
    m_fade.outSec = std::max(0.0, s);
    repaint(getLocalBounds());
    if (m_dragMode == DragMode::None && onChanged) onChanged();
}

void StudioClip::setMarker(const std::string& name, juce::Colour col) {
    m_marker.label  = name;
    m_marker.colour = col;
    repaint(getLocalBounds());
}

void StudioClip::setLoop(double inRel, double outRel, bool enabled) {
    m_loopInRel  = inRel;
    m_loopOutRel = outRel;
    m_loopOn     = enabled;
    repaint(getLocalBounds());
}

void StudioClip::setCues(std::vector<Models::CuePoint> cues) {
    m_cues = std::move(cues);
    repaint(getLocalBounds());
}

void StudioClip::addEffect(const std::string& type) {
    ClipEffect fx;
    fx.type = type;
    fx.enabled = true;
    m_effects.push_back(std::move(fx));
    repaint(getLocalBounds());
    if (onChanged) onChanged();
}

void StudioClip::removeEffect(int idx) {
    if (idx < 0 || idx >= (int) m_effects.size()) return;
    m_effects.erase(m_effects.begin() + idx);
    repaint(getLocalBounds());
    if (onChanged) onChanged();
}

void StudioClip::setEffectParam(int fxIdx, const std::string& name, float v) {
    if (fxIdx < 0 || fxIdx >= (int) m_effects.size()) return;
    auto& fx = m_effects[fxIdx];
    for (auto& p : fx.params) {
        if (p.first == name) { p.second = v; return; }
    }
    fx.params.emplace_back(name, v);
    if (onChanged) onChanged();
}

void StudioClip::setEffectEnabled(int fxIdx, bool enabled) {
    if (fxIdx < 0 || fxIdx >= (int) m_effects.size()) return;
    if (m_effects[fxIdx].enabled == enabled) return;
    m_effects[fxIdx].enabled = enabled;
    repaint(getLocalBounds());
    if (onChanged) onChanged();
}

void StudioClip::attachStems(std::unique_ptr<StemData> s) {
    m_stems = std::move(s);
    m_showStems = (m_stems && m_stems->ready);
    repaint(getLocalBounds());
    if (onChanged) onChanged();
}

void StudioClip::setStemMute(int stemIdx, bool muted) {
    if (stemIdx < 0 || stemIdx > 3) return;
    if (m_stemMutes[stemIdx] == muted) return;
    m_stemMutes[stemIdx] = muted;
    repaint(getLocalBounds());
    if (onChanged) onChanged();
}

bool StudioClip::isStemMuted(int stemIdx) const noexcept {
    if (stemIdx < 0 || stemIdx > 3) return false;
    return m_stemMutes[stemIdx];
}

StudioClip::DragMode StudioClip::hitTest(juce::Point<int> p) const {
    const auto b = getLocalBounds();
    if (b.isEmpty()) return DragMode::None;

    if (hitAutoPoint(p) >= 0) return DragMode::AutoPoint;
    if (hitHotCue(p)    >= 0) return DragMode::HotCue;
    {
        const int lh = hitLoopHandle(p);
        if (lh == 0) return DragMode::LoopLeft;
        if (lh == 1) return DragMode::LoopRight;
    }

    if (p.y < kHeaderH) {
        if (p.x < kFadeHitPx)                     return DragMode::FadeIn;
        if (p.x > b.getWidth() - kFadeHitPx)      return DragMode::FadeOut;
        return DragMode::Move;
    }
    if (p.x < (int) kEdgeHitPx)                   return DragMode::ResizeLeft;
    if (p.x > b.getWidth() - (int) kEdgeHitPx)    return DragMode::ResizeRight;
    return DragMode::Move;
}

int StudioClip::hitAutoPoint(juce::Point<int> p) const {
    if (m_autoVol.empty()) return -1;
    const auto wave = juce::Rectangle<float>(
        2.0f, (float) kHeaderH + 2.0f,
        (float) getWidth() - 4.0f,
        (float) getHeight() - kHeaderH - 4.0f);
    const double len = std::max(0.001, getLengthSec());
    for (size_t i = 0; i < m_autoVol.size(); ++i) {
        const float x = wave.getX() + (float) (m_autoVol[i].timeRel / len) * wave.getWidth();
        const float y = wave.getY() + wave.getHeight() * (1.0f - m_autoVol[i].value);
        if (std::abs((float) p.x - x) <= 6.0f && std::abs((float) p.y - y) <= 6.0f)
            return (int) i;
    }
    return -1;
}

int StudioClip::hitLoopHandle(juce::Point<int> p) const {
    if (!m_loopOn) return -1;
    const auto wave = juce::Rectangle<float>(
        2.0f, (float) kHeaderH + 2.0f,
        (float) getWidth() - 4.0f,
        (float) getHeight() - kHeaderH - 4.0f);
    const double len = std::max(0.001, getLengthSec());
    const float xL = wave.getX() + (float) (m_loopInRel  / len) * wave.getWidth();
    const float xR = wave.getX() + (float) (m_loopOutRel / len) * wave.getWidth();
    if (std::abs((float) p.x - xL) <= 5.0f && p.y > kHeaderH) return 0;
    if (std::abs((float) p.x - xR) <= 5.0f && p.y > kHeaderH) return 1;
    return -1;
}

int StudioClip::hitHotCue(juce::Point<int> p) const {
    if (m_cues.empty()) return -1;
    const auto wave = juce::Rectangle<float>(
        2.0f, (float) kHeaderH + 2.0f,
        (float) getWidth() - 4.0f,
        (float) getHeight() - kHeaderH - 4.0f);
    const double len = std::max(0.001, getLengthSec());
    for (size_t i = 0; i < m_cues.size(); ++i) {
        if (m_cues[i].position < 0.0 || m_cues[i].position > len) continue;
        const float x = wave.getX() + (float) (m_cues[i].position / len) * wave.getWidth();
        if (std::abs((float) p.x - x) <= 5.0f && p.y > kHeaderH)
            return (int) i;
    }
    return -1;
}

void StudioClip::mouseMove(const juce::MouseEvent& e) {
    switch (hitTest(e.getPosition())) {
        case DragMode::Move:        setMouseCursor(juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::ResizeLeft:
        case DragMode::ResizeRight: setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::FadeIn:
        case DragMode::FadeOut:     setMouseCursor(juce::MouseCursor::CopyingCursor); break;
        default:                    setMouseCursor(juce::MouseCursor::NormalCursor); break;
    }
    // mouseEnter peut manquer (entree par un coin) ; mouseMove garantit le hover.
    setHovered(true);
}

void StudioClip::mouseEnter(const juce::MouseEvent&) {
    setHovered(true);
}

void StudioClip::mouseExit(const juce::MouseEvent&) {
    setHovered(false);
}

void StudioClip::mouseDown(const juce::MouseEvent& e) {
    const int idx = m_owner.indexOf(this);
    spdlog::debug("[Studio] StudioClip::mouseDown: clip idx={}, mods={}{}{}",
                  idx,
                  e.mods.isLeftButtonDown()  ? "L" : "",
                  e.mods.isRightButtonDown() ? "R" : "",
                  e.mods.isShiftDown()       ? "+Shift" : "");
    if (! e.mods.isPopupMenu() && ! e.mods.isAltDown()) {
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) {
            m_owner.toggleSelectClip(idx);
        } else {
            m_owner.setSelectedIndex(idx);
        }
    } else {
        if (m_owner.getSelectedIndex() != idx
            && std::find(m_owner.getMultiSelection().begin(),
                         m_owner.getMultiSelection().end(), idx)
               == m_owner.getMultiSelection().end()) {
            m_owner.setSelectedIndex(idx);
        }
    }
    // Focus clavier pour Ctrl+C/X/V/Suppr sans dependre du parent.
    m_owner.grabKeyboardFocus();

    if (e.mods.isAltDown() && e.getPosition().y > kHeaderH) {
        const float relX = (float) (e.getPosition().x - 0)
                            / std::max(1.0f, (float) (getWidth()));
        const double t = juce::jlimit(0.0, 1.0, (double) relX) * getLengthSec();
        const float relY = (float) (e.getPosition().y - kHeaderH)
                            / std::max(1.0f, (float)(getHeight() - kHeaderH));
        const float v = juce::jlimit(0.0f, 1.0f, 1.0f - relY);

        if (e.mods.isShiftDown() && e.mods.isCtrlDown())  addEqLoAutoPoint (t, v);
        else if (e.mods.isShiftDown())                     addEqHiAutoPoint (t, v);
        else if (e.mods.isCtrlDown())                      addEqMidAutoPoint(t, v);
        else                                                addVolumeAutoPoint(t, v);
        return;
    }

    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        m.addItem(1,  "Couper            Ctrl+X");
        m.addItem(2,  "Copier            Ctrl+C");
        m.addItem(3,  "Coller            Ctrl+V");
        m.addSeparator();
        m.addItem(10, "Split (playhead)  Ctrl+B");
        m.addItem(11, "Dupliquer");
        m.addItem(12, "Supprimer         Suppr");
        m.addSeparator();
        m.addItem(20, "Quantize this clip");
        m.addItem(21, "Align start to next beat");
        m.addItem(22, "Align start to previous beat");
        m.addSeparator();
        m.addItem(30, "Fade-in 2 s");
        m.addItem(31, "Fade-out 2 s");
        m.addItem(32, "Reset fades");
        m.addSeparator();
        m.addItem(40, "Marquer INTRO");
        m.addItem(41, "Marquer OUTRO");
        m.addItem(42, m_locked ? "Deverrouiller clip" : "Verrouiller clip");
        m.addSeparator();
        m.addItem(50, "Infos piste");
        m.addSeparator();
        m.addItem(60, "Inverser le clip");
        m.addItem(61, m_muted ? "Reactiver" : "Mute");
        m.addItem(62, "Vider l'automation");
        m.addSeparator();
        m.addItem(70, "Transition: Cut");
        m.addItem(71, "Transition: Fade");
        m.addItem(72, "Transition: Equal Power");
        m.addItem(73, "Transition: Echo Out");
        m.addItem(74, "Transition: Filter Sweep");
        m.addItem(75, "Transition: Backspin");
        m.addSeparator();
        m.addItem(80, "Crossfade 4 bars");
        m.addItem(81, "Crossfade 8 bars");
        m.addItem(82, "Crossfade 16 bars");
        m.addItem(83, "Crossfade 32 bars");
        m.addSeparator();
        m.addItem(90, juce::String::formatted("Volume 25%% (current %.0f%%)", m_volume * 100.0f));
        m.addItem(91, "Volume 50%");
        m.addItem(92, "Volume 75%");
        m.addItem(93, "Volume 100%");
        m.addItem(94, "Volume 125%");
        m.addSeparator();
        m.addItem(100, "Loop 1 bar from playhead");
        m.addItem(101, "Loop 4 bars from playhead");
        m.addItem(102, "Loop OFF");
        m.addSeparator();
        juce::PopupMenu fxMenu;
        fxMenu.addItem(200, juce::String::fromUTF8("Reverb"));
        fxMenu.addItem(201, juce::String::fromUTF8("Delay"));
        fxMenu.addItem(202, juce::String::fromUTF8("EQ 3-bandes"));
        fxMenu.addItem(203, juce::String::fromUTF8("Compresseur"));
        fxMenu.addItem(204, juce::String::fromUTF8("Filtre LP/HP"));
        m.addSubMenu(juce::String::fromUTF8("Ajouter un effet"), fxMenu);
        if (!m_effects.empty()) {
            juce::PopupMenu rmMenu;
            for (size_t i = 0; i < m_effects.size(); ++i) {
                rmMenu.addItem(220 + (int) i,
                    juce::String::fromUTF8("Retirer ") + juce::String(m_effects[i].type));
            }
            m.addSubMenu(juce::String::fromUTF8("Effets actifs"), rmMenu);
        }
        m.addSeparator();
        m.addItem(400, juce::String::fromUTF8("Remix IA (sur ce clip â†’ 6 sections)"));
        m.addSeparator();
        m.addItem(300, juce::String::fromUTF8("Extraire stems (Demucs)"));
        m.addItem(301, juce::String::fromUTF8("Extraire stems sur sélection"));
        if (m_stems && m_stems->ready) {
            m.addItem(310, juce::String::fromUTF8("Mute Vocals"),  true, m_stemMutes[3]);
            m.addItem(311, juce::String::fromUTF8("Mute Drums"),   true, m_stemMutes[0]);
            m.addItem(312, juce::String::fromUTF8("Mute Bass"),    true, m_stemMutes[1]);
            m.addItem(313, juce::String::fromUTF8("Mute Other"),   true, m_stemMutes[2]);
        }

        m.showMenuAsync(juce::PopupMenu::Options(),
            [weak = juce::Component::SafePointer<StudioClip>(this)](int r) {
                if (!weak) return;
                auto* self = weak.getComponent();
                auto* tl = &self->m_owner;
                const int idx = tl->indexOf(self);
                switch (r) {
                    case 1:  tl->cutSelection();             break;
                    case 2:  tl->copySelection();            break;
                    case 3:  tl->pasteAtPlayhead();          break;
                    case 10: tl->splitAtPlayhead();          break;
                    case 11: tl->duplicateClip(idx);         break;
                    case 12: tl->removeClip(idx);            break;
                    case 20: {
                        const double beat = 60.0 / std::max(1.0, tl->getMasterBpm());
                        const double snapped = std::round(self->getStartSec() / beat) * beat;
                        self->setStartSec(snapped);
                        tl->layoutClips();
                        tl->repaint();
                        break;
                    }
                    case 21: tl->alignClipToNextBeat(idx);   break;
                    case 22: tl->alignClipToPrevBeat(idx);   break;
                    case 30: self->setFadeIn(2.0);           break;
                    case 31: self->setFadeOut(2.0);          break;
                    case 32: self->setFadeIn(0.0); self->setFadeOut(0.0); break;
                    case 40: self->setMarker("INTRO", juce::Colour(0xFF22D3EE)); break;
                    case 41: self->setMarker("OUTRO", juce::Colour(0xFFF472B6)); break;
                    case 42: self->setLocked(!self->isLocked()); break;
                    case 50: spdlog::info("[Studio] Track info: {}", self->getTrack().title); break;
                    case 60: self->setReversed(!self->isReversed()); break;
                    case 61: self->setMuted(!self->isMuted()); break;
                    case 62: self->clearAutomation(); break;
                    case 70: tl->setTransitionType(idx, TransitionKind::Cut);         break;
                    case 71: tl->setTransitionType(idx, TransitionKind::Fade);        break;
                    case 72: tl->setTransitionType(idx, TransitionKind::EqualPower);  break;
                    case 73: tl->setTransitionType(idx, TransitionKind::EchoOut);     break;
                    case 74: tl->setTransitionType(idx, TransitionKind::FilterSweep); break;
                    case 75: tl->setTransitionType(idx, TransitionKind::Backspin);    break;
                    case 80: tl->setTransitionDurationBars(idx, 4);  break;
                    case 81: tl->setTransitionDurationBars(idx, 8);  break;
                    case 82: tl->setTransitionDurationBars(idx, 16); break;
                    case 83: tl->setTransitionDurationBars(idx, 32); break;
                    case 90: self->setVolume(0.25f); break;
                    case 91: self->setVolume(0.50f); break;
                    case 92: self->setVolume(0.75f); break;
                    case 93: self->setVolume(1.00f); break;
                    case 94: self->setVolume(1.25f); break;
                    case 100: {
                        const double bar = 4.0 * 60.0 / std::max(1.0, tl->getMasterBpm());
                        const double rel = juce::jlimit(0.0, self->getLengthSec() - bar,
                                            tl->getPlayheadSec() - self->getStartSec());
                        self->setLoop(rel, rel + bar, true);
                        break;
                    }
                    case 101: {
                        const double bar = 4.0 * 60.0 / std::max(1.0, tl->getMasterBpm());
                        const double rel = juce::jlimit(0.0, self->getLengthSec() - bar * 4,
                                            tl->getPlayheadSec() - self->getStartSec());
                        self->setLoop(rel, rel + bar * 4, true);
                        break;
                    }
                    case 102: self->setLoop(0.0, 0.0, false); break;

                    case 200: self->addEffect("reverb");      break;
                    case 201: self->addEffect("delay");       break;
                    case 202: self->addEffect("eq3");         break;
                    case 203: self->addEffect("compressor");  break;
                    case 204: self->addEffect("filter");      break;

                    case 300: tl->setSelectedIndex(idx); tl->extractStemsForSelection(); break;
                    case 301: tl->extractStemsForSelection(); break;
                    case 310: self->setStemMute(3, !self->isStemMuted(3)); break; // vocals
                    case 311: self->setStemMute(0, !self->isStemMuted(0)); break; // drums
                    case 312: self->setStemMute(1, !self->isStemMuted(1)); break; // bass
                    case 313: self->setStemMute(2, !self->isStemMuted(2)); break; // other
                    case 400: tl->setSelectedIndex(idx); tl->runRemixAI(); break;

                    default:
                        if (r >= 220 && r < 220 + (int) self->getEffects().size()) {
                            self->removeEffect(r - 220);
                        }
                        break;
                }
            });
        return;
    }

    if (m_locked) return;
    m_dragMode      = hitTest(e.getPosition());
    m_dragFrom      = e.getPosition();
    m_dragStartSec  = m_startSec;
    m_dragAudioIn   = m_audioInSec;
    m_dragAudioOut  = m_audioOutSec;
    m_dragFadeIn    = m_fade.inSec;
    m_dragFadeOut   = m_fade.outSec;
    m_dragAutoIdx   = (m_dragMode == DragMode::AutoPoint) ? hitAutoPoint(e.getPosition()) : -1;
    m_dragCueIdx    = (m_dragMode == DragMode::HotCue)    ? hitHotCue   (e.getPosition()) : -1;
}

void StudioClip::mouseDrag(const juce::MouseEvent& e) {
    if (m_locked || m_dragMode == DragMode::None) return;
    const double pps = std::max(1.0, m_owner.pixelsPerSec());
    const int    dx  = e.getPosition().x - m_dragFrom.x;
    const double dt  = (double) dx / pps;

    switch (m_dragMode) {
        case DragMode::Move: {
            const double target = std::max(0.0, m_dragStartSec + dt);
            setStartSec(m_owner.snap(target));
            // Ne pas rappeler layoutClips ici (deja via onChanged) : source du lag de drag.
            break;
        }
        case DragMode::ResizeLeft: {
            const double newIn = m_owner.snap(std::max(0.0, m_dragAudioIn + dt));
            setAudioRange(newIn, m_audioOutSec);
            break;
        }
        case DragMode::ResizeRight: {
            const double newOut = m_owner.snap(std::max(m_audioInSec + kMinClipLengthSec,
                                                        m_dragAudioOut + dt));
            setAudioRange(m_audioInSec, newOut);
            break;
        }
        case DragMode::FadeIn:  setFadeIn (std::max(0.0, m_dragFadeIn  + dt)); break;
        case DragMode::FadeOut: setFadeOut(std::max(0.0, m_dragFadeOut - dt)); break;
        case DragMode::AutoPoint: {
            if (m_dragAutoIdx >= 0 && m_dragAutoIdx < (int) m_autoVol.size()) {
                const auto wave = juce::Rectangle<float>(
                    2.0f, (float) kHeaderH + 2.0f,
                    (float) getWidth() - 4.0f,
                    (float) getHeight() - kHeaderH - 4.0f);
                const double len = std::max(0.001, getLengthSec());
                const float relX = juce::jlimit(0.0f, 1.0f,
                    ((float) e.getPosition().x - wave.getX()) / wave.getWidth());
                const float relY = juce::jlimit(0.0f, 1.0f,
                    ((float) e.getPosition().y - wave.getY()) / wave.getHeight());
                m_autoVol[m_dragAutoIdx].timeRel = relX * len;
                m_autoVol[m_dragAutoIdx].value   = 1.0f - relY;
            }
            break;
        }
        case DragMode::HotCue: {
            if (m_dragCueIdx >= 0 && m_dragCueIdx < (int) m_cues.size()) {
                const auto wave = juce::Rectangle<float>(
                    2.0f, (float) kHeaderH + 2.0f,
                    (float) getWidth() - 4.0f,
                    (float) getHeight() - kHeaderH - 4.0f);
                const double len = std::max(0.001, getLengthSec());
                const float relX = juce::jlimit(0.0f, 1.0f,
                    ((float) e.getPosition().x - wave.getX()) / wave.getWidth());
                m_cues[m_dragCueIdx].position = relX * len;
            }
            break;
        }
        case DragMode::LoopLeft:
        case DragMode::LoopRight: {
            const auto wave = juce::Rectangle<float>(
                2.0f, (float) kHeaderH + 2.0f,
                (float) getWidth() - 4.0f,
                (float) getHeight() - kHeaderH - 4.0f);
            const double len = std::max(0.001, getLengthSec());
            const float relX = juce::jlimit(0.0f, 1.0f,
                ((float) e.getPosition().x - wave.getX()) / wave.getWidth());
            const double t = relX * len;
            if (m_dragMode == DragMode::LoopLeft)
                m_loopInRel  = std::min(t, m_loopOutRel - 0.05);
            else
                m_loopOutRel = std::max(t, m_loopInRel  + 0.05);
            m_loopOn = true;
            break;
        }
        default: break;
    }
    repaint(getLocalBounds());
}

void StudioClip::mouseUp(const juce::MouseEvent&) {
    const bool wasDragging = (m_dragMode != DragMode::None);
    m_dragMode = DragMode::None;
    // Un seul onChanged coalesce en fin de drag.
    if (wasDragging && onChanged) {
        onChanged();
    }
}

void StudioClip::paint(juce::Graphics& g) {
    const auto bf = getLocalBounds().toFloat();
    constexpr float kCorner = 8.0f;

    const int idx = m_owner.indexOf(this);
    juce::Colour laneTint(0xFF3B82F6);
    if (idx >= 0) {
        const auto li = m_owner.getLaneInfo(m_owner.getLaneOfClip(idx));
        laneTint = li.color;
    }

    if (m_selected) {
        for (int k = 0; k < 4; ++k) {
            const float exp = (float) (4 - k);
            g.setColour(juce::Colour(0xFFE3A942).withAlpha(0.06f + 0.04f * (3 - k)));
            g.fillRoundedRectangle(bf.expanded(exp), kCorner + exp);
        }
    } else {
        // Drop shadow leger (gain perf : pas DropShadow JUCE recree par frame).
        g.setColour(juce::Colour(0x60000000));
        g.fillRoundedRectangle(bf.translated(0.0f, 2.0f), kCorner);
    }

    const auto bodyTop = juce::Colour(0xFF1F2129).interpolatedWith(laneTint, 0.06f);
    const auto bodyBot = juce::Colour(0xFF14151B).interpolatedWith(laneTint, 0.03f);
    juce::ColourGradient bodyGrad(bodyTop, bf.getX(), bf.getY(),
                                   bodyBot, bf.getX(), bf.getBottom(), false);
    g.setGradientFill(bodyGrad);
    g.fillRoundedRectangle(bf, kCorner);

    const auto accent = m_selected ? juce::Colour(0xFFE3A942)
                                    : (m_hovered ? juce::Colour(0xFF22D3EE)
                                                 : juce::Colour(0xFF353841));
    g.setColour(accent);
    g.drawRoundedRectangle(bf.reduced(0.5f), kCorner,
                           m_selected ? 1.6f : (m_hovered ? 1.2f : 0.8f));

    g.setColour(juce::Colours::white.withAlpha(m_selected ? 0.12f : 0.06f));
    g.drawHorizontalLine((int) bf.getY() + 1, bf.getX() + kCorner, bf.getRight() - kCorner);

    auto header = bf.withHeight((float) kHeaderH);
    paintHeader(g, header);

    auto wave = bf.withTrimmedTop((float) kHeaderH).reduced(2.0f, 2.0f);
    if (m_showStems) {
        paintStems(g, wave);
    } else {
        paintWaveform(g, wave);
    }
    paintFades(g, wave);
    paintLoop(g, wave);
    paintCues(g, wave);
    paintAutomation(g, wave);
    paintMarker(g, wave);

    if (m_locked) {
        g.setColour(juce::Colour(0x18FFFFFF));
        for (float x = 0; x < bf.getWidth(); x += 10.0f)
            g.drawLine(x, 0, x - 12.0f, bf.getHeight(), 0.4f);
        g.setColour(juce::Colour(0xFFF43F5E));
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("LOCK"),
                   (int) bf.getX() + 6, (int) bf.getBottom() - 13, 40, 11,
                   juce::Justification::centredLeft);
    }
}

void StudioClip::paintHeader(juce::Graphics& g, juce::Rectangle<float> r) {
    constexpr float kCorner = 8.0f;

    const int idx = m_owner.indexOf(this);
    juce::Colour laneCol(0xFF3B82F6);
    if (idx >= 0) {
        const auto li = m_owner.getLaneInfo(m_owner.getLaneOfClip(idx));
        laneCol = li.color;
    }

    const auto hdrTop = juce::Colour(0xFF2A2D38).interpolatedWith(laneCol, 0.18f);
    const auto hdrBot = juce::Colour(0xFF1B1D27).interpolatedWith(laneCol, 0.10f);
    juce::ColourGradient hdrGrad(hdrTop, r.getX(), r.getY(),
                                  hdrBot, r.getX(), r.getBottom(), false);
    g.setGradientFill(hdrGrad);
    g.fillRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), kCorner);

    g.setColour(laneCol);
    g.fillRoundedRectangle(r.getX(), r.getY() + 1.0f, 3.0f, r.getHeight() - 2.0f, 1.5f);

    if (m_selected) {
        g.setColour(juce::Colour(0xFFE3A942));
        g.fillRoundedRectangle(r.getX() + 4.0f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f, 1.0f);
    }

    auto inner = r.withTrimmedLeft(12.0f);
    g.setFont(juce::Font(juce::FontOptions("Segoe UI", 11.5f, juce::Font::bold)));
    g.setColour(juce::Colour(0xFFFAFAFA));
    g.drawText(juce::String(m_track.title),
               inner.withWidth(inner.getWidth() - 100.0f).toNearestInt(),
               juce::Justification::centredLeft, true);

    if (r.getWidth() > 180.0f) {
        auto bpm = juce::Rectangle<float>(r.getRight() - 96.0f, r.getY() + 3.0f, 44.0f, 16.0f);
        g.setColour(juce::Colour(0xFF0EA5E9).withAlpha(0.28f));
        g.fillRoundedRectangle(bpm, 5.0f);
        g.setColour(juce::Colour(0xFF0EA5E9).withAlpha(0.45f));
        g.drawRoundedRectangle(bpm.reduced(0.5f), 5.0f, 0.7f);
        g.setColour(juce::Colour(0xFFE0F2FE));
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 9.5f, juce::Font::bold)));
        g.drawText(juce::String(m_track.bpm, 0) + " BPM", bpm.toNearestInt(),
                   juce::Justification::centred);

        auto key = juce::Rectangle<float>(r.getRight() - 48.0f, r.getY() + 3.0f, 44.0f, 16.0f);
        g.setColour(juce::Colour(0xFF9333EA).withAlpha(0.28f));
        g.fillRoundedRectangle(key, 5.0f);
        g.setColour(juce::Colour(0xFF9333EA).withAlpha(0.45f));
        g.drawRoundedRectangle(key.reduced(0.5f), 5.0f, 0.7f);
        g.setColour(juce::Colour(0xFFEDE9FE));
        const auto keyStr = m_track.camelotKey.empty()
            ? juce::String(m_track.key) : juce::String(m_track.camelotKey);
        g.drawText(keyStr, key.toNearestInt(), juce::Justification::centred);
    }

    if (!m_marker.label.empty()) {
        auto mr = juce::Rectangle<float>(inner.getRight() - 110.0f, r.getY() + 3.0f, 32.0f, 16.0f);
        g.setColour(m_marker.colour.withAlpha(0.32f));
        g.fillRoundedRectangle(mr, 5.0f);
        g.setColour(m_marker.colour);
        g.drawRoundedRectangle(mr.reduced(0.5f), 5.0f, 0.7f);
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 9.5f, juce::Font::bold)));
        g.drawText(juce::String(m_marker.label),
                   mr.toNearestInt(), juce::Justification::centred);
    }
}

void StudioClip::paintWaveform(juce::Graphics& g, juce::Rectangle<float> r) {
    if (r.getHeight() < 8.0f || r.getWidth() < 8.0f) return;

    const int curW = (int) r.getWidth();
    const int curH = (int) r.getHeight();
    const double curZoom = m_owner.getZoom();
    if (curW > 0 && curH > 0
        && (!m_waveCacheValid || curW != m_waveCacheW || curH != m_waveCacheH
            || std::abs(curZoom - m_waveCacheZoom) > 1e-6)) {
        m_waveCache = juce::Image(juce::Image::ARGB, curW, curH, true);
        juce::Graphics gc(m_waveCache);
        juce::ColourGradient bgGrad(juce::Colour(0xFF1A1A1F), 0.0f, 0.0f,
                                    juce::Colour(0xFF12121A), 0.0f, (float) curH, false);
        gc.setGradientFill(bgGrad);
        gc.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, (float) curW, (float) curH), 3.0f);

        const double total = m_thumb ? (m_thumb->getTotalLength() > 0.0 ? m_thumb->getTotalLength()
                                                                         : std::max(m_audioOutSec, 1.0))
                                     : std::max(m_audioOutSec, 1.0);
        const double showIn  = juce::jlimit(0.0, total, m_audioInSec);
        const double showOut = juce::jlimit(showIn + 0.01, total, m_audioOutSec);
        const auto wr = juce::Rectangle<int>(2, 2, std::max(1, curW - 4), std::max(1, curH - 4));

        const auto* peaks = getStudioCachedPeaks(m_track.filePath,
                              juce::Component::SafePointer<juce::Component>(this));
        const bool rgbReady = peaks && peaks->isValid()
                              && !peaks->lowFreq.empty()
                              && !peaks->midFreq.empty()
                              && !peaks->highFreq.empty()
                              && peaks->duration > 0.0;
        if (rgbReady) {
            const int N = (int) peaks->lowFreq.size();
            const float midY  = (float) wr.getCentreY();
            const float halfH = (float)(wr.getHeight() / 2 - 2);
            const double durationFile = peaks->duration;
            const double s0 = juce::jlimit(0.0, 1.0, showIn  / std::max(1e-3, durationFile));
            const double s1 = juce::jlimit(0.0, 1.0, showOut / std::max(1e-3, durationFile));
            const int segStart = (int) std::floor(s0 * N);
            const int segEnd   = (int) std::ceil (s1 * N);
            const int segCount = std::max(1, segEnd - segStart);

            for (int x = 0; x < wr.getWidth(); ++x) {
                const double t01 = (double) x / std::max(1, wr.getWidth() - 1);
                const int seg = std::min(N - 1, segStart + (int) std::round(t01 * segCount));
                if (seg < 0) continue;
                const float lo = juce::jlimit(0.0f, 1.0f, peaks->lowFreq [seg]);
                const float md = juce::jlimit(0.0f, 1.0f, peaks->midFreq [seg]);
                const float hi = juce::jlimit(0.0f, 1.0f, peaks->highFreq[seg]);
                const float xp = (float)(wr.getX() + x);

                gc.setColour(juce::Colour(0xFFEF4444).withAlpha(0.92f));
                gc.drawVerticalLine((int) xp, midY - lo * halfH, midY + lo * halfH);
                gc.setColour(juce::Colour(0xFF22C55E).withAlpha(0.78f));
                gc.drawVerticalLine((int) xp, midY - md * halfH * 0.85f, midY + md * halfH * 0.85f);
                gc.setColour(juce::Colour(0xFF3B82F6).withAlpha(0.62f));
                gc.drawVerticalLine((int) xp, midY - hi * halfH * 0.65f, midY + hi * halfH * 0.65f);
            }

            gc.setColour(juce::Colour(0x14FFFFFF));
            gc.drawHorizontalLine((int) midY, (float) wr.getX(), (float) wr.getRight());
        } else if (m_thumb && m_thumb->getNumChannels() > 0
                   && m_thumb->getNumSamplesFinished() > 0) {
            gc.setColour(juce::Colour(0xFFEF4444).withAlpha(0.85f));
            m_thumb->drawChannels(gc, wr, showIn, showOut, 1.0f);
            gc.setColour(juce::Colour(0xFF22C55E).withAlpha(0.70f));
            m_thumb->drawChannels(gc, wr.reduced(0, 1), showIn, showOut, 0.80f);
            gc.setColour(juce::Colour(0xFF3B82F6).withAlpha(0.55f));
            m_thumb->drawChannels(gc, wr.reduced(0, 2), showIn, showOut, 0.60f);
            gc.setColour(juce::Colour(0xFFF5C97A).withAlpha(0.25f));
            m_thumb->drawChannels(gc, wr.reduced(0, 3), showIn, showOut, 0.40f);
        }

        m_waveCacheW = curW;
        m_waveCacheH = curH;
        m_waveCacheZoom = curZoom;
        m_waveCacheValid = true;
    }
    if (m_waveCacheValid) {
        g.drawImageAt(m_waveCache, (int) r.getX(), (int) r.getY());
        const float cy = r.getCentreY();
        g.setColour(juce::Colour(0x14FFFFFF));
        g.drawHorizontalLine((int) cy, r.getX(), r.getRight());
        return;
    }

    g.setColour(juce::Colour(0xFF1A1A1F));
    g.fillRoundedRectangle(r, 3.0f);

    const auto wave = r.reduced(2.0f);
    const float cy  = wave.getCentreY();

    if (m_thumb && m_thumb->getNumChannels() > 0
        && m_thumb->getNumSamplesFinished() > 0) {
        const double total = m_thumb->getTotalLength() > 0.0
                                 ? m_thumb->getTotalLength()
                                 : std::max(m_audioOutSec, 1.0);
        const double showIn  = juce::jlimit(0.0, total, m_audioInSec);
        const double showOut = juce::jlimit(showIn + 0.01, total, m_audioOutSec);
        const auto wr = wave.toNearestInt();

        g.setColour(juce::Colour(0xFFE3A942));
        m_thumb->drawChannels(g, wr, showIn, showOut, 1.0f);

        g.setColour(juce::Colour(0xFFF5C97A));
        m_thumb->drawChannels(g, wr.reduced(0, 1), showIn, showOut, 0.6f);
    } else {
        g.setColour(juce::Colour(0xFF64748B));
        g.setFont(juce::Font(10.0f));
        g.drawText(juce::String::fromUTF8("Decodage..."),
                   wave.toNearestInt(), juce::Justification::centred);
    }

    g.setColour(juce::Colour(0x14FFFFFF));
    g.drawHorizontalLine((int) cy, wave.getX(), wave.getRight());
}

void StudioClip::paintFades(juce::Graphics& g, juce::Rectangle<float> r) {
    const double len = std::max(0.001, getLengthSec());
    const float  W   = r.getWidth();

    auto drawFade = [&](float xBase, float xPeak, bool isFadeIn) {
        juce::Path p;
        p.startNewSubPath(xBase, r.getBottom());
        p.lineTo         (xPeak, r.getY());
        p.lineTo         (xBase, r.getY());
        p.closeSubPath();

        juce::ColourGradient grad(juce::Colour(0xFF38BDF8).withAlpha(0.42f),
                                   xBase, r.getCentreY(),
                                   juce::Colour(0xFF38BDF8).withAlpha(0.0f),
                                   xPeak, r.getCentreY(), false);
        g.setGradientFill(grad);
        g.fillPath(p);

        g.setColour(juce::Colour(0xFFFACC15));
        if (isFadeIn) g.drawLine(xBase, r.getBottom(), xPeak, r.getY(),    1.6f);
        else          g.drawLine(xBase, r.getBottom(), xPeak, r.getY(),    1.6f);

        const float hx = xPeak;
        const float hy = r.getY() + 4.0f;
        g.setColour(juce::Colour(0xFF0F172A).withAlpha(0.6f));
        g.fillEllipse(hx - 5.0f, hy - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(hx - 4.0f, hy - 4.0f, 8.0f, 8.0f);
        g.setColour(juce::Colour(0xFFFACC15));
        g.drawEllipse(hx - 4.0f, hy - 4.0f, 8.0f, 8.0f, 1.4f);
    };

    if (m_fade.inSec > 0.0) {
        const float fw = (float) (m_fade.inSec / len) * W;
        drawFade(r.getX(), r.getX() + fw, true);
    }
    if (m_fade.outSec > 0.0) {
        const float fw = (float) (m_fade.outSec / len) * W;
        drawFade(r.getRight(), r.getRight() - fw, false);
    }
}

void StudioClip::paintLoop(juce::Graphics& g, juce::Rectangle<float> r) {
    if (!m_loopOn || m_loopOutRel <= m_loopInRel) return;
    const double len = std::max(0.001, getLengthSec());
    const float x1 = r.getX() + (float) (juce::jlimit(0.0, len, m_loopInRel)  / len) * r.getWidth();
    const float x2 = r.getX() + (float) (juce::jlimit(0.0, len, m_loopOutRel) / len) * r.getWidth();
    g.setColour(juce::Colour(0xFFFDE047).withAlpha(0.18f));
    g.fillRect(x1, r.getY(), std::max(2.0f, x2 - x1), r.getHeight());
    g.setColour(juce::Colour(0xFFFDE047));
    g.fillRect(x1 - 0.5f, r.getY(), 1.0f, r.getHeight());
    g.fillRect(x2 - 0.5f, r.getY(), 1.0f, r.getHeight());
}

void StudioClip::addVolumeAutoPoint(double t, float v) {
    m_autoVol.push_back({ t, juce::jlimit(0.0f, 1.0f, v) });
    std::sort(m_autoVol.begin(), m_autoVol.end(),
              [](const AutoPoint& a, const AutoPoint& b){ return a.timeRel < b.timeRel; });
    repaint(getLocalBounds());
}

static void addAutoP(std::vector<StudioClip::AutoPoint>& v, double t, float val) {
    v.push_back({ t, juce::jlimit(0.0f, 1.0f, val) });
    std::sort(v.begin(), v.end(),
              [](const StudioClip::AutoPoint& a, const StudioClip::AutoPoint& b){
                  return a.timeRel < b.timeRel;
              });
}

void StudioClip::addEqHiAutoPoint (double t, float v) { addAutoP(m_autoEqHi,  t, v); repaint(getLocalBounds()); }
void StudioClip::addEqMidAutoPoint(double t, float v) { addAutoP(m_autoEqMid, t, v); repaint(getLocalBounds()); }
void StudioClip::addEqLoAutoPoint (double t, float v) { addAutoP(m_autoEqLo,  t, v); repaint(getLocalBounds()); }

void StudioClip::clearAutomation() {
    m_autoVol.clear();
    m_autoEqHi.clear();
    m_autoEqMid.clear();
    m_autoEqLo.clear();
    repaint(getLocalBounds());
}

void StudioClip::paintStems(juce::Graphics& g, juce::Rectangle<float> r) {
    if (r.getHeight() < 12.0f || r.getWidth() < 8.0f) return;

    g.setColour(juce::Colour(0xFF02040A));
    g.fillRoundedRectangle(r, 3.0f);

    struct Stem {
        const char* label;
        juce::Colour col;
        float waveScale;
    };
    const Stem stems[] = {
        { "VOCALS", juce::Colour(0xFFEC4899), 0.85f },
        { "DRUMS",  juce::Colour(0xFF22C55E), 1.00f },
        { "BASS",   juce::Colour(0xFFF59E0B), 0.95f },
        { "OTHER",  juce::Colour(0xFF38BDF8), 0.70f },
    };

    const float subH = r.getHeight() / 4.0f;
    const float labelW = std::min(40.0f, r.getWidth() * 0.10f);

    for (int s = 0; s < 4; ++s) {
        const float subY = r.getY() + s * subH;
        juce::Rectangle<float> row(r.getX(), subY, r.getWidth(), subH - 1.0f);

        g.setColour(stems[s].col.withAlpha(0.10f));
        g.fillRect(row);

        g.setColour(stems[s].col.withAlpha(0.25f));
        g.fillRect(row.getX(), row.getY(), labelW, row.getHeight());
        g.setColour(stems[s].col);
        g.setFont(juce::Font(7.5f, juce::Font::bold));
        g.drawText(stems[s].label,
                   row.withWidth(labelW).toNearestInt(),
                   juce::Justification::centred);

        const auto wr = juce::Rectangle<float>(
            row.getX() + labelW + 2, row.getY() + 1,
            row.getWidth() - labelW - 4, row.getHeight() - 2);

        if (m_thumb && m_thumb->getNumSamplesFinished() > 0) {
            const double total = std::max(m_thumb->getTotalLength(), m_audioOutSec);
            const double a = juce::jlimit(0.0, total, m_audioInSec);
            const double b = juce::jlimit(a + 0.01, total, m_audioOutSec);
            g.setColour(stems[s].col.withAlpha(0.85f));
            m_thumb->drawChannels(g, wr.toNearestInt(), a, b, stems[s].waveScale);
        }

        g.setColour(juce::Colour(0xFF1F2937));
        g.fillRect(row.getX(), row.getBottom() - 1, row.getWidth(), 1.0f);
    }
}

void StudioClip::paintAutomation(juce::Graphics& g, juce::Rectangle<float> r) {
    auto drawLane = [&](const std::vector<AutoPoint>& pts, juce::Colour col) {
        if (pts.empty()) return;
        const double len = std::max(0.001, getLengthSec());
        juce::Path p;
        bool first = true;
        for (auto& pt : pts) {
            const float x = r.getX() + (float) (pt.timeRel / len) * r.getWidth();
            const float y = r.getY() + r.getHeight() * (1.0f - pt.value);
            if (first) { p.startNewSubPath(x, y); first = false; }
            else        p.lineTo(x, y);
            g.setColour(col);
            g.fillEllipse(x - 3, y - 3, 6, 6);
            g.setColour(juce::Colours::white);
            g.drawEllipse(x - 3, y - 3, 6, 6, 1.0f);
        }
        g.setColour(col.withAlpha(0.7f));
        g.strokePath(p, juce::PathStrokeType(1.5f));
    };
    drawLane(m_autoVol,   juce::Colour(0xFF22D3EE));
    drawLane(m_autoEqHi,  juce::Colour(0xFFEC4899));
    drawLane(m_autoEqMid, juce::Colour(0xFFA855F7));
    drawLane(m_autoEqLo,  juce::Colour(0xFFF59E0B));
}

void StudioClip::paintMarker(juce::Graphics& g, juce::Rectangle<float> r) {
    if (m_marker.label.empty()) return;
    auto badge = juce::Rectangle<float>(r.getX() + 4, r.getY() + 4, 64.0f, 14.0f);
    g.setColour(m_marker.colour.withAlpha(0.85f));
    g.fillRoundedRectangle(badge, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(8.5f, juce::Font::bold));
    g.drawText(juce::String(m_marker.label), badge.toNearestInt(),
               juce::Justification::centred);
}

void StudioClip::paintCues(juce::Graphics& g, juce::Rectangle<float> r) {
    if (m_cues.empty()) return;
    const double len = std::max(0.001, getLengthSec());
    for (const auto& c : m_cues) {
        if (c.position < 0.0 || c.position > len) continue;
        const float x = r.getX() + (float) (c.position / len) * r.getWidth();
        g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.8f));
        g.fillRect(x - 0.5f, r.getY(), 1.0f, r.getHeight());
    }
}

StudioTimeline::StudioTimeline() {
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
    // 1 worker suffit : la prep est bien plus courte que la duree du clip.
    m_prepPool = std::make_unique<juce::ThreadPool>(1);
    spdlog::info("[Studio] StudioTimeline ctor : keyboard focus enabled, mouse intercepts on");
}

class StudioTimeline::TransitionInlineEditor : public juce::Component {
public:
    TransitionInlineEditor() {

        m_durationSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        m_durationSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
        m_durationSlider.setRange(1.0, 32.0, 1.0);
        m_durationSlider.setValue(8.0, juce::dontSendNotification);
        m_durationSlider.setTextValueSuffix(" bars");
        m_durationSlider.setColour(juce::Slider::trackColourId, Colors::primary().withAlpha(0.6f));
        m_durationSlider.setColour(juce::Slider::thumbColourId, Colors::primary());
        m_durationSlider.setColour(juce::Slider::backgroundColourId, Colors::bgElevated());
        m_durationSlider.setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
        m_durationSlider.setColour(juce::Slider::textBoxOutlineColourId,
                                    juce::Colours::transparentBlack);
        m_durationSlider.onValueChange = [this]() { repaint(); };
        addAndMakeVisible(m_durationSlider);

        m_typeCombo.addItem("Cut",         1);
        m_typeCombo.addItem("Fade",        2);
        m_typeCombo.addItem("EqualPower",  3);
        m_typeCombo.addItem("EchoOut",     4);
        m_typeCombo.addItem("FilterSweep", 5);
        m_typeCombo.addItem("Backspin",    6);
        m_typeCombo.addItem("Custom",      7);
        m_typeCombo.setSelectedId(3, juce::dontSendNotification);
        m_typeCombo.setColour(juce::ComboBox::backgroundColourId, Colors::bgElevated());
        m_typeCombo.setColour(juce::ComboBox::textColourId,       Colors::textPrimary());
        m_typeCombo.setColour(juce::ComboBox::outlineColourId,    Colors::border());
        m_typeCombo.setColour(juce::ComboBox::arrowColourId,      Colors::primary());
        m_typeCombo.onChange = [this]() { repaint(); };
        addAndMakeVisible(m_typeCombo);

        m_autoGenButton.setButtonText("Auto-Generate AI");
        m_autoGenButton.setColour(juce::TextButton::buttonColourId, Colors::secondary().withAlpha(0.85f));
        m_autoGenButton.setColour(juce::TextButton::buttonOnColourId, Colors::secondaryHover());
        m_autoGenButton.setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        m_autoGenButton.setColour(juce::TextButton::textColourOnId,  Colors::textPrimary());
        m_autoGenButton.onClick = [this]() {
            m_typeCombo.setSelectedId(3, juce::dontSendNotification);
            m_durationSlider.setValue(8.0, juce::dontSendNotification);
            repaint();
        };
        addAndMakeVisible(m_autoGenButton);

        m_applyButton.setButtonText("Apply");
        m_applyButton.setColour(juce::TextButton::buttonColourId, Colors::primary());
        m_applyButton.setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
        m_applyButton.setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        m_applyButton.setColour(juce::TextButton::textColourOnId,  Colors::textPrimary());
        m_applyButton.onClick = [this]() {
            if (onApply) onApply(currentKind(), (int) m_durationSlider.getValue());
        };
        addAndMakeVisible(m_applyButton);

        setSize(320, 200);
    }

    ~TransitionInlineEditor() override = default;

    void configure(int gapIndex, TransitionKind currentKind, int currentBars) {
        m_gapIndex = gapIndex;
        m_durationSlider.setValue(juce::jlimit(1, 32, currentBars),
                                   juce::dontSendNotification);
        m_typeCombo.setSelectedId(kindToComboId(currentKind), juce::dontSendNotification);
        repaint();
    }

    int  getGapIndex() const noexcept { return m_gapIndex; }

    std::function<void(TransitionKind kind, int bars)> onApply;
    std::function<void()>                              onClose;

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);

        g.setColour(Colors::bgCard());
        g.fillRoundedRectangle(r, 6.0f);

        g.setColour(Colors::primary());
        g.drawRoundedRectangle(r, 6.0f, 1.5f);

        const int headerH = 24;
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
        const juce::String title = "Transition #" + juce::String(m_gapIndex + 1);
        g.drawText(title, 10, 0, getWidth() - 30, headerH,
                   juce::Justification::centredLeft);

        const auto closeRect = getCloseButtonRect();
        g.setColour(m_closeHover ? Colors::error() : Colors::textSecondary());
        g.setFont(juce::Font("Segoe UI", 16.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("\xC3\x97"), closeRect, juce::Justification::centred);

        g.setColour(Colors::border());
        g.fillRect(8.0f, (float) headerH, (float) getWidth() - 16.0f, 1.0f);

        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        g.drawText("Duration", 10, headerH + 4, 80, 14, juce::Justification::centredLeft);

        g.drawText("Type", 10, headerH + 4 + 18 + 26 + 4, 80, 14,
                   juce::Justification::centredLeft);

        const auto previewRect = getPreviewRect();
        g.setColour(Colors::bgElevated());
        g.fillRoundedRectangle(previewRect.toFloat(), 3.0f);
        g.setColour(Colors::border());
        g.drawRoundedRectangle(previewRect.toFloat(), 3.0f, 1.0f);
        paintPreviewCurve(g, previewRect.toFloat().reduced(2.0f), currentKind());

        g.setColour(Colors::textSecondary());
        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::plain));
        g.drawText("Preview",
                   previewRect.getX(), previewRect.getY() - 12,
                   previewRect.getWidth(), 11,
                   juce::Justification::centredLeft);
    }

    void resized() override {
        const int w = getWidth();
        const int h = getHeight();
        const int headerH = 24;
        const int pad = 8;

        m_durationSlider.setBounds(80, headerH + 4, w - 90, 22);

        m_typeCombo.setBounds(80, headerH + 4 + 26 + 4, w - 90, 22);

        const int btnY = h - pad - 28;
        const int btnW = (w - pad * 3) / 2;
        m_autoGenButton.setBounds(pad, btnY, btnW, 28);
        m_applyButton  .setBounds(pad * 2 + btnW, btnY, btnW, 28);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const bool over = getCloseButtonRect().contains(e.getPosition());
        if (over != m_closeHover) {
            m_closeHover = over;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (getCloseButtonRect().contains(e.getPosition())) {
            if (onClose) onClose();
            return;
        }
    }

private:
    juce::Slider     m_durationSlider;
    juce::ComboBox   m_typeCombo;
    juce::TextButton m_autoGenButton;
    juce::TextButton m_applyButton;
    int              m_gapIndex   { -1 };
    bool             m_closeHover { false };

    juce::Rectangle<int> getCloseButtonRect() const {
        return { getWidth() - 26, 2, 22, 22 };
    }

    juce::Rectangle<int> getPreviewRect() const {
        const int w = 80, h = 40;
        const int x = (getWidth() - w) / 2;
        const int y = getHeight() - 8 - 28 - 8 - h;
        return { x, y, w, h };
    }

    static int kindToComboId(TransitionKind k) {
        switch (k) {
            case TransitionKind::Cut:         return 1;
            case TransitionKind::Fade:        return 2;
            case TransitionKind::EqualPower:  return 3;
            case TransitionKind::EchoOut:     return 4;
            case TransitionKind::FilterSweep: return 5;
            case TransitionKind::Backspin:    return 6;
            case TransitionKind::Custom:      return 7;
        }
        return 3;
    }

    TransitionKind currentKind() const {
        switch (m_typeCombo.getSelectedId()) {
            case 1: return TransitionKind::Cut;
            case 2: return TransitionKind::Fade;
            case 3: return TransitionKind::EqualPower;
            case 4: return TransitionKind::EchoOut;
            case 5: return TransitionKind::FilterSweep;
            case 6: return TransitionKind::Backspin;
            case 7: return TransitionKind::Custom;
        }
        return TransitionKind::EqualPower;
    }

    static void paintPreviewCurve(juce::Graphics& g, juce::Rectangle<float> r,
                                   TransitionKind kind) {
        if (r.getWidth() < 2.0f || r.getHeight() < 2.0f) return;

        juce::Path pathA, pathB;
        const int N = 32;
        const float x0 = r.getX();
        const float y0 = r.getY();
        const float w  = r.getWidth();
        const float h  = r.getHeight();

        auto valA = [kind](float t) -> float {
            switch (kind) {
                case TransitionKind::Cut:
                    return t < 0.5f ? 1.0f : 0.0f;
                case TransitionKind::Fade:
                    return 1.0f - t;
                case TransitionKind::EqualPower:
                    return std::cos(t * juce::MathConstants<float>::halfPi);
                case TransitionKind::EchoOut:
                    return (1.0f - t) * (0.5f + 0.5f * std::cos(t * 18.0f));
                case TransitionKind::FilterSweep:
                    return std::pow(1.0f - t, 0.5f);
                case TransitionKind::Backspin:
                    return std::max(0.0f, 1.0f - t * 1.6f);
                case TransitionKind::Custom:
                    return 1.0f - t;
            }
            return 1.0f - t;
        };

        for (int i = 0; i <= N; ++i) {
            const float t = (float) i / (float) N;
            const float a = juce::jlimit(0.0f, 1.0f, valA(t));
            const float b = juce::jlimit(0.0f, 1.0f, 1.0f - a);
            const float xx = x0 + t * w;
            const float yA = y0 + (1.0f - a) * h;
            const float yB = y0 + (1.0f - b) * h;
            if (i == 0) { pathA.startNewSubPath(xx, yA); pathB.startNewSubPath(xx, yB); }
            else        { pathA.lineTo(xx, yA);          pathB.lineTo(xx, yB); }
        }

        g.setColour(Colors::primary());
        g.strokePath(pathA, juce::PathStrokeType(1.5f));
        g.setColour(Colors::accent().withAlpha(0.85f));
        g.strokePath(pathB, juce::PathStrokeType(1.5f));
    }
};

void StudioTimeline::openTransitionEditor(int gapIdx, juce::Rectangle<int> gapRect) {
    if (gapIdx < 0 || gapIdx + 1 >= (int) m_clips.size()) return;
    if (gapIdx >= (int) m_transitions.size())
        m_transitions.resize((size_t) gapIdx + 1);

    m_transitionEditor = std::make_unique<TransitionInlineEditor>();
    auto* editor = m_transitionEditor.get();
    editor->configure(gapIdx,
                      m_transitions[(size_t) gapIdx].kind,
                      m_transitions[(size_t) gapIdx].bars);
    editor->onApply = [this](TransitionKind kind, int bars) {
        const int gi = m_transitionEditor ? m_transitionEditor->getGapIndex() : -1;
        if (gi >= 0) {
            setTransitionType(gi, kind);
            setTransitionDurationBars(gi, bars);
        }
        m_transitionEditor.reset();
        repaint();
    };
    editor->onClose = [this]() {
        m_transitionEditor.reset();
        repaint();
    };

    const int W = editor->getWidth();
    const int H = editor->getHeight();
    int x = gapRect.getCentreX() - W / 2;
    int y = gapRect.getY() - H - 6;
    if (y < 4) y = std::min(getHeight() - H - 4, gapRect.getBottom() + 6);
    x = juce::jlimit(4, std::max(4, getWidth() - W - 4), x);
    y = juce::jlimit(4, std::max(4, getHeight() - H - 4), y);
    editor->setBounds(x, y, W, H);
    addAndMakeVisible(editor);
    editor->toFront(false);
    m_editingGapIdx = gapIdx;
}

void StudioTimeline::closeTransitionEditor() {
    m_transitionEditor.reset();
    m_editingGapIdx = -1;
    repaint();
}

StudioTimeline::~StudioTimeline() {
    // Vider le pool AVANT la destruction des membres, sinon use-after-free depuis un job.
    stopTimer();
    if (m_prepPool) m_prepPool->removeAllJobs(/*interrupt=*/true,
                                              /*timeoutMs=*/5000);
    if (m_audioInited) {
        m_sourcePlayer.setSource(nullptr);
        m_deviceManager.removeAudioCallback(&m_sourcePlayer);
    }
}

int StudioTimeline::addClip(const Models::Track& t) {
    double startAt = 0.0;
    for (auto& c : m_clips) {
        const double end = c->getStartSec() + c->getLengthSec();
        if (end > startAt) startAt = end;
    }
    auto clip = std::make_unique<StudioClip>(t, *this);
    clip->setStartSec(startAt);
    const int idx = (int) m_clips.size();
    auto* raw = clip.get();
    wireClipCallback(*raw);
    addAndMakeVisible(*raw);
    m_clips.push_back(std::move(clip));
    layoutClips();
    setSelectedIndex(idx);
    enqueueClipPrep(idx);
    return idx;
}

bool StudioTimeline::removeClip(int i) {
    if (i < 0 || i >= (int) m_clips.size()) return false;
    removeChildComponent(m_clips[i].get());
    m_clips.erase(m_clips.begin() + i);
    // Garder m_transitions aligne sur les gaps clip[i]->clip[i+1].
    if (!m_transitions.empty()) {
        const int gapToErase = (i < (int) m_transitions.size()) ? i
                                : (int) m_transitions.size() - 1;
        if (gapToErase >= 0 && gapToErase < (int) m_transitions.size())
            m_transitions.erase(m_transitions.begin() + gapToErase);
    }
    {
        std::lock_guard<std::mutex> lock(m_renderModelMutex);
        if (i < (int) m_clipBufs.size())
            m_clipBufs.erase(m_clipBufs.begin() + i);
    }
    if (m_selected >= (int) m_clips.size()) m_selected = (int) m_clips.size() - 1;
    layoutClips();
    m_listeners.call([](Listener& l){ l.clipsReordered(); });
    repaint();
    return true;
}

namespace {
juce::AudioBuffer<float> sliceBuffer(const juce::AudioBuffer<float>& src,
                                      int startFrame, int numFrames)
{
    if (numFrames <= 0 || startFrame < 0 || src.getNumSamples() <= 0
        || startFrame >= src.getNumSamples())
        return {};
    const int realN = std::min(numFrames, src.getNumSamples() - startFrame);
    juce::AudioBuffer<float> out(src.getNumChannels(), realN);
    for (int c = 0; c < src.getNumChannels(); ++c)
        out.copyFrom(c, 0, src, c, startFrame, realN);
    return out;
}

int framesOf(double sec, double sampleRate) noexcept {
    if (sampleRate <= 0.0 || sec <= 0.0) return 0;
    const double n = sec * sampleRate;
    return n > (double) std::numeric_limits<int>::max()
              ? std::numeric_limits<int>::max() : (int) std::round(n);
}

std::unique_ptr<StudioClip::StemData> sliceStems(const StudioClip::StemData& parent,
                                                  double startSec, double endSec)
{
    if (!parent.ready || endSec <= startSec) return nullptr;
    const int startFrame = framesOf(startSec, parent.sampleRate);
    const int numFrames  = framesOf(endSec - startSec, parent.sampleRate);
    if (numFrames <= 0) return nullptr;
    auto out = std::make_unique<StudioClip::StemData>();
    out->sampleRate = parent.sampleRate;
    out->vocals = sliceBuffer(parent.vocals, startFrame, numFrames);
    out->drums  = sliceBuffer(parent.drums,  startFrame, numFrames);
    out->bass   = sliceBuffer(parent.bass,   startFrame, numFrames);
    out->other  = sliceBuffer(parent.other,  startFrame, numFrames);
    out->ready  = (out->vocals.getNumSamples() > 0)
               || (out->drums.getNumSamples()  > 0)
               || (out->bass.getNumSamples()   > 0)
               || (out->other.getNumSamples()  > 0);
    return out;
}

// Copie les decorations editables ; copyStems=false pour un split (slicer manuellement).
void cloneClipDecorations(const StudioClip& src, StudioClip& dst,
                           bool copyStems = true)
{
    dst.setFadeIn (src.getFade().inSec);
    dst.setFadeOut(src.getFade().outSec);
    dst.setVolume  (src.getVolume());
    dst.setReversed(src.isReversed());
    dst.setLocked  (src.isLocked());
    dst.setMuted   (src.isMuted());
    dst.setPitchSemitones(src.getPitchSemitones());
    dst.setTempoRatio    (src.getTempoRatio());

    for (const auto& fx : src.getEffects()) {
        dst.addEffect(fx.type);
        const int newIdx = (int) dst.getEffects().size() - 1;
        for (const auto& kv : fx.params)
            dst.setEffectParam(newIdx, kv.first, kv.second);
        dst.setEffectEnabled(newIdx, fx.enabled);
    }

    for (const auto& p : src.getVolumeAutomation()) dst.addVolumeAutoPoint(p.timeRel, p.value);
    for (const auto& p : src.getEqHiAutomation())   dst.addEqHiAutoPoint  (p.timeRel, p.value);
    for (const auto& p : src.getEqMidAutomation())  dst.addEqMidAutoPoint (p.timeRel, p.value);
    for (const auto& p : src.getEqLoAutomation())   dst.addEqLoAutoPoint  (p.timeRel, p.value);

    if (copyStems && src.hasStems()) {
        if (auto* srcStems = const_cast<StudioClip&>(src).getStems()) {
            auto copy = std::make_unique<StudioClip::StemData>();
            copy->vocals     = srcStems->vocals;
            copy->drums      = srcStems->drums;
            copy->bass       = srcStems->bass;
            copy->other      = srcStems->other;
            copy->ready      = srcStems->ready;
            copy->sampleRate = srcStems->sampleRate;
            dst.attachStems(std::move(copy));
            for (int i = 0; i < 4; ++i) dst.setStemMute(i, src.isStemMuted(i));
        }
    }

    dst.setGroupId(src.getGroupId());

    dst.setCues(src.getCues());
    {
        const auto loop = src.getLoop();
        if (loop.on) dst.setLoop(loop.inRel, loop.outRel, true);
    }
    {
        const auto& mk = src.getMarker();
        if (! mk.label.empty()) dst.setMarker(mk.label, mk.colour);
    }
}

void reprojectAutomationForSplit(StudioClip& clip,
                                  double oldDur,
                                  double newDur,
                                  double offset,
                                  bool isRightSide)
{
    if (oldDur <= 1e-6 || newDur <= 1e-6) return;
    auto vol = clip.getVolumeAutomation();
    auto hi  = clip.getEqHiAutomation();
    auto mid = clip.getEqMidAutomation();
    auto lo  = clip.getEqLoAutomation();
    clip.clearAutomation();

    auto reproj = [&](const std::vector<StudioClip::AutoPoint>& pts,
                       const std::function<void(double, float)>& add) {
        for (const auto& p : pts) {
            const double absT = p.timeRel * oldDur;
            if (isRightSide) {
                if (absT <= offset) continue;            // appartient au gauche
                const double newT = (absT - offset) / newDur;
                if (newT >= 0.0 && newT <= 1.0) add(newT, p.value);
            } else {
                if (absT > offset) continue;             // appartient au droit
                const double newT = absT / newDur;
                if (newT >= 0.0 && newT <= 1.0) add(newT, p.value);
            }
        }
    };
    reproj(vol, [&](double t, float v){ clip.addVolumeAutoPoint(t, v); });
    reproj(hi,  [&](double t, float v){ clip.addEqHiAutoPoint  (t, v); });
    reproj(mid, [&](double t, float v){ clip.addEqMidAutoPoint (t, v); });
    reproj(lo,  [&](double t, float v){ clip.addEqLoAutoPoint  (t, v); });
}

StudioTimeline::ClipboardEntry buildClipboardEntry(const StudioClip& src,
                                                    double audioInOverride,
                                                    double audioOutOverride,
                                                    double startRelToFirst)
{
    StudioTimeline::ClipboardEntry e;
    e.track    = src.getTrack();
    e.audioIn  = (audioInOverride  >= 0.0) ? audioInOverride  : src.getAudioInSec();
    e.audioOut = (audioOutOverride >= 0.0) ? audioOutOverride : src.getAudioOutSec();
    e.fadeIn   = src.getFade().inSec;
    e.fadeOut  = src.getFade().outSec;
    e.volume   = src.getVolume();
    e.reversed = src.isReversed();
    e.muted    = src.isMuted();
    e.locked   = src.isLocked();
    e.groupId  = src.getGroupId();
    e.effects  = src.getEffects();
    e.autoVol  = src.getVolumeAutomation();
    e.autoEqHi = src.getEqHiAutomation();
    e.autoEqMid= src.getEqMidAutomation();
    e.autoEqLo = src.getEqLoAutomation();
    e.cues   = src.getCues();
    {
        const auto loop = src.getLoop();
        e.loopInRel  = loop.inRel;
        e.loopOutRel = loop.outRel;
        e.loopOn     = loop.on;
    }
    e.marker = src.getMarker();
    e.pitchSemitones = src.getPitchSemitones();
    e.tempoRatio     = src.getTempoRatio();
    e.startRelToFirst = startRelToFirst;
    return e;
}

// startSec reste la responsabilite de l appelant.
void applyClipboardEntry(const StudioTimeline::ClipboardEntry& src, StudioClip& dst)
{
    dst.setFadeIn (src.fadeIn);
    dst.setFadeOut(src.fadeOut);
    dst.setVolume  (src.volume);
    dst.setReversed(src.reversed);
    dst.setLocked  (src.locked);
    dst.setMuted   (src.muted);
    dst.setGroupId (src.groupId);

    for (const auto& fx : src.effects) {
        dst.addEffect(fx.type);
        const int newIdx = (int) dst.getEffects().size() - 1;
        for (const auto& kv : fx.params)
            dst.setEffectParam(newIdx, kv.first, kv.second);
        dst.setEffectEnabled(newIdx, fx.enabled);
    }

    for (const auto& p : src.autoVol)  dst.addVolumeAutoPoint(p.timeRel, p.value);
    for (const auto& p : src.autoEqHi) dst.addEqHiAutoPoint  (p.timeRel, p.value);
    for (const auto& p : src.autoEqMid)dst.addEqMidAutoPoint (p.timeRel, p.value);
    for (const auto& p : src.autoEqLo) dst.addEqLoAutoPoint  (p.timeRel, p.value);
    if (! src.cues.empty()) dst.setCues(src.cues);
    if (src.loopOn && src.loopOutRel > src.loopInRel)
        dst.setLoop(src.loopInRel, src.loopOutRel, true);
    if (! src.marker.label.empty()) dst.setMarker(src.marker.label, src.marker.colour);
    dst.setPitchSemitones(src.pitchSemitones);
    dst.setTempoRatio    (src.tempoRatio);
}
} // namespace

bool StudioTimeline::duplicateClip(int i) {
    if (i < 0 || i >= (int) m_clips.size()) return false;
    pushUndoSnapshot();
    const auto& src = *m_clips[i];
    auto dup = std::make_unique<StudioClip>(src.getTrack(), *this);
    dup->setAudioRange(src.getAudioInSec(), src.getAudioOutSec());
    cloneClipDecorations(src, *dup);
    const double rawStart = src.getStartSec() + src.getLengthSec();
    dup->setStartSec(snap(rawStart));
    auto* raw = dup.get();
    wireClipCallback(*raw);
    addAndMakeVisible(*raw);
    m_clips.insert(m_clips.begin() + i + 1, std::move(dup));
    layoutClips();
    setSelectedIndex(i + 1);
    enqueueClipPrep(i + 1);
    return true;
}

bool StudioTimeline::splitAtPlayhead() {
    const double splitSec = snap(m_playhead);
    for (int i = 0; i < (int) m_clips.size(); ++i) {
        auto& c = *m_clips[i];
        const double s   = c.getStartSec();
        const double e   = s + c.getLengthSec();
        if (splitSec <= s + kMinClipLengthSec || splitSec >= e - kMinClipLengthSec)
            continue;

        pushUndoSnapshot();
        const double offset    = splitSec - s;             // duree gauche en sec
        const double oldDur    = c.getLengthSec();          // duree totale
        const double origOut   = c.getAudioOutSec();
        const double origIn    = c.getAudioInSec();
        const double newOut    = origIn + offset;           // nouvel audioOut du gauche

        // Preparer les stems AVANT de muter le clip gauche (troncature sample-precise).
        std::unique_ptr<StudioClip::StemData> leftStems;
        std::unique_ptr<StudioClip::StemData> rightStems;
        bool stemMutes[4] { false, false, false, false };
        if (c.hasStems()) {
            if (auto* parentStems = c.getStems()) {
                leftStems  = sliceStems(*parentStems, 0.0,    offset);
                rightStems = sliceStems(*parentStems, offset, oldDur);
                for (int k = 0; k < 4; ++k) stemMutes[k] = c.isStemMuted(k);
            }
        }

        auto right = std::make_unique<StudioClip>(c.getTrack(), *this);
        right->setAudioRange(newOut, origOut);
        right->setStartSec(splitSec);
        cloneClipDecorations(c, *right, /*copyStems=*/false);
        right->setFadeIn(0.0);
        if (rightStems) {
            right->attachStems(std::move(rightStems));
            for (int k = 0; k < 4; ++k) right->setStemMute(k, stemMutes[k]);
        }

        c.setFadeOut(0.0);
        c.setAudioRange(origIn, newOut);
        if (c.hasStems() && leftStems) {
            c.attachStems(std::move(leftStems));
            for (int k = 0; k < 4; ++k) c.setStemMute(k, stemMutes[k]);
        }
        reprojectAutomationForSplit(c,      oldDur, offset,           offset, false);
        reprojectAutomationForSplit(*right, oldDur, oldDur - offset,  offset, true);

        auto* raw = right.get();
        wireClipCallback(*raw);
        addAndMakeVisible(*raw);
        m_clips.insert(m_clips.begin() + i + 1, std::move(right));
        if (i < (int) m_transitions.size())
            m_transitions.insert(m_transitions.begin() + i, GapTransition{});
        layoutClips();
        enqueueClipPrep(i);
        enqueueClipPrep(i + 1);
        return true;
    }
    return false;
}

void StudioTimeline::reorderClip(int from, int to) {
    if (from < 0 || from >= (int) m_clips.size()) return;
    if (to   < 0 || to   >= (int) m_clips.size()) return;
    auto tmp = std::move(m_clips[from]);
    m_clips.erase(m_clips.begin() + from);
    m_clips.insert(m_clips.begin() + to, std::move(tmp));
    {
        std::lock_guard<std::mutex> lock(m_renderModelMutex);
        if (from < (int) m_clipBufs.size() && to < (int) m_clipBufs.size()) {
            auto bufTmp = std::move(m_clipBufs[(size_t) from]);
            m_clipBufs.erase(m_clipBufs.begin() + from);
            m_clipBufs.insert(m_clipBufs.begin() + to, std::move(bufTmp));
        }
    }
    layoutClips();
    m_listeners.call([](Listener& l){ l.clipsReordered(); });
}

StudioClip* StudioTimeline::getClip(int i) const {
    if (i < 0 || i >= (int) m_clips.size()) return nullptr;
    return m_clips[i].get();
}

int StudioTimeline::indexOf(const StudioClip* c) const {
    for (int i = 0; i < (int) m_clips.size(); ++i) {
        if (m_clips[i].get() == c) return i;
    }
    return -1;
}

void StudioTimeline::setSelectedIndex(int i) {
    spdlog::info("[Studio] setSelectedIndex: {} â†’ {}", m_selected, i);
    if (i == m_selected) return;
    for (int k = 0; k < (int) m_clips.size(); ++k) {
        m_clips[k]->setSelected(k == i);
    }
    m_selected = i;
    m_listeners.call([i](Listener& l){ l.clipSelected(i); });
}

void StudioTimeline::selectAll() {
    for (auto& c : m_clips) c->setSelected(true);
    m_selected = m_clips.empty() ? -1 : 0;
    repaint();
}

void StudioTimeline::deselectAll() {
    for (auto& c : m_clips) c->setSelected(false);
    m_selected = -1;
    repaint();
}

void StudioTimeline::setPlayheadSec(double s) {
    const double oldSec = m_playhead;
    m_playhead = std::max(0.0, s);
    // Propager le seek vers l audio thread ; tolerance 2 frames contre le store circulaire.
    if (m_playing.load()) {
        const double srRef = (m_playbackSr > 0.0) ? m_playbackSr : 44100.0;
        const int64_t newFrame = (int64_t)(m_playhead * srRef);
        const int64_t curFrame = m_playbackFrame.load();
        if (std::abs(newFrame - curFrame) > 2) {
            m_playbackFrame.store(newFrame);
        }
    }
    m_listeners.call([s](Listener& l){ l.playheadMoved(s); });
    const float pps = (float) pixelsPerSec();
    const int x0 = (int) std::round(std::min(oldSec, m_playhead) * pps) - 2;
    const int x1 = (int) std::round(std::max(oldSec, m_playhead) * pps) + 2;
    repaint(juce::Rectangle<int>(x0, 0, std::max(4, x1 - x0), getHeight()));
}

void StudioTimeline::jumpToEnd() {
    setPlayheadSec(getTotalLengthSec());
}

void StudioTimeline::nudgePlayhead(double delta) {
    setPlayheadSec(juce::jlimit(0.0, getTotalLengthSec(), m_playhead + delta));
}

void StudioTimeline::jumpToNextTransition() {
    double best = std::numeric_limits<double>::max();
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        const double end = m_clips[i]->getStartSec() + m_clips[i]->getLengthSec();
        if (end > m_playhead + 0.01 && end < best) best = end;
    }
    if (best != std::numeric_limits<double>::max()) setPlayheadSec(best);
}

void StudioTimeline::jumpToPrevTransition() {
    double best = -1.0;
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        const double end = m_clips[i]->getStartSec() + m_clips[i]->getLengthSec();
        if (end < m_playhead - 0.01 && end > best) best = end;
    }
    if (best > 0.0) setPlayheadSec(best);
}

void StudioTimeline::setZoom(double z) {
    m_zoom = juce::jlimit(0.05, 1500.0, z);
    layoutClips();
    repaint();
}

static void zoomAndRecenter(StudioTimeline& tl, double factor) {
    auto* vp = tl.findParentComponentOfClass<juce::Viewport>();
    double anchorSec = tl.getPlayheadSec();
    int    anchorViewportX = -1;

    if (vp != nullptr) {
        const int vw = vp->getViewWidth();
        const int vx = vp->getViewPositionX();
        const float pps = (float) tl.pixelsPerSec();
        const int playX = (int) std::round(tl.getPlayheadSec() * pps) + 180;
        const bool playheadVisible = (playX >= vx && playX <= vx + vw);
        if (playheadVisible) {
            anchorSec = tl.getPlayheadSec();
            anchorViewportX = playX - vx;
        } else {
            const int centerScreenX = vx + vw / 2;
            anchorSec = std::max(0.0, (centerScreenX - 180) / std::max(1.0, (double) pps));
            anchorViewportX = vw / 2;
        }
    }

    tl.setZoom(juce::jlimit(0.05, 200.0, tl.getZoom() * factor));

    if (vp != nullptr && anchorViewportX >= 0) {
        const float pps2 = (float) tl.pixelsPerSec();
        const int newAnchorX = (int) std::round(anchorSec * pps2) + 180;
        const int target = std::max(0, newAnchorX - anchorViewportX);
        vp->setViewPosition(target, vp->getViewPositionY());
    }
}

void StudioTimeline::zoomIn()  { zoomAndRecenter(*this, 1.25); }
void StudioTimeline::zoomOut() { zoomAndRecenter(*this, 0.80); }

void StudioTimeline::fitView() {
    const double total = std::max(1.0, getTotalLengthSec());
    const double wanted = (double) getWidth() / (total * 40.0);
    setZoom(std::max(0.1, wanted));
}

void StudioTimeline::cutSelection() {
    pushUndoSnapshot();
    if (hasTimeRange()) {
        copySelection();
        const double rs = m_rangeStartSec;
        const double re = m_rangeEndSec;
        std::vector<std::unique_ptr<StudioClip>> rebuilt;
        rebuilt.reserve(m_clips.size() * 2);
        for (auto& cp : m_clips) {
            const double cs = cp->getStartSec();
            const double ce = cs + cp->getLengthSec();
            if (re <= cs || rs >= ce) {
                rebuilt.push_back(std::move(cp));
                continue;
            }
            const double leftLen  = std::max(0.0, rs - cs);
            const double rightLen = std::max(0.0, ce - re);
            const double aIn0 = cp->getAudioInSec();
            const StudioClip::StemData* parentStems
                = cp->hasStems() ? cp->getStems() : nullptr;
            bool stemMutes[4] { false, false, false, false };
            if (parentStems) {
                for (int k = 0; k < 4; ++k) stemMutes[k] = cp->isStemMuted(k);
            }
            if (leftLen > 0.05) {
                auto left = std::make_unique<StudioClip>(cp->getTrack(), *this);
                left->setAudioRange(aIn0, aIn0 + leftLen);
                left->setStartSec(cs);
                cloneClipDecorations(*cp, *left, /*copyStems=*/false);
                left->setFadeOut(0.0);   // bord interne du cut â†’ pas de fade-out.
                if (parentStems) {
                    if (auto sliced = sliceStems(*parentStems, 0.0, leftLen)) {
                        left->attachStems(std::move(sliced));
                        for (int k = 0; k < 4; ++k) left->setStemMute(k, stemMutes[k]);
                    }
                }
                addAndMakeVisible(*left);
                rebuilt.push_back(std::move(left));
            }
            if (rightLen > 0.05) {
                const double rightOffsetInParent = re - cs;
                auto right = std::make_unique<StudioClip>(cp->getTrack(), *this);
                right->setAudioRange(aIn0 + rightOffsetInParent,
                                      aIn0 + rightOffsetInParent + rightLen);
                right->setStartSec(re);
                cloneClipDecorations(*cp, *right, /*copyStems=*/false);
                right->setFadeIn(0.0);   // bord interne du cut â†’ pas de fade-in.
                if (parentStems) {
                    if (auto sliced = sliceStems(*parentStems,
                                                  rightOffsetInParent,
                                                  rightOffsetInParent + rightLen)) {
                        right->attachStems(std::move(sliced));
                        for (int k = 0; k < 4; ++k) right->setStemMute(k, stemMutes[k]);
                    }
                }
                addAndMakeVisible(*right);
                rebuilt.push_back(std::move(right));
            }
        }
        m_clips = std::move(rebuilt);
        {
            std::lock_guard<std::mutex> lk(m_renderModelMutex);
            m_clipBufs.clear();
        }
        for (auto& cp : m_clips) {
            auto* raw = cp.get();
            wireClipCallback(*raw);
        }
        for (int k = 0; k < (int) m_clips.size(); ++k) enqueueClipPrep(k);
        clearTimeRange();
        layoutClips();
        return;
    }

    std::vector<int> indices;
    if (m_selected >= 0 && m_selected < (int) m_clips.size()) {
        indices.push_back(m_selected);
    } else {
        indices = m_multiSel;
    }
    if (indices.empty()) return;
    copySelection();
    std::sort(indices.rbegin(), indices.rend());
    for (int idx : indices) {
        if (idx >= 0 && idx < (int) m_clips.size()) removeClip(idx);
    }
}

void StudioTimeline::copySelection() {
    m_clipboard.clear();

    if (hasTimeRange()) {
        const double anchor = m_rangeStartSec;
        for (auto& cp : m_clips) {
            const double cs = cp->getStartSec();
            const double ce = cp->getStartSec() + cp->getLengthSec();
            const double s  = std::max(m_rangeStartSec, cs);
            const double e  = std::min(m_rangeEndSec,   ce);
            if (e <= s + 0.01) continue;
            const double rel0 = s - cs;
            const double rel1 = e - cs;
            const double aIn  = cp->getAudioInSec() + rel0;
            const double aOut = cp->getAudioInSec() + rel1;
            // Les bordures du range ne portent jamais de fade (comme un split).
            auto entry = buildClipboardEntry(*cp, aIn, aOut, s - anchor);
            entry.fadeIn  = 0.0;
            entry.fadeOut = 0.0;
            m_clipboard.push_back(std::move(entry));
        }
        spdlog::info("[Studio] copySelection RANGE [{:.2f}..{:.2f}] -> {} sub-clip(s)",
                     m_rangeStartSec, m_rangeEndSec, (int) m_clipboard.size());
        return;
    }

    std::vector<int> indices;
    if (m_selected >= 0 && m_selected < (int) m_clips.size()) {
        indices.push_back(m_selected);
    } else {
        for (int idx : m_multiSel) {
            if (idx >= 0 && idx < (int) m_clips.size()) indices.push_back(idx);
        }
    }
    if (indices.empty()) return;
    std::sort(indices.begin(), indices.end());
    const double anchor = m_clips[indices.front()]->getStartSec();
    for (int idx : indices) {
        auto& c = *m_clips[idx];
        m_clipboard.push_back(buildClipboardEntry(c, -1.0, -1.0,
                                                   c.getStartSec() - anchor));
    }
    spdlog::info("[Studio] copySelection OK: {} clip(s) on clipboard (full state)",
                 (int) m_clipboard.size());
}

void StudioTimeline::pasteAtPlayhead() {
    pasteAt(m_playhead);
}

void StudioTimeline::pasteAt(double atSec) {
    spdlog::info("[Studio] pasteAt: clipboard size={}, atSec={:.3f}",
                 (int) m_clipboard.size(), atSec);
    if (m_clipboard.empty()) {
        spdlog::warn("[Studio] pasteAt ABORT: clipboard empty");
        return;
    }
    pushUndoSnapshot();
    const double base = std::max(0.0, snap(atSec));

    bool anyRel = false;
    for (const auto& e : m_clipboard) if (e.startRelToFirst > 1e-6) { anyRel = true; break; }

    double sequentialPos = base;
    for (const auto& e : m_clipboard) {
        const double targetStart = anyRel ? (base + e.startRelToFirst)
                                          : sequentialPos;
        auto clip = std::make_unique<StudioClip>(e.track, *this);
        clip->setAudioRange(e.audioIn, e.audioOut);
        clip->setStartSec(targetStart);
        applyClipboardEntry(e, *clip);
        auto* raw = clip.get();
        wireClipCallback(*raw);
        addAndMakeVisible(*raw);
        m_clips.push_back(std::move(clip));
        enqueueClipPrep((int) m_clips.size() - 1);
        sequentialPos = targetStart + std::max(0.05, e.audioOut - e.audioIn);
    }
    layoutClips();
    spdlog::info("[Studio] pasteAt OK: now {} clips (full-state apply)",
                 (int) m_clips.size());
}

void StudioTimeline::pasteAtMouse() {
    if (m_lastMouseTimelineX >= 0) {
        pasteAt(secAtX(m_lastMouseTimelineX));
        return;
    }
    pasteAtPlayhead();
}

void StudioTimeline::deleteSelection() {
    spdlog::info("[Studio] deleteSelection: m_selected={}, multiSel={}",
                 m_selected, (int) m_multiSel.size());
    std::vector<int> indices;
    if (m_selected >= 0 && m_selected < (int) m_clips.size()) {
        indices.push_back(m_selected);
    } else {
        indices = m_multiSel;
    }
    if (indices.empty()) {
        spdlog::warn("[Studio] deleteSelection ABORT: no clip selected");
        return;
    }
    std::sort(indices.rbegin(), indices.rend());
    for (int idx : indices) {
        if (idx >= 0 && idx < (int) m_clips.size()) removeClip(idx);
    }
}

void StudioTimeline::quantizeAllClips() {
    const double beat = 60.0 / std::max(1.0, m_masterBpm);
    for (auto& c : m_clips) {
        const double q = std::round(c->getStartSec() / beat) * beat;
        c->setStartSec(q);
    }
    layoutClips();
    repaint();
}

void StudioTimeline::alignClipToNextBeat(int i) {
    if (i < 0 || i >= (int) m_clips.size()) return;
    const double beat = 60.0 / std::max(1.0, m_masterBpm);
    const double snapped = std::ceil(m_clips[i]->getStartSec() / beat) * beat;
    m_clips[i]->setStartSec(snapped);
    layoutClips();
}

void StudioTimeline::alignClipToPrevBeat(int i) {
    if (i < 0 || i >= (int) m_clips.size()) return;
    const double beat = 60.0 / std::max(1.0, m_masterBpm);
    const double snapped = std::floor(m_clips[i]->getStartSec() / beat) * beat;
    m_clips[i]->setStartSec(snapped);
    layoutClips();
}

void StudioTimeline::setShowStems(bool on) {
    m_showStems = on;
    for (auto& c : m_clips) c->setShowStems(on);
    repaint();
}

void StudioTimeline::beginMashupAssist(AssistMode m) {
    m_assist = m;
    spdlog::info("[Studio] Assist mode: {}",
                 m == AssistMode::Manual    ? "Manual"
               : m == AssistMode::MashupAI  ? "Mashup IA"
               : m == AssistMode::MegamixAI ? "Megamix IA"
                                            : "Medley IA");
    if (onMashupAssistRequested) onMashupAssistRequested();
    switch (m) {
        case AssistMode::MashupAI:  runMashupAI();  break;
        case AssistMode::MegamixAI: runMegamixAI(); break;
        case AssistMode::MedleyAI:  runMedleyAI();  break;
        default: break;
    }
}

void StudioTimeline::toggleSelectClip(int i) {
    if (i < 0 || i >= (int) m_clips.size()) return;
    auto it = std::find(m_multiSel.begin(), m_multiSel.end(), i);
    if (it == m_multiSel.end()) {
        m_multiSel.push_back(i);
        m_clips[i]->setSelected(true);
    } else {
        m_multiSel.erase(it);
        m_clips[i]->setSelected(false);
    }
    repaint();
}

void StudioTimeline::rubberBandSelect(juce::Rectangle<int> r) {
    m_multiSel.clear();
    for (int i = 0; i < (int) m_clips.size(); ++i) {
        if (m_clips[i]->getBounds().intersects(r)) {
            m_multiSel.push_back(i);
            m_clips[i]->setSelected(true);
        } else {
            m_clips[i]->setSelected(false);
        }
    }
    repaint();
}

void StudioTimeline::pushUndoSnapshot() {
    UndoSnapshot snap;
    snap.playhead         = m_playhead;
    snap.masterBpm        = m_masterBpm;
    snap.tsigNum          = m_tsigNum;
    snap.tsigDen          = m_tsigDen;
    snap.masterVol        = m_masterVol.load();
    snap.masterCompAmount = m_masterCompAmount.load();

    for (auto& c : m_clips) {
        UndoSnapshot::ClipState cs;
        cs.trackId   = c->getTrack().id;
        cs.filePath  = c->getTrack().filePath;
        cs.title     = c->getTrack().title;
        cs.startSec  = c->getStartSec();
        cs.audioIn   = c->getAudioInSec();
        cs.audioOut  = c->getAudioOutSec();
        cs.fadeIn    = c->getFade().inSec;
        cs.fadeOut   = c->getFade().outSec;
        cs.locked    = c->isLocked();
        cs.muted     = c->isMuted();
        cs.reversed  = c->isReversed();
        cs.volume    = c->getVolume();
        cs.groupId   = c->getGroupId();
        cs.autoVol   = c->getVolumeAutomation();
        cs.autoEqHi  = c->getEqHiAutomation();
        cs.autoEqMid = c->getEqMidAutomation();
        cs.autoEqLo  = c->getEqLoAutomation();
        cs.effects   = c->getEffects();
        for (int i = 0; i < 4; ++i) cs.stemMutes[i] = c->isStemMuted(i);
        const auto loop = c->getLoop();
        cs.loopInRel  = loop.inRel;
        cs.loopOutRel = loop.outRel;
        cs.loopOn     = loop.on;
        cs.cues       = c->getCues();
        cs.marker     = c->getMarker();
        cs.pitchSemitones = c->getPitchSemitones();
        cs.tempoRatio     = c->getTempoRatio();
        snap.clips.push_back(std::move(cs));
    }

    snap.lanes = m_laneInfos;

    for (const auto& g : m_transitions) {
        UndoSnapshot::GapState gs;
        gs.kind   = g.kind;
        gs.bars   = g.bars;
        gs.custom = g.customCurve;
        snap.gaps.push_back(std::move(gs));
    }

    snap.tempoPoints       = m_tempoPoints;
    snap.phrases           = m_phrases;
    snap.snap              = m_snap;
    snap.zoom              = m_zoom;
    snap.centerPlayhead    = m_centerPlayhead;
    snap.showBeatGrid      = m_showBeatGrid;
    snap.showStems         = m_showStems;
    snap.assist            = m_assist;
    snap.rangeStartSec     = m_rangeStartSec;
    snap.rangeEndSec       = m_rangeEndSec;
    snap.rangeLoopEnabled  = m_rangeLoopEnabled;

    m_undo.push_back(std::move(snap));
    if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void StudioTimeline::applySnapshot(const UndoSnapshot& s) {
    for (auto& c : m_clips) removeChildComponent(c.get());
    m_clips.clear();
    m_selected = -1;
    m_multiSel.clear();
    for (auto& cs : s.clips) {
        Models::Track t;
        t.id = cs.trackId;
        t.filePath = cs.filePath;
        t.title    = cs.title;
        t.duration = cs.audioOut > 0 ? cs.audioOut : 1.0;
        auto clip = std::make_unique<StudioClip>(t, *this);
        clip->setAudioRange(cs.audioIn, cs.audioOut);
        clip->setStartSec(cs.startSec);
        clip->setFadeIn(cs.fadeIn);
        clip->setFadeOut(cs.fadeOut);
        clip->setLocked(cs.locked);
        clip->setMuted(cs.muted);
        clip->setReversed(cs.reversed);
        clip->setVolume(cs.volume);
        clip->setGroupId(cs.groupId);
        for (auto& p : cs.autoVol)   clip->addVolumeAutoPoint(p.timeRel, p.value);
        for (auto& p : cs.autoEqHi)  clip->addEqHiAutoPoint  (p.timeRel, p.value);
        for (auto& p : cs.autoEqMid) clip->addEqMidAutoPoint (p.timeRel, p.value);
        for (auto& p : cs.autoEqLo)  clip->addEqLoAutoPoint  (p.timeRel, p.value);
        for (auto& fx : cs.effects) {
            clip->addEffect(fx.type);
            const int fxIdx = (int) clip->getEffects().size() - 1;
            for (auto& kv : fx.params) clip->setEffectParam(fxIdx, kv.first, kv.second);
        }
        for (int i = 0; i < 4; ++i) clip->setStemMute(i, cs.stemMutes[i]);
        if (cs.loopOn) clip->setLoop(cs.loopInRel, cs.loopOutRel, true);
        if (! cs.cues.empty()) clip->setCues(cs.cues);
        if (! cs.marker.label.empty()) clip->setMarker(cs.marker.label, cs.marker.colour);
        clip->setPitchSemitones(cs.pitchSemitones);
        clip->setTempoRatio    (cs.tempoRatio);

        addAndMakeVisible(*clip);
        m_clips.push_back(std::move(clip));
    }

    m_transitions.clear();
    for (const auto& gs : s.gaps) {
        GapTransition g;
        g.kind = gs.kind;
        g.bars = gs.bars;
        g.customCurve = gs.custom;
        m_transitions.push_back(std::move(g));
    }

    m_laneInfos = s.lanes;

    m_playhead         = s.playhead;
    m_masterBpm        = s.masterBpm;
    m_tsigNum          = s.tsigNum;
    m_tsigDen          = s.tsigDen;
    m_masterVol.store(s.masterVol);
    m_masterCompAmount.store(s.masterCompAmount);

    m_tempoPoints      = s.tempoPoints;
    m_phrases          = s.phrases;
    m_snap             = s.snap;
    m_zoom             = s.zoom;
    m_centerPlayhead   = s.centerPlayhead;
    m_showBeatGrid     = s.showBeatGrid;
    m_showStems        = s.showStems;
    m_assist           = s.assist;
    m_rangeStartSec    = s.rangeStartSec;
    m_rangeEndSec      = s.rangeEndSec;
    m_rangeLoopEnabled = s.rangeLoopEnabled;

    {
        std::lock_guard<std::mutex> lk(m_renderModelMutex);
        m_clipBufs.clear();
    }
    for (auto& cp : m_clips) {
        wireClipCallback(*cp);
    }
    for (int k = 0; k < (int) m_clips.size(); ++k) enqueueClipPrep(k);

    layoutClips();
    repaint();
}

void StudioTimeline::undo() {
    if (m_undo.empty()) return;
    // Guard : il faut m_undo.size() >= 2 apres le push pour popper current puis target.
    pushUndoSnapshot();
    if (m_undo.size() < 2) {
        m_undo.pop_back();   // rollback du push qu'on vient de faire
        return;
    }
    auto cur = std::move(m_undo.back()); m_undo.pop_back();   // pop le current
    auto target = std::move(m_undo.back()); m_undo.pop_back(); // pop le précédent
    m_redo.push_back(std::move(cur));
    applySnapshot(target);
}

void StudioTimeline::redo() {
    if (m_redo.empty()) return;
    const auto savedRedo = std::move(m_redo);
    m_redo.clear();
    pushUndoSnapshot();
    m_redo = savedRedo;
    auto target = std::move(m_redo.back()); m_redo.pop_back();
    applySnapshot(target);
}

void StudioTimeline::setClipMute(int i, bool muted) {
    if (auto* c = getClip(i)) c->setMuted(muted);
    repaint();
}

void StudioTimeline::applyMashupPlan(const BeatMate::Services::AI::MashupResult& plan) {
    if (!plan.ok) return;
    pushUndoSnapshot();

    for (auto& c : m_clips) removeChildComponent(c.get());
    m_clips.clear();
    m_transitions.clear();
    m_selected = -1;
    m_multiSel.clear();

    if (plan.averageBpm > 60.0 && plan.averageBpm < 220.0)
        m_masterBpm = plan.averageBpm;

    juce::AudioFormatManager planFmt;
    planFmt.registerBasicFormats();
    juce::WavAudioFormat wavFmt;
    const juce::File tmpDir = juce::File::getSpecialLocation(
                                 juce::File::tempDirectory).getChildFile("BeatMate_Mashup");
    tmpDir.createDirectory();

    for (const auto& cp : plan.clips) {
        Models::Track track = cp.track;
        const double srcLen = std::max(0.01, cp.sourceEndSec - cp.sourceStartSec);
        double inSec  = cp.sourceStartSec;
        double outSec = cp.sourceStartSec + srcLen;

        const bool needStretch = std::abs(cp.bpmAdjustRatio - 1.0) > 1e-3
                              || std::abs(cp.pitchSemitones)     > 1e-3;

        if (needStretch && !track.filePath.empty()) {
            juce::File src(juce::String::fromUTF8(track.filePath.c_str()));
            std::unique_ptr<juce::AudioFormatReader> reader(planFmt.createReaderFor(src));
            if (reader) {
                const int    sr   = (int) reader->sampleRate;
                const int    chs  = (int) std::max(1u, reader->numChannels);
                const int64_t s0  = (int64_t)(cp.sourceStartSec * sr);
                const int64_t s1  = (int64_t)((cp.sourceStartSec + srcLen) * sr);
                const int     N   = (int) std::max<int64_t>(0, s1 - s0);
                if (N > 0) {
                    juce::AudioBuffer<float> buf(chs, N);
                    reader->read(&buf, 0, N, s0, true, true);

                    soundtouch::SoundTouch st;
                    st.setSampleRate((unsigned int) sr);
                    st.setChannels((unsigned int) chs);
                    // Params SoundTouch choisis pour preserver les vocals (offline, cout CPU acceptable).
                    st.setSetting(SETTING_USE_AA_FILTER, 1);
                    st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
                    st.setSetting(SETTING_USE_QUICKSEEK, 0);
                    st.setSetting(SETTING_SEQUENCE_MS,    60);
                    st.setSetting(SETTING_SEEKWINDOW_MS,  25);
                    st.setSetting(SETTING_OVERLAP_MS,     12);
                    // bpmAdjustRatio = newBpm / oldBpm. Tempo > 1 = plus rapide.
                    st.setTempo(juce::jlimit(0.25, 4.0, cp.bpmAdjustRatio));
                    st.setPitchSemiTones((float) juce::jlimit(-12.0, 12.0,
                                                              cp.pitchSemitones));

                    // SoundTouch attend interleaved.
                    std::vector<float> interIn ((size_t) N * chs, 0.0f);
                    for (int i = 0; i < N; ++i)
                        for (int c = 0; c < chs; ++c)
                            interIn[(size_t) i * chs + c] = buf.getSample(c, i);
                    st.putSamples(interIn.data(), (unsigned int) N);
                    st.flush();

                    std::vector<float> interOut;
                    interOut.reserve((size_t)(N / std::max(0.25, cp.bpmAdjustRatio)) * chs);
                    const int kBlock = 4096;
                    std::vector<float> tmp((size_t) kBlock * chs);
                    while (true) {
                        const unsigned int got = st.receiveSamples(tmp.data(), kBlock);
                        if (got == 0) break;
                        interOut.insert(interOut.end(),
                                        tmp.begin(),
                                        tmp.begin() + (size_t) got * chs);
                    }
                    if (!interOut.empty()) {
                        const int outN = (int)(interOut.size() / chs);
                        juce::AudioBuffer<float> outBuf(chs, outN);
                        for (int i = 0; i < outN; ++i)
                            for (int c = 0; c < chs; ++c)
                                outBuf.setSample(c, i, interOut[(size_t) i * chs + c]);

                        const juce::String stem = juce::String::fromUTF8(track.title.c_str())
                            .replaceCharacters("/\\:*?\"<>|", "_________");
                        const juce::File outFile = tmpDir.getChildFile(
                            "stretch_" + stem + "_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
                        juce::FileOutputStream* osRaw = new juce::FileOutputStream(outFile);
                        if (osRaw->openedOk()) {
                            osRaw->setPosition(0); osRaw->truncate();
                            std::unique_ptr<juce::AudioFormatWriter> writer(
                                wavFmt.createWriterFor(osRaw, sr, (unsigned int) chs,
                                                       16, {}, 0));
                            if (writer) {
                                writer->writeFromAudioSampleBuffer(outBuf, 0, outN);
                                writer.reset(); // flush + close (writer takes ownership of stream)
                                track.filePath = outFile.getFullPathName().toStdString();
                                track.duration = (double) outN / std::max(1, sr);
                                inSec  = 0.0;
                                outSec = track.duration;
                            } else {
                                delete osRaw;
                            }
                        } else {
                            delete osRaw;
                        }
                    }
                }
            }
        }

        auto clip = std::make_unique<StudioClip>(track, *this);
        clip->setAudioRange(inSec, outSec);
        clip->setStartSec(cp.timelineStartSec);
        addAndMakeVisible(*clip);
        auto* raw = clip.get();
        wireClipCallback(*raw);
        m_clips.push_back(std::move(clip));
        enqueueClipPrep((int) m_clips.size() - 1);
    }

    auto mapKind = [](const juce::String& k) -> TransitionKind {
        const auto s = k.toLowerCase();
        if (s.contains("cut"))     return TransitionKind::Cut;
        if (s.contains("echo"))    return TransitionKind::EchoOut;
        if (s.contains("filter"))  return TransitionKind::FilterSweep;
        if (s.contains("brake") || s.contains("backspin")) return TransitionKind::Backspin;
        if (s.contains("equal"))   return TransitionKind::EqualPower;
        if (s.contains("fade"))    return TransitionKind::Fade;
        return TransitionKind::EqualPower;
    };
    for (const auto& tp : plan.transitions) {
        while ((int) m_transitions.size() <= tp.gapIndex)
            m_transitions.push_back({});
        if (tp.gapIndex >= 0 && tp.gapIndex < (int) m_transitions.size()) {
            m_transitions[(size_t) tp.gapIndex].kind = mapKind(tp.kind);
            m_transitions[(size_t) tp.gapIndex].bars = std::max(1, tp.durationBars);
        }
    }

    layoutClips();
    repaint();
    m_listeners.call([](Listener& l){ l.clipChanged(-1); });
}

void StudioTimeline::renameClip(int idx, const juce::String& newTitle) {
    if (idx < 0 || idx >= (int) m_clips.size()) return;
    if (juce::String(m_clips[idx]->getTrack().title) == newTitle) return;
    pushUndoSnapshot();
    m_clips[idx]->setTitle(newTitle);
    m_listeners.call([idx](Listener& l){ l.clipChanged(idx); });
    repaint();
}

void StudioTimeline::setClipVolume(int i, float gain01) {
    if (auto* c = getClip(i)) c->setVolume(gain01);
}

void StudioTimeline::setClipFadeIn(int i, double s) {
    if (auto* c = getClip(i)) c->setFadeIn(s);
}

void StudioTimeline::setClipFadeOut(int i, double s) {
    if (auto* c = getClip(i)) c->setFadeOut(s);
}

void StudioTimeline::setClipLooped(int i, double a, double b, bool on) {
    if (auto* c = getClip(i)) c->setLoop(a, b, on);
}

void StudioTimeline::setTransitionType(int gap, TransitionKind k) {
    if (gap < 0 || gap >= getNumTransitions()) {
        spdlog::warn("[Studio] setTransitionType ignored: gap {} out of range [0..{})",
                     gap, getNumTransitions());
        return;
    }
    while ((int) m_transitions.size() <= gap)
        m_transitions.push_back({});
    m_transitions[gap].kind = k;
    repaint();
}

void StudioTimeline::setTransitionDurationBars(int gap, int bars) {
    if (gap < 0 || gap >= getNumTransitions()) {
        spdlog::warn("[Studio] setTransitionDurationBars ignored: gap {} out of range [0..{})",
                     gap, getNumTransitions());
        return;
    }
    while ((int) m_transitions.size() <= gap)
        m_transitions.push_back({});
    m_transitions[gap].bars = std::max(1, bars);
    repaint();
}

int StudioTimeline::getNumTransitions() const noexcept {
    return std::max(0, (int) m_clips.size() - 1);
}

void StudioTimeline::setTransitionDurationBeats(int gap, int beats) {
    if (gap < 0 || gap >= getNumTransitions()) return;
    const int beatsClamped = std::max(0, beats);
    const int barsCalc = std::max(1, (beatsClamped + (m_tsigNum - 1)) / std::max(1, m_tsigNum));
    setTransitionDurationBars(gap, barsCalc);
}

void StudioTimeline::applyTransitionPresetAll(TransitionKind k, int beats) {
    pushUndoSnapshot();
    const int n = getNumTransitions();
    while ((int) m_transitions.size() < n)
        m_transitions.push_back({});
    for (int i = 0; i < n; ++i) {
        m_transitions[(size_t) i].kind = k;
        const int barsCalc = std::max(1,
            (std::max(0, beats) + (m_tsigNum - 1)) / std::max(1, m_tsigNum));
        m_transitions[(size_t) i].bars = barsCalc;
    }
    spdlog::info("[Studio] applyTransitionPresetAll: kind={} beats={} -> {} transitions",
                 (int) k, beats, n);
    repaint();
}

bool StudioTimeline::saveProject(const juce::File& f) const {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version",          4);
    root->setProperty("masterBpm",        m_masterBpm);
    root->setProperty("playhead",         m_playhead);
    root->setProperty("tsigNum",          m_tsigNum);
    root->setProperty("tsigDen",          m_tsigDen);
    root->setProperty("masterVol",        m_masterVol.load());
    root->setProperty("masterCompAmount", m_masterCompAmount.load());
    root->setProperty("zoom",             m_zoom);
    root->setProperty("snap",             (int) m_snap);
    root->setProperty("centerPlayhead",   m_centerPlayhead);
    root->setProperty("showBeatGrid",     m_showBeatGrid);
    root->setProperty("showStems",        m_showStems);
    root->setProperty("assist",           (int) m_assist);
    root->setProperty("rangeStartSec",    m_rangeStartSec);
    root->setProperty("rangeEndSec",      m_rangeEndSec);
    root->setProperty("rangeLoopEnabled", m_rangeLoopEnabled);

    juce::Array<juce::var> arr;
    for (auto& c : m_clips) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("id",        (juce::int64) c->getTrack().id);
        o->setProperty("filePath",  juce::String(c->getTrack().filePath));
        o->setProperty("title",     juce::String(c->getTrack().title));
        o->setProperty("startSec",  c->getStartSec());
        o->setProperty("audioIn",   c->getAudioInSec());
        o->setProperty("audioOut",  c->getAudioOutSec());
        o->setProperty("fadeIn",    c->getFade().inSec);
        o->setProperty("fadeOut",   c->getFade().outSec);
        o->setProperty("locked",    c->isLocked());
        o->setProperty("muted",     c->isMuted());
        o->setProperty("reversed",  c->isReversed());
        o->setProperty("volume",    c->getVolume());
        o->setProperty("groupId",   c->getGroupId());

        auto serializeAuto = [](const std::vector<StudioClip::AutoPoint>& pts) {
            juce::Array<juce::var> a;
            for (auto& p : pts) {
                juce::DynamicObject::Ptr po = new juce::DynamicObject();
                po->setProperty("t", p.timeRel);
                po->setProperty("v", p.value);
                a.add(po.get());
            }
            return juce::var(a);
        };
        o->setProperty("autoVol",   serializeAuto(c->getVolumeAutomation()));
        o->setProperty("autoEqHi",  serializeAuto(c->getEqHiAutomation()));
        o->setProperty("autoEqMid", serializeAuto(c->getEqMidAutomation()));
        o->setProperty("autoEqLo",  serializeAuto(c->getEqLoAutomation()));

        juce::Array<juce::var> fxArr;
        for (auto& fx : c->getEffects()) {
            juce::DynamicObject::Ptr fo = new juce::DynamicObject();
            fo->setProperty("type", juce::String(fx.type));
            fo->setProperty("enabled", fx.enabled);
            juce::DynamicObject::Ptr params = new juce::DynamicObject();
            for (auto& kv : fx.params) params->setProperty(juce::String(kv.first), (double) kv.second);
            fo->setProperty("params", params.get());
            fxArr.add(fo.get());
        }
        o->setProperty("effects", fxArr);

        juce::Array<juce::var> sm;
        for (int i = 0; i < 4; ++i) sm.add(juce::var(c->isStemMuted(i)));
        o->setProperty("stemMutes", sm);

        o->setProperty("pitchSt",    c->getPitchSemitones());
        o->setProperty("tempoRatio", c->getTempoRatio());

        const auto loop = c->getLoop();
        o->setProperty("loopInRel",  loop.inRel);
        o->setProperty("loopOutRel", loop.outRel);
        o->setProperty("loopOn",     loop.on);

        juce::Array<juce::var> cuesArr;
        for (auto& cu : c->getCues()) {
            juce::DynamicObject::Ptr co = new juce::DynamicObject();
            co->setProperty("pos",    cu.position);
            co->setProperty("len",    cu.length);
            co->setProperty("name",   juce::String(cu.name));
            co->setProperty("color",  juce::String(cu.color));
            co->setProperty("type",   (int) cu.type);
            co->setProperty("number", cu.number);
            cuesArr.add(co.get());
        }
        o->setProperty("cues", cuesArr);

        const auto& mk = c->getMarker();
        if (! mk.label.empty()) {
            juce::DynamicObject::Ptr mo = new juce::DynamicObject();
            mo->setProperty("label", juce::String(mk.label));
            mo->setProperty("color", (juce::int64) mk.colour.getARGB());
            mo->setProperty("bar",   mk.bar);
            o->setProperty("marker", mo.get());
        }

        arr.add(o.get());
    }
    root->setProperty("clips", arr);

    juce::Array<juce::var> tArr;
    for (const auto& g : m_transitions) {
        juce::DynamicObject::Ptr to = new juce::DynamicObject();
        to->setProperty("kind", (int) g.kind);
        to->setProperty("bars", g.bars);
        juce::Array<juce::var> cc;
        for (auto& p : g.customCurve) {
            juce::DynamicObject::Ptr po = new juce::DynamicObject();
            po->setProperty("t", p.timeRel);
            po->setProperty("v", p.value);
            cc.add(po.get());
        }
        to->setProperty("custom", cc);
        tArr.add(to.get());
    }
    root->setProperty("transitions", tArr);

    juce::Array<juce::var> lArr;
    for (const auto& li : m_laneInfos) {
        juce::DynamicObject::Ptr lo = new juce::DynamicObject();
        lo->setProperty("name",    li.name);
        lo->setProperty("color",   (juce::int64) li.color.getARGB());
        lo->setProperty("muted",   li.muted);
        lo->setProperty("solo",    li.solo);
        lo->setProperty("armed",   li.armed);
        lo->setProperty("locked",  li.locked);
        lo->setProperty("volDb",   li.volumeDb);
        lArr.add(lo.get());
    }
    root->setProperty("lanes", lArr);

    juce::Array<juce::var> tpArr;
    for (const auto& tp : m_tempoPoints) {
        juce::DynamicObject::Ptr po = new juce::DynamicObject();
        po->setProperty("sec", tp.sec);
        po->setProperty("bpm", tp.bpm);
        tpArr.add(po.get());
    }
    root->setProperty("tempoPoints", tpArr);

    juce::Array<juce::var> phArr;
    for (const auto& ph : m_phrases) {
        juce::DynamicObject::Ptr po = new juce::DynamicObject();
        po->setProperty("sec",   ph.sec);
        po->setProperty("dur",   ph.dur);
        po->setProperty("label", juce::String(ph.label));
        po->setProperty("color", (juce::int64) ph.colour.getARGB());
        phArr.add(po.get());
    }
    root->setProperty("phrases", phArr);

    // Stems sauves en sidecar <bmproj>.stems/ ; synchrone, acceptable au save explicite.
    {
        const juce::File sidecarRoot = f.getSiblingFile(
            f.getFileNameWithoutExtension() + ".stems");
        int saved = 0;
        for (const auto& cl : m_clips) {
            if (!cl || !cl->hasStems()) continue;
            auto* st = const_cast<StudioClip&>(*cl).getStems();
            if (!st) continue;
            const auto clipId = BeatMate::Core::StemSidecar::makeClipId(
                juce::String::fromUTF8(cl->getTrack().filePath.c_str()),
                cl->getAudioInSec(),
                cl->getAudioOutSec());
            BeatMate::Core::StemSidecar::StemSet set;
            set.vocals     = st->vocals;
            set.drums      = st->drums;
            set.bass       = st->bass;
            set.other      = st->other;
            set.sampleRate = st->sampleRate;
            set.ready      = st->ready;
            if (BeatMate::Core::StemSidecar::save(sidecarRoot, clipId, set)) ++saved;
        }
        if (saved > 0)
            spdlog::info("[Studio] saveProject: {} stems sets persisted to sidecar", saved);
    }

    return f.replaceWithText(juce::JSON::toString(root.get(), true));
}

bool StudioTimeline::loadProject(const juce::File& f) {
    if (!f.existsAsFile()) return false;
    auto v = juce::JSON::parse(f.loadFileAsString());
    if (!v.isObject()) return false;

    const int fileVersion = (int) v.getProperty("version", 3);
    if (fileVersion > 4) {
        spdlog::warn("[Studio] loadProject: file version {} > supported V4, "
                     "champs inconnus seront ignores", fileVersion);
    }
    m_masterBpm = (double) v.getProperty("masterBpm", 128.0);
    m_tsigNum   = (int)    v.getProperty("tsigNum", 4);
    m_tsigDen   = (int)    v.getProperty("tsigDen", 4);
    m_masterVol.store((float) (double) v.getProperty("masterVol", 1.0));
    m_masterCompAmount.store((float) (double) v.getProperty("masterCompAmount", 0.0));
    m_zoom = juce::jlimit(0.05, 200.0, (double) v.getProperty("zoom", 1.0));
    m_snap            = (SnapMode) (int) v.getProperty("snap", (int) SnapMode::Beat);
    m_centerPlayhead  = (bool) v.getProperty("centerPlayhead", false);
    m_showBeatGrid    = (bool) v.getProperty("showBeatGrid",   true);
    m_showStems       = (bool) v.getProperty("showStems",      false);
    m_assist          = (AssistMode) (int) v.getProperty("assist", (int) AssistMode::Manual);
    m_rangeStartSec   = (double) v.getProperty("rangeStartSec", -1.0);
    m_rangeEndSec     = (double) v.getProperty("rangeEndSec",   -1.0);
    m_rangeLoopEnabled = (bool) v.getProperty("rangeLoopEnabled", false);
    // Sync atomic shadows pour TimelineAudioSource.
    m_audioLoopStart.store(m_rangeStartSec);
    m_audioLoopEnd  .store(m_rangeEndSec);
    m_audioLoopOn   .store(m_rangeLoopEnabled);

    for (auto& c : m_clips) removeChildComponent(c.get());
    m_clips.clear();
    m_transitions.clear();
    m_laneInfos.clear();
    m_tempoPoints.clear();
    m_phrases.clear();

    auto deserializeAuto = [](const juce::var& arrV) {
        std::vector<StudioClip::AutoPoint> pts;
        if (auto* a = arrV.getArray()) {
            for (auto& it : *a) {
                StudioClip::AutoPoint p;
                p.timeRel = (double) it.getProperty("t", 0.0);
                p.value   = (float)  (double) it.getProperty("v", 0.0);
                pts.push_back(p);
            }
        }
        return pts;
    };

    auto arr = v.getProperty("clips", juce::var());
    if (auto* a = arr.getArray()) {
        for (auto& it : *a) {
            Models::Track t;
            t.id       = (int64_t) (juce::int64) it.getProperty("id", 0);
            t.filePath = it.getProperty("filePath", "").toString().toStdString();
            t.title    = it.getProperty("title", "").toString().toStdString();
            t.duration = (double) it.getProperty("audioOut", 1.0);
            auto clip = std::make_unique<StudioClip>(t, *this);
            clip->setAudioRange((double) it.getProperty("audioIn",  0.0),
                                (double) it.getProperty("audioOut", 1.0));
            clip->setStartSec  ((double) it.getProperty("startSec", 0.0));
            clip->setFadeIn    ((double) it.getProperty("fadeIn",   0.0));
            clip->setFadeOut   ((double) it.getProperty("fadeOut",  0.0));
            clip->setLocked    ((bool)   it.getProperty("locked",   false));
            clip->setMuted     ((bool)   it.getProperty("muted",    false));
            clip->setReversed  ((bool)   it.getProperty("reversed", false));
            clip->setVolume    ((float) (double) it.getProperty("volume", 1.0));
            clip->setGroupId   ((int)    it.getProperty("groupId", 0));

            for (auto& p : deserializeAuto(it.getProperty("autoVol",   juce::var())))
                clip->addVolumeAutoPoint(p.timeRel, p.value);
            for (auto& p : deserializeAuto(it.getProperty("autoEqHi",  juce::var())))
                clip->addEqHiAutoPoint  (p.timeRel, p.value);
            for (auto& p : deserializeAuto(it.getProperty("autoEqMid", juce::var())))
                clip->addEqMidAutoPoint (p.timeRel, p.value);
            for (auto& p : deserializeAuto(it.getProperty("autoEqLo",  juce::var())))
                clip->addEqLoAutoPoint  (p.timeRel, p.value);

            if (auto* fxA = it.getProperty("effects", juce::var()).getArray()) {
                for (auto& fxIt : *fxA) {
                    const auto type = fxIt.getProperty("type", "").toString().toStdString();
                    if (type.empty()) continue;
                    clip->addEffect(type);
                    const int fxIdx = (int) clip->getEffects().size() - 1;
                    auto pv = fxIt.getProperty("params", juce::var());
                    if (auto* pObj = pv.getDynamicObject()) {
                        for (auto& kv : pObj->getProperties()) {
                            clip->setEffectParam(fxIdx,
                                                 kv.name.toString().toStdString(),
                                                 (float) (double) kv.value);
                        }
                    }
                }
            }

            if (auto* sm = it.getProperty("stemMutes", juce::var()).getArray()) {
                for (int i = 0; i < std::min(4, sm->size()); ++i)
                    clip->setStemMute(i, (bool) (*sm)[i]);
            }
            clip->setPitchSemitones((double) it.getProperty("pitchSt",    0.0));
            clip->setTempoRatio    ((double) it.getProperty("tempoRatio", 1.0));

            const double loopIn  = (double) it.getProperty("loopInRel",  -1.0);
            const double loopOut = (double) it.getProperty("loopOutRel", -1.0);
            const bool   loopOn  = (bool)   it.getProperty("loopOn",     false);
            if (loopOn && loopIn >= 0.0 && loopOut > loopIn) {
                clip->setLoop(loopIn, loopOut, true);
            }
            if (auto* cuesA = it.getProperty("cues", juce::var()).getArray()) {
                std::vector<Models::CuePoint> cues;
                cues.reserve((size_t) cuesA->size());
                for (auto& cIt : *cuesA) {
                    Models::CuePoint cu;
                    cu.position = (double) cIt.getProperty("pos", 0.0);
                    cu.length   = (double) cIt.getProperty("len", 0.0);
                    cu.name     = cIt.getProperty("name", "").toString().toStdString();
                    cu.color    = cIt.getProperty("color", "").toString().toStdString();
                    cu.type     = (Models::CuePointType) (int) cIt.getProperty("type",
                                       (int) Models::CuePointType::HotCue);
                    cu.number   = (int) cIt.getProperty("number", 0);
                    cues.push_back(std::move(cu));
                }
                if (! cues.empty()) clip->setCues(std::move(cues));
            }
            auto markerVar = it.getProperty("marker", juce::var());
            if (markerVar.isObject()) {
                const auto label = markerVar.getProperty("label", "").toString().toStdString();
                const auto col   = juce::Colour((juce::uint32)(juce::int64)
                                       markerVar.getProperty("color", (juce::int64) 0));
                if (! label.empty()) clip->setMarker(label, col);
            }

            addAndMakeVisible(*clip);
            m_clips.push_back(std::move(clip));
        }
    }

    if (auto* tA = v.getProperty("transitions", juce::var()).getArray()) {
        for (auto& tIt : *tA) {
            GapTransition g;
            g.kind = (TransitionKind) (int) tIt.getProperty("kind", (int) TransitionKind::EqualPower);
            g.bars = (int) tIt.getProperty("bars", 8);
            if (auto* cc = tIt.getProperty("custom", juce::var()).getArray()) {
                for (auto& pIt : *cc) {
                    CrossfadeCurvePoint cp;
                    cp.timeRel = (double) pIt.getProperty("t", 0.0);
                    cp.value   = (double) pIt.getProperty("v", 0.0);
                    g.customCurve.push_back(cp);
                }
            }
            m_transitions.push_back(std::move(g));
        }
    }

    if (auto* lA = v.getProperty("lanes", juce::var()).getArray()) {
        for (auto& lIt : *lA) {
            LaneInfo li;
            li.name     = lIt.getProperty("name", "Track").toString();
            li.color    = juce::Colour((juce::uint32)(juce::int64) lIt.getProperty("color", (juce::int64) 0xFF3B82F6));
            li.muted    = (bool)  lIt.getProperty("muted",  false);
            li.solo     = (bool)  lIt.getProperty("solo",   false);
            li.armed    = (bool)  lIt.getProperty("armed",  false);
            li.locked   = (bool)  lIt.getProperty("locked", false);
            li.volumeDb = (float) (double) lIt.getProperty("volDb", 0.0);
            m_laneInfos.push_back(li);
        }
    }

    if (auto* tpA = v.getProperty("tempoPoints", juce::var()).getArray()) {
        for (auto& it : *tpA) {
            TempoPoint tp;
            tp.sec = (double) it.getProperty("sec", 0.0);
            tp.bpm = (double) it.getProperty("bpm", 128.0);
            m_tempoPoints.push_back(tp);
        }
    }
    if (auto* phA = v.getProperty("phrases", juce::var()).getArray()) {
        for (auto& it : *phA) {
            PhraseMarker ph;
            ph.sec   = (double) it.getProperty("sec", 0.0);
            ph.dur   = (double) it.getProperty("dur", 0.0);
            ph.label = it.getProperty("label", "").toString().toStdString();
            ph.colour = juce::Colour((juce::uint32)(juce::int64) it.getProperty("color", (juce::int64) 0xFF4A9EFF));
            m_phrases.push_back(ph);
        }
    }

    {
        const juce::File sidecarRoot = f.getSiblingFile(
            f.getFileNameWithoutExtension() + ".stems");
        int loaded = 0;
        for (auto& cl : m_clips) {
            if (!cl || cl->hasStems()) continue;  // deja charges (cas peu probable au load).
            const auto clipId = BeatMate::Core::StemSidecar::makeClipId(
                juce::String::fromUTF8(cl->getTrack().filePath.c_str()),
                cl->getAudioInSec(),
                cl->getAudioOutSec());
            if (!BeatMate::Core::StemSidecar::exists(sidecarRoot, clipId)) continue;
            BeatMate::Core::StemSidecar::StemSet set;
            if (!BeatMate::Core::StemSidecar::load(sidecarRoot, clipId, set)) continue;
            auto sd = std::make_unique<StudioClip::StemData>();
            sd->vocals     = std::move(set.vocals);
            sd->drums      = std::move(set.drums);
            sd->bass       = std::move(set.bass);
            sd->other      = std::move(set.other);
            sd->sampleRate = set.sampleRate;
            sd->ready      = set.ready;
            cl->attachStems(std::move(sd));
            ++loaded;
        }
        if (loaded > 0)
            spdlog::info("[Studio] loadProject: {} stems sets restored from sidecar", loaded);
    }

    m_playhead = (double) v.getProperty("playhead", 0.0);
    layoutClips();
    repaint();
    return true;
}

void StudioTimeline::exportMixToWav(const juce::File& outFile) {
    if (m_clips.empty()) return;

    // Passe par bouncePlaybackBuffer : l export doit refleter EXACTEMENT le preview.
    bouncePlaybackBuffer();

    if (m_playbackBuffer.getNumSamples() <= 0
        || m_playbackBuffer.getNumChannels() <= 0) {
        spdlog::warn("[Studio] exportMixToWav: empty buffer after bounce, abort");
        return;
    }

    juce::WavAudioFormat wav;
    outFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out(outFile.createOutputStream());
    if (!out) {
        spdlog::error("[Studio] exportMixToWav: cannot open {} for write",
                      outFile.getFullPathName().toStdString());
        return;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out.get(),
                             m_playbackSr,
                             (unsigned int) m_playbackBuffer.getNumChannels(),
                             16, {}, 0));
    if (!writer) {
        spdlog::error("[Studio] exportMixToWav: createWriterFor failed");
        return;
    }
    out.release();
    writer->writeFromAudioSampleBuffer(m_playbackBuffer, 0,
                                        m_playbackBuffer.getNumSamples());
    spdlog::info("[Studio] Export WAV OK -> {} ({} samples @ {:.0f} Hz, via bounce)",
                 outFile.getFullPathName().toStdString(),
                 m_playbackBuffer.getNumSamples(), m_playbackSr);
}

void StudioTimeline::exportToAbleton(const juce::File& alsFile) {
    juce::String xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<Ableton MajorVersion=\"5\" MinorVersion=\"11.0_11202\" SchemaChangeCount=\"3\" Creator=\"BeatMate\" Revision=\"\">\n";
    xml << "  <LiveSet>\n";
    xml << "    <NextPointeeId Value=\"" << (int) (m_clips.size() * 4 + 100) << "\"/>\n";
    xml << "    <Tracks>\n";
    int trackId = 1;
    for (auto& c : m_clips) {
        xml << "      <AudioTrack Id=\"" << trackId++ << "\">\n";
        xml << "        <Name><EffectiveName Value=\"" << juce::String(c->getTrack().title) << "\"/></Name>\n";
        xml << "        <DeviceChain>\n";
        xml << "          <MainSequencer><Sample><ArrangerAutomation><Events>\n";
        xml << "            <AudioClip Time=\"" << c->getStartSec() << "\">\n";
        xml << "              <Name Value=\"" << juce::String(c->getTrack().title) << "\"/>\n";
        xml << "              <CurrentStart Value=\"" << c->getAudioInSec() << "\"/>\n";
        xml << "              <CurrentEnd Value=\"" << c->getAudioOutSec() << "\"/>\n";
        xml << "              <Loop><LoopStart Value=\"0\"/><LoopEnd Value=\""
            << c->getLengthSec() << "\"/><LoopOn Value=\"false\"/></Loop>\n";
        xml << "              <SampleRef><FileRef><RelativePath Value=\""
            << juce::String(c->getTrack().filePath) << "\"/></FileRef></SampleRef>\n";
        xml << "              <Fade><FadeInLength Value=\"" << c->getFade().inSec << "\"/>";
        xml << "<FadeOutLength Value=\"" << c->getFade().outSec << "\"/></Fade>\n";
        xml << "            </AudioClip>\n";
        xml << "          </Events></ArrangerAutomation></Sample></MainSequencer>\n";
        xml << "        </DeviceChain>\n";
        xml << "      </AudioTrack>\n";
    }
    xml << "    </Tracks>\n";
    xml << "    <MasterTrack><DeviceChain><Mixer><Tempo>";
    xml << "<Manual Value=\"" << m_masterBpm << "\"/></Tempo></Mixer></DeviceChain></MasterTrack>\n";
    xml << "  </LiveSet>\n";
    xml << "</Ableton>\n";

    alsFile.deleteFile();
    juce::FileOutputStream raw{alsFile};
    if (!raw.openedOk()) return;
    juce::GZIPCompressorOutputStream gz(&raw, 6, false,
        juce::GZIPCompressorOutputStream::windowBitsGZIP);
    const auto bytes = xml.toUTF8();
    gz.write(bytes.getAddress(), (size_t) bytes.sizeInBytes() - 1);
    gz.flush();
    spdlog::info("[Studio] Export Ableton .als OK -> {}", alsFile.getFullPathName().toStdString());
}

class StudioTimeline::TimelineAudioSource : public juce::AudioSource {
public:
    TimelineAudioSource(juce::AudioBuffer<float>& buf,
                         std::atomic<int64_t>& pos,
                         double& sr,
                         std::atomic<bool>& playing,
                         std::atomic<float>& masterVol,
                         std::atomic<float>& compAmount,
                         std::atomic<float>& peakL,
                         std::atomic<float>& peakR,
                         std::atomic<bool>&   loopOn,
                         std::atomic<double>& loopStart,
                         std::atomic<double>& loopEnd)
        : buf_(buf), pos_(pos), sr_(sr),
          playing_(playing), masterVol_(masterVol), compAmount_(compAmount),
          peakL_(peakL), peakR_(peakR),
          loopOn_(loopOn), loopStart_(loopStart), loopEnd_(loopEnd) {
        // Lissage master (~20 ms) : supprime le zipper noise.
        smoothedVol_.reset(44100.0, 0.020);
        smoothedVol_.setCurrentAndTargetValue(masterVol_.load());
        compressor_.setAttack(10.0f);
        compressor_.setRelease(100.0f);
        compressor_.setKnee(6.0f);
        applyCompressorAmount(compAmount_.load());
    }

    void prepareToPlay(int blockSize, double sampleRate) override {
        sr_ = sampleRate;
        smoothedVol_.reset(sampleRate, 0.020);
        smoothedVol_.setCurrentAndTargetValue(masterVol_.load());
        compressor_.setSampleRate(sampleRate);
        compressor_.reset();
        scratch_.assign((size_t) blockSize * 2 /*max channels*/, 0.0f);
    }
    void releaseResources() override {
        compressor_.reset();
        scratch_.clear();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        info.clearActiveBufferRegion();
        if (!playing_.load() || buf_.getNumSamples() == 0) return;

        const int64_t srcLen = buf_.getNumSamples();
        const int srcCh = buf_.getNumChannels();
        const int dstCh = info.buffer->getNumChannels();
        const int N = info.numSamples;

        // Met à jour la cible du master smoothing (atomic load — pas de lock).
        smoothedVol_.setTargetValue(masterVol_.load());

        const float amount = compAmount_.load();
        if (std::abs(amount - lastCompAmount_) > 1e-3f) {
            applyCompressorAmount(amount);
            lastCompAmount_ = amount;
        }

        // Wrap audio-side du loop range AVANT lecture.
        const bool   loopActive = loopOn_.load();
        const double loopS_sec  = loopStart_.load();
        const double loopE_sec  = loopEnd_.load();
        const bool   validLoop  = loopActive && loopS_sec >= 0.0 && loopE_sec > loopS_sec;
        const int64_t loopStartFrame = validLoop ? (int64_t)(loopS_sec * sr_) : 0;
        const int64_t loopEndFrame   = validLoop ? (int64_t)(loopE_sec * sr_) : 0;

        int64_t p = pos_.load();
        if (validLoop && p >= loopEndFrame) p = loopStartFrame;
        int writtenSamples = 0;
        for (int i = 0; i < N; ++i) {
            if (validLoop && p >= loopEndFrame) p = loopStartFrame;
            if (p >= srcLen) { playing_.store(false); break; }
            for (int c = 0; c < dstCh; ++c) {
                const int sc = juce::jmin(c, srcCh - 1);
                info.buffer->setSample(c, info.startSample + i,
                                       buf_.getSample(sc, (int) p));
            }
            ++p;
            ++writtenSamples;
        }
        pos_.store(p);

        if (writtenSamples <= 0) return;

        if (compAmount_.load() > 0.05f && dstCh > 0) {
            const size_t need = (size_t) writtenSamples * (size_t) dstCh;
            if (scratch_.size() < need) scratch_.resize(need);
            for (int i = 0; i < writtenSamples; ++i) {
                for (int c = 0; c < dstCh; ++c) {
                    scratch_[(size_t) i * (size_t) dstCh + (size_t) c] =
                        info.buffer->getSample(c, info.startSample + i);
                }
            }
            compressor_.process(scratch_.data(), writtenSamples, dstCh);
            for (int i = 0; i < writtenSamples; ++i) {
                for (int c = 0; c < dstCh; ++c) {
                    info.buffer->setSample(c, info.startSample + i,
                        scratch_[(size_t) i * (size_t) dstCh + (size_t) c]);
                }
            }
        }

        for (int i = 0; i < writtenSamples; ++i) {
            const float gv = smoothedVol_.getNextValue();
            for (int c = 0; c < dstCh; ++c) {
                info.buffer->applyGain(c, info.startSample + i, 1, gv);
            }
        }

        // Peak metering post-master avec decay par bloc (lu par PeakMeterDual a 30 Hz).
        if (dstCh > 0) {
            float blockMaxL = 0.0f;
            float blockMaxR = 0.0f;
            const int chL = 0;
            const int chR = juce::jmin(1, dstCh - 1);
            const float* dL = info.buffer->getReadPointer(chL, info.startSample);
            const float* dR = info.buffer->getReadPointer(chR, info.startSample);
            for (int i = 0; i < writtenSamples; ++i) {
                blockMaxL = std::max(blockMaxL, std::abs(dL[i]));
                blockMaxR = std::max(blockMaxR, std::abs(dR[i]));
            }
            constexpr float kBlockDecay = 0.85f;
            float prevL = peakL_.load(std::memory_order_relaxed);
            float prevR = peakR_.load(std::memory_order_relaxed);
            const float newL = std::max(blockMaxL, prevL * kBlockDecay);
            const float newR = std::max(blockMaxR, prevR * kBlockDecay);
            peakL_.store(newL, std::memory_order_relaxed);
            peakR_.store(newR, std::memory_order_relaxed);
        }
    }

    BeatMate::Core::CompressorProcessor& getCompressor() noexcept { return compressor_; }

private:
    void applyCompressorAmount(float amount) {
        // Mapping calque sur AudioEngine::setMasterCompressorAmount.
        const float a = juce::jlimit(0.0f, 10.0f, amount) / 10.0f;
        compressor_.setThreshold(-30.0f * a);
        compressor_.setRatio(1.0f + 7.0f * a);
        compressor_.setMakeupGain(0.5f * a * 4.0f);
    }

    juce::AudioBuffer<float>& buf_;
    std::atomic<int64_t>&     pos_;
    double&                   sr_;
    std::atomic<bool>&        playing_;
    std::atomic<float>&       masterVol_;
    std::atomic<float>&       compAmount_;
    std::atomic<float>&       peakL_;
    std::atomic<float>&       peakR_;
    std::atomic<bool>&        loopOn_;
    std::atomic<double>&      loopStart_;
    std::atomic<double>&      loopEnd_;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVol_;
    BeatMate::Core::CompressorProcessor compressor_;
    float                     lastCompAmount_ { -1.0f };
    std::vector<float>        scratch_;
};

// Lecture live par bloc depuis m_clipBufs ; edits audibles au bloc suivant, lock-free cote audio.
class StudioTimeline::LiveTimelineSource : public juce::AudioSource {
public:
    LiveTimelineSource(StudioTimeline& owner,
                       std::atomic<int64_t>& pos,
                       double& sr,
                       std::atomic<bool>& playing,
                       std::atomic<float>& masterVol,
                       std::atomic<float>& compAmount,
                       std::atomic<float>& peakL,
                       std::atomic<float>& peakR)
        : owner_(owner), pos_(pos), sr_(sr),
          playing_(playing), masterVol_(masterVol), compAmount_(compAmount),
          peakL_(peakL), peakR_(peakR)
    {
        smoothedVol_.reset(44100.0, 0.020);
        smoothedVol_.setCurrentAndTargetValue(masterVol_.load());
        compressor_.setAttack(10.0f);
        compressor_.setRelease(100.0f);
        compressor_.setKnee(6.0f);
        applyCompressorAmount(compAmount_.load());
    }

    void prepareToPlay(int blockSize, double sampleRate) override {
        sr_ = sampleRate;
        smoothedVol_.reset(sampleRate, 0.020);
        smoothedVol_.setCurrentAndTargetValue(masterVol_.load());
        compressor_.setSampleRate(sampleRate);
        compressor_.reset();
        scratch_.assign((size_t) blockSize * 2, 0.0f);
        snapshot_.reserve(64);
    }
    void releaseResources() override {
        compressor_.reset();
        scratch_.clear();
        snapshot_.clear();
        snapshot_.shrink_to_fit();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        info.clearActiveBufferRegion();
        if (!playing_.load()) return;

        const int N      = info.numSamples;
        const int dstCh  = info.buffer->getNumChannels();
        if (N <= 0 || dstCh <= 0) return;

        const double sr        = sr_ > 0.0 ? sr_ : 44100.0;

        // Snapshot du modele sous mutex bref, puis traitement lock-free.
        snapshot_.clear();
        bool anyLaneSolo = false;
        std::vector<float> laneGains;   // dB->lin par lane
        std::vector<bool>  laneMutes;
        std::vector<bool>  laneSolos;
        bool   rangeLoopActive = false;
        double rangeStartSec   = -1.0;
        double rangeEndSec     = -1.0;
        {
            std::lock_guard<std::mutex> lock(owner_.m_renderModelMutex);
            rangeLoopActive = owner_.m_rangeLoopEnabled
                            && owner_.m_rangeStartSec >= 0.0
                            && owner_.m_rangeEndSec > owner_.m_rangeStartSec;
            rangeStartSec = owner_.m_rangeStartSec;
            rangeEndSec   = owner_.m_rangeEndSec;
            const size_t nClips = owner_.m_clips.size();
            const size_t nBufs  = owner_.m_clipBufs.size();
            const size_t nLanes = owner_.m_laneInfos.size();
            laneGains.resize(nLanes, 1.0f);
            laneMutes.resize(nLanes, false);
            laneSolos.resize(nLanes, false);
            for (size_t l = 0; l < nLanes; ++l) {
                laneGains[l] = juce::Decibels::decibelsToGain(owner_.m_laneInfos[l].volumeDb);
                laneMutes[l] = owner_.m_laneInfos[l].muted;
                laneSolos[l] = owner_.m_laneInfos[l].solo;
                if (laneSolos[l]) anyLaneSolo = true;
            }
            for (size_t i = 0; i < nClips && i < nBufs; ++i) {
                if (!owner_.m_clipBufs[i]) continue;
                ClipSnap s;
                s.buf       = owner_.m_clipBufs[i];           // shared_ptr copy (refcount)
                s.startSec  = owner_.m_clips[i]->getStartSec();
                s.muted     = owner_.m_clips[i]->isMuted();
                s.laneIdx   = owner_.getLaneOfClip((int) i);
                // Copie du vecteur automation : alloc potentielle, acceptable car snapshot_ est reutilise.
                s.autoVol   = owner_.m_clips[i]->getVolumeAutomation();
                snapshot_.push_back(std::move(s));
            }
        }

        // Wrap audio-side AVANT lecture du bloc.
        if (rangeLoopActive) {
            const int64_t loopStartFrame = (int64_t) (rangeStartSec * sr);
            const int64_t loopEndFrame   = (int64_t) (rangeEndSec   * sr);
            int64_t curPos = pos_.load();
            if (curPos >= loopEndFrame) {
                curPos = loopStartFrame;
                pos_.store(curPos);
            }
        }
        const int64_t blockS = pos_.load();
        const int64_t blockE = blockS + N;

        // Pre-calcul des envelopes de transition (rendu equal-power).
        struct ClipEnv {
            bool    hasOutgoing { false };
            int64_t outOvS { 0 }, outOvE { 0 };
            bool    isCutOut { false };
            bool    useCustomOut { false };
            std::vector<CrossfadeCurvePoint> customOut;
            bool    hasIncoming { false };
            int64_t inOvS { 0 }, inOvE { 0 };
            bool    isCutIn { false };
            bool    useCustomIn { false };
            std::vector<CrossfadeCurvePoint> customIn;
        };
        std::vector<ClipEnv> envs(snapshot_.size());
        std::vector<TransitionKind> transKinds(snapshot_.size(), TransitionKind::EqualPower);
        std::vector<std::vector<CrossfadeCurvePoint>> transCustom(snapshot_.size());
        {
            std::lock_guard<std::mutex> lock(owner_.m_renderModelMutex);
            for (size_t k = 0; k < owner_.m_transitions.size() && k < snapshot_.size(); ++k) {
                transKinds[k]  = owner_.m_transitions[k].kind;
                transCustom[k] = owner_.m_transitions[k].customCurve;   // copy
            }
        }
        for (size_t i = 0; i + 1 < snapshot_.size(); ++i) {
            const auto& a = snapshot_[i];
            const auto& b = snapshot_[i + 1];
            if (!a.buf || !b.buf) continue;
            const int64_t aS = (int64_t) (a.startSec * sr);
            const int64_t aE = aS + a.buf->buf.getNumSamples();
            const int64_t bS = (int64_t) (b.startSec * sr);
            const int64_t bE = bS + b.buf->buf.getNumSamples();
            const int64_t ovS = std::max(aS, bS);
            const int64_t ovE = std::min(aE, bE);
            if (ovE - ovS >= (int64_t) (sr * 0.005)) {
                const bool isCut    = (transKinds[i] == TransitionKind::Cut);
                const bool isCustom = (transKinds[i] == TransitionKind::Custom)
                                       && (transCustom[i].size() >= 2);
                envs[i].hasOutgoing  = true;
                envs[i].outOvS       = ovS;
                envs[i].outOvE       = ovE;
                envs[i].isCutOut     = isCut;
                envs[i].useCustomOut = isCustom;
                if (isCustom) envs[i].customOut = transCustom[i];
                envs[i + 1].hasIncoming = true;
                envs[i + 1].inOvS       = ovS;
                envs[i + 1].inOvE       = ovE;
                envs[i + 1].isCutIn     = isCut;
                envs[i + 1].useCustomIn = isCustom;
                if (isCustom) envs[i + 1].customIn = transCustom[i];
            }
        }
        constexpr float kHalfPi = 1.5707963f;

        for (size_t cIdx = 0; cIdx < snapshot_.size(); ++cIdx) {
            const auto& s = snapshot_[cIdx];
            if (s.muted) continue;
            if (anyLaneSolo) {
                if (s.laneIdx < 0 || (size_t) s.laneIdx >= laneSolos.size()) continue;
                if (!laneSolos[(size_t) s.laneIdx]) continue;
            }
            if (s.laneIdx >= 0 && (size_t) s.laneIdx < laneMutes.size()
                && laneMutes[(size_t) s.laneIdx]) continue;

            const float laneGain = (s.laneIdx >= 0 && (size_t) s.laneIdx < laneGains.size())
                                    ? laneGains[(size_t) s.laneIdx] : 1.0f;

            const auto&   srcBuf      = s.buf->buf;
            const int     srcCh       = srcBuf.getNumChannels();
            const int64_t clipStart   = (int64_t) (s.startSec * sr);
            const int64_t clipEnd     = clipStart + srcBuf.getNumSamples();

            const int64_t from = std::max(blockS, clipStart);
            const int64_t to   = std::min(blockE, clipEnd);
            if (to <= from) continue;
            const int     n      = (int) (to - from);
            const int     dstOff = info.startSample + (int) (from - blockS);
            const int     srcOff = (int) (from - clipStart);
            if (dstOff < 0 || dstOff + n > info.startSample + N) continue;
            if (srcOff < 0 || srcOff + n > srcBuf.getNumSamples()) continue;

            const ClipEnv& env = envs[cIdx];
            const bool hasAutomation = !s.autoVol.empty();
            const bool needPerSample = env.hasOutgoing || env.hasIncoming || hasAutomation;
            const float invSrcLen = (srcBuf.getNumSamples() > 0)
                                     ? 1.0f / (float) srcBuf.getNumSamples()
                                     : 0.0f;
            if (!needPerSample) {
                for (int c = 0; c < dstCh; ++c) {
                    const int sc = juce::jmin(c, srcCh - 1);
                    if (sc < 0) continue;
                    info.buffer->addFrom(c, dstOff, srcBuf, sc, srcOff, n, laneGain);
                }
            } else {
                for (int j = 0; j < n; ++j) {
                    const int64_t f = from + j;
                    float g = laneGain;
                    if (env.hasOutgoing && f >= env.outOvS && f < env.outOvE) {
                        const float t = (float) (f - env.outOvS)
                                        / std::max<float>(1.0f, (float) (env.outOvE - env.outOvS));
                        if (env.isCutOut)             g *= (t < 0.5f ? 1.0f : 0.0f);
                        else if (env.useCustomOut)    g *= interpolateCustomCurve(env.customOut, t);
                        else                          g *= std::cos(t * kHalfPi);
                    }
                    if (env.hasIncoming && f >= env.inOvS && f < env.inOvE) {
                        const float t = (float) (f - env.inOvS)
                                        / std::max<float>(1.0f, (float) (env.inOvE - env.inOvS));
                        if (env.isCutIn)              g *= (t < 0.5f ? 0.0f : 1.0f);
                        else if (env.useCustomIn)     g *= 1.0f - interpolateCustomCurve(env.customIn, t);
                        else                          g *= std::sin(t * kHalfPi);
                    }
                    if (hasAutomation) {
                        const float timeRel = (float) (srcOff + j) * invSrcLen;
                        g *= interpolateAutoLinear(s.autoVol, timeRel);
                    }
                    if (g <= 1e-6f) continue;
                    for (int c = 0; c < dstCh; ++c) {
                        const int sc = juce::jmin(c, srcCh - 1);
                        if (sc < 0) continue;
                        info.buffer->addSample(c, dstOff + j,
                                                srcBuf.getSample(sc, srcOff + j) * g);
                    }
                }
            }
        }

        const int writtenSamples = N;
        pos_.store(blockE);

        smoothedVol_.setTargetValue(masterVol_.load());
        const float amount = compAmount_.load();
        if (std::abs(amount - lastCompAmount_) > 1e-3f) {
            applyCompressorAmount(amount);
            lastCompAmount_ = amount;
        }
        if (amount > 0.05f && dstCh > 0) {
            const size_t need = (size_t) writtenSamples * (size_t) dstCh;
            if (scratch_.size() < need) scratch_.resize(need);
            for (int i = 0; i < writtenSamples; ++i) {
                for (int c = 0; c < dstCh; ++c) {
                    scratch_[(size_t) i * (size_t) dstCh + (size_t) c] =
                        info.buffer->getSample(c, info.startSample + i);
                }
            }
            compressor_.process(scratch_.data(), writtenSamples, dstCh);
            for (int i = 0; i < writtenSamples; ++i) {
                for (int c = 0; c < dstCh; ++c) {
                    info.buffer->setSample(c, info.startSample + i,
                        scratch_[(size_t) i * (size_t) dstCh + (size_t) c]);
                }
            }
        }
        for (int i = 0; i < writtenSamples; ++i) {
            const float gv = smoothedVol_.getNextValue();
            for (int c = 0; c < dstCh; ++c)
                info.buffer->applyGain(c, info.startSample + i, 1, gv);
        }

        if (dstCh > 0) {
            float blockMaxL = 0.0f, blockMaxR = 0.0f;
            const int chL = 0;
            const int chR = juce::jmin(1, dstCh - 1);
            const float* dL = info.buffer->getReadPointer(chL, info.startSample);
            const float* dR = info.buffer->getReadPointer(chR, info.startSample);
            for (int i = 0; i < writtenSamples; ++i) {
                blockMaxL = std::max(blockMaxL, std::abs(dL[i]));
                blockMaxR = std::max(blockMaxR, std::abs(dR[i]));
            }
            constexpr float kBlockDecay = 0.85f;
            const float prevL = peakL_.load(std::memory_order_relaxed);
            const float prevR = peakR_.load(std::memory_order_relaxed);
            peakL_.store(std::max(blockMaxL, prevL * kBlockDecay), std::memory_order_relaxed);
            peakR_.store(std::max(blockMaxR, prevR * kBlockDecay), std::memory_order_relaxed);
        }
    }

    BeatMate::Core::CompressorProcessor& getCompressor() noexcept { return compressor_; }

private:
    void applyCompressorAmount(float amount) {
        const float a = juce::jlimit(0.0f, 10.0f, amount) / 10.0f;
        compressor_.setThreshold(-30.0f * a);
        compressor_.setRatio(1.0f + 7.0f * a);
        compressor_.setMakeupGain(0.5f * a * 4.0f);
    }

    struct ClipSnap {
        std::shared_ptr<StudioTimeline::PreparedClipBuffer> buf;
        double startSec  { 0.0 };
        bool   muted     { false };
        int    laneIdx   { -1 };
        std::vector<StudioClip::AutoPoint> autoVol;   // automation volume live
    };

    static float interpolateAutoLinear(const std::vector<StudioClip::AutoPoint>& pts,
                                        float timeRel) noexcept
    {
        if (pts.empty()) return 1.0f;
        if (timeRel <= pts.front().timeRel) return pts.front().value;
        if (timeRel >= pts.back().timeRel)  return pts.back().value;
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            const auto& p0 = pts[k];
            const auto& p1 = pts[k + 1];
            if (timeRel >= (float) p0.timeRel && timeRel <= (float) p1.timeRel) {
                const float span = std::max(1e-6f, (float) (p1.timeRel - p0.timeRel));
                const float t    = (timeRel - (float) p0.timeRel) / span;
                return p0.value + t * (p1.value - p0.value);
            }
        }
        return 1.0f;
    }

    static float interpolateCustomCurve(const std::vector<CrossfadeCurvePoint>& curve,
                                         float t) noexcept
    {
        if (curve.empty()) return 0.5f;
        if (t <= (float) curve.front().timeRel) return (float) curve.front().value;
        if (t >= (float) curve.back().timeRel)  return (float) curve.back().value;
        for (size_t k = 0; k + 1 < curve.size(); ++k) {
            const auto& p0 = curve[k];
            const auto& p1 = curve[k + 1];
            if (t >= (float) p0.timeRel && t <= (float) p1.timeRel) {
                const float span = std::max(1e-6f, (float) (p1.timeRel - p0.timeRel));
                const float u = (t - (float) p0.timeRel) / span;
                return (float) p0.value + u * (float) (p1.value - p0.value);
            }
        }
        return 0.5f;
    }

    StudioTimeline&            owner_;
    std::atomic<int64_t>&      pos_;
    double&                    sr_;
    std::atomic<bool>&         playing_;
    std::atomic<float>&        masterVol_;
    std::atomic<float>&        compAmount_;
    std::atomic<float>&        peakL_;
    std::atomic<float>&        peakR_;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVol_;
    BeatMate::Core::CompressorProcessor compressor_;
    float                      lastCompAmount_ { -1.0f };
    std::vector<float>         scratch_;
    std::vector<ClipSnap>      snapshot_;        // reuse pour eviter alloc/block
};

void StudioTimeline::prepareAudioOutput() {
    if (m_audioInited) return;
    m_audioInited = true;
    m_deviceManager.initialiseWithDefaultDevices(0, 2);
    if (m_useLiveSource.load()) {
        m_liveSource = std::make_unique<LiveTimelineSource>(
            *this, m_playbackFrame, m_playbackSr,
            m_playing, m_masterVol, m_masterCompAmount,
            m_masterPeakL, m_masterPeakR);
        m_sourcePlayer.setSource(m_liveSource.get());
    } else {
        m_audioSource = std::make_unique<TimelineAudioSource>(
            m_playbackBuffer, m_playbackFrame, m_playbackSr,
            m_playing, m_masterVol, m_masterCompAmount,
            m_masterPeakL, m_masterPeakR,
            m_audioLoopOn, m_audioLoopStart, m_audioLoopEnd);
        m_sourcePlayer.setSource(m_audioSource.get());
    }
    m_deviceManager.addAudioCallback(&m_sourcePlayer);
}

void StudioTimeline::setLivePlaybackEnabled(bool on) {
    if (m_useLiveSource.load() == on) return;
    const bool wasPlaying = m_playing.load();
    if (wasPlaying) stop();
    m_useLiveSource.store(on);
    // Force re-init de l output pour re-attacher le bon AudioSource.
    if (m_audioInited) {
        m_sourcePlayer.setSource(nullptr);
        m_deviceManager.removeAudioCallback(&m_sourcePlayer);
        m_audioInited = false;
        m_audioSource.reset();
        m_liveSource.reset();
    }
    spdlog::info("[Studio] LivePlayback {}", on ? "ENABLED" : "DISABLED");
    if (wasPlaying) play();
}

// Prep sync d un clip : peut bloquer (I/O, FX, resample), JAMAIS depuis l audio thread.
std::shared_ptr<StudioTimeline::PreparedClipBuffer>
StudioTimeline::prepareClipBufferSync(int idx) const
{
    if (idx < 0 || idx >= (int) m_clips.size()) return nullptr;
    const auto& cl = *m_clips[(size_t) idx];
    const double targetSr = m_playbackSr;

    juce::AudioBuffer<float> src;
    double srcSampleRate = targetSr;

    if (cl.hasStems()) {
        if (auto* st = const_cast<StudioClip&>(cl).getStems()) {
            srcSampleRate = st->sampleRate;
            const int n = std::max({ st->drums.getNumSamples(),
                                      st->bass.getNumSamples(),
                                      st->other.getNumSamples(),
                                      st->vocals.getNumSamples() });
            const int chs = std::max({ st->drums.getNumChannels(),
                                        st->bass.getNumChannels(),
                                        st->other.getNumChannels(),
                                        st->vocals.getNumChannels() });
            if (n > 0 && chs > 0) {
                src.setSize(chs, n, false, true, false);
                src.clear();
                auto addIfNotMuted = [&](const juce::AudioBuffer<float>& s, int sIdx) {
                    if (cl.isStemMuted(sIdx) || s.getNumSamples() == 0) return;
                    const int N = std::min(n, s.getNumSamples());
                    for (int c = 0; c < chs; ++c) {
                        const int sc = std::min(c, s.getNumChannels() - 1);
                        if (sc < 0) continue;
                        src.addFrom(c, 0, s, sc, 0, N);
                    }
                };
                addIfNotMuted(st->drums,  0);
                addIfNotMuted(st->bass,   1);
                addIfNotMuted(st->other,  2);
                addIfNotMuted(st->vocals, 3);
            }
        }
    } else {
        juce::File f { juce::String::fromUTF8(cl.getTrack().filePath.c_str()) };
        if (!f.existsAsFile()) return nullptr;
        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
        if (!reader) return nullptr;
        const int64_t srcStart = (int64_t) (cl.getAudioInSec()  * reader->sampleRate);
        const int64_t srcEnd   = (int64_t) (cl.getAudioOutSec() * reader->sampleRate);
        const int64_t srcLen   = std::max<int64_t>(0, srcEnd - srcStart);
        if (srcLen <= 0) return nullptr;
        src.setSize((int) reader->numChannels, (int) srcLen, false, true, false);
        reader->read(&src, 0, (int) srcLen, srcStart, true, true);
        srcSampleRate = reader->sampleRate;
    }
    if (src.getNumSamples() <= 0) return nullptr;

    if (cl.isReversed()) {
        for (int c = 0; c < src.getNumChannels(); ++c)
            std::reverse(src.getWritePointer(c),
                         src.getWritePointer(c) + src.getNumSamples());
    }

    const int fadeInN  = (int) (cl.getFade().inSec  * srcSampleRate);
    const int fadeOutN = (int) (cl.getFade().outSec * srcSampleRate);
    if (fadeInN > 0)
        for (int c = 0; c < src.getNumChannels(); ++c)
            src.applyGainRamp(c, 0, std::min(fadeInN, src.getNumSamples()), 0.0f, 1.0f);
    if (fadeOutN > 0) {
        const int s = std::max(0, src.getNumSamples() - fadeOutN);
        const int n = src.getNumSamples() - s;
        for (int c = 0; c < src.getNumChannels(); ++c)
            src.applyGainRamp(c, s, n, 1.0f, 0.0f);
    }

    if (!cl.getEffects().empty())
        applyClipEffectsToBuffer(src, srcSampleRate, cl.getEffects());

    // Volume per-clip uniquement (PAS lane gain, applique live).
    const float vol = cl.getVolume();
    if (std::abs(vol - 1.0f) > 1e-6f) src.applyGain(vol);

    const double pitchSt   = cl.getPitchSemitones();
    const double tempoRat  = cl.getTempoRatio();
    if ((std::abs(tempoRat - 1.0) > 1e-4 || std::abs(pitchSt) > 1e-4)
        && src.getNumSamples() > 0) {
        const int chs = src.getNumChannels();
        const int nIn = src.getNumSamples();
        soundtouch::SoundTouch st;
        st.setSampleRate((unsigned int) srcSampleRate);
        st.setChannels((unsigned int) chs);
        st.setSetting(SETTING_USE_AA_FILTER, 1);
        st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
        st.setSetting(SETTING_USE_QUICKSEEK, 0);
        st.setSetting(SETTING_SEQUENCE_MS,    60);
        st.setSetting(SETTING_SEEKWINDOW_MS,  25);
        st.setSetting(SETTING_OVERLAP_MS,     12);
        st.setTempo(juce::jlimit(0.25, 4.0, tempoRat));
        st.setPitchSemiTones((float) juce::jlimit(-24.0, 24.0, pitchSt));

        std::vector<float> inter((size_t) nIn * chs, 0.0f);
        for (int i = 0; i < nIn; ++i)
            for (int c = 0; c < chs; ++c)
                inter[(size_t) i * chs + c] = src.getSample(c, i);
        st.putSamples(inter.data(), (unsigned int) nIn);
        st.flush();
        std::vector<float> out;
        out.reserve((size_t)((double) nIn / std::max(0.25, tempoRat)) * chs);
        const int kBlock = 4096;
        std::vector<float> tmp((size_t) kBlock * chs);
        while (true) {
            const unsigned int got = st.receiveSamples(tmp.data(), kBlock);
            if (got == 0) break;
            out.insert(out.end(), tmp.begin(), tmp.begin() + (size_t) got * chs);
        }
        if (!out.empty()) {
            const int outN = (int) (out.size() / chs);
            juce::AudioBuffer<float> stretched(chs, outN);
            for (int i = 0; i < outN; ++i)
                for (int c = 0; c < chs; ++c)
                    stretched.setSample(c, i, out[(size_t) i * chs + c]);
            src = std::move(stretched);
        }
    }

    if (std::abs(srcSampleRate - targetSr) > 1.0 && src.getNumSamples() > 0) {
        const double ratio = srcSampleRate / targetSr;
        const int newN = (int) std::ceil(src.getNumSamples() / ratio);
        if (newN > 0) {
            juce::AudioBuffer<float> resampled(src.getNumChannels(), newN);
            resampled.clear();
            for (int c = 0; c < src.getNumChannels(); ++c) {
                juce::LagrangeInterpolator interp;
                interp.process(ratio,
                               src.getReadPointer(c),
                               resampled.getWritePointer(c),
                               newN);
            }
            src = std::move(resampled);
        }
    }

    auto out = std::make_shared<PreparedClipBuffer>();
    out->buf        = std::move(src);
    out->sampleRate = targetSr;
    out->version    = m_clipPrepGen.fetch_add(1);
    return out;
}

void StudioTimeline::rebuildAllClipBufsSync() {
    const int n = (int) m_clips.size();
    std::vector<std::shared_ptr<PreparedClipBuffer>> rebuilt;
    rebuilt.reserve((size_t) n);
    for (int i = 0; i < n; ++i) {
        rebuilt.push_back(prepareClipBufferSync(i));
    }
    {
        std::lock_guard<std::mutex> lock(m_renderModelMutex);
        m_clipBufs = std::move(rebuilt);
    }
    spdlog::info("[Studio] LiveSource: rebuilt {} clip buffer(s) sync", n);
}

void StudioTimeline::enqueueClipPrep(int idx) {
    if (!m_useLiveSource.load() || !m_prepPool) return;
    if (idx < 0 || idx >= (int) m_clips.size()) return;
    {
        std::lock_guard<std::mutex> lock(m_renderModelMutex);
        if ((int) m_clipBufs.size() < (int) m_clips.size())
            m_clipBufs.resize(m_clips.size());
    }
    // Capture la version a l enqueue : une edition intermediaire cause au pire une re-prep de plus.
    m_prepPool->addJob(
        [this, idx]() {
            auto pcb = prepareClipBufferSync(idx);
            std::lock_guard<std::mutex> lock(m_renderModelMutex);
            if (idx >= 0 && idx < (int) m_clipBufs.size())
                m_clipBufs[(size_t) idx] = std::move(pcb);
        });
}

void StudioTimeline::wireClipCallback(StudioClip& clip) {
    auto* raw = &clip;
    raw->onChanged = [this, raw]() {
        const int idx = indexOf(raw);
        if (idx < 0) return;
        m_listeners.call([idx](Listener& l){ l.clipChanged(idx); });
        layoutClips();
        enqueueClipPrep(idx);
    };
}

void StudioTimeline::setMasterCompressorAmount(float amt0to10) {
    m_masterCompAmount.store(juce::jlimit(0.0f, 10.0f, amt0to10));
}

float StudioTimeline::getMasterCompressorGainReduction() const noexcept {
    if (!m_audioSource) return 0.0f;
    return m_audioSource->getCompressor().getGainReduction();
}

void StudioTimeline::bouncePlaybackBuffer() {
    const double sr = m_playbackSr;
    const int    ch = 2;
    const double total = std::max(1.0, getTotalLengthSec());
    const int64_t totalFrames = (int64_t) std::ceil(total * sr);
    m_playbackBuffer.setSize(ch, (int) totalFrames, false, true, true);
    m_playbackBuffer.clear();

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    // Phase 1 : preparer chaque clip dans son propre buffer.
    struct PreparedClip {
        juce::AudioBuffer<float> buf;
        int64_t startFrame = 0;
        int64_t endFrame   = 0;
        bool    valid      = false;
    };
    std::vector<PreparedClip> prep(m_clips.size());

    for (size_t idx = 0; idx < m_clips.size(); ++idx) {
        auto& cl = m_clips[idx];
        if (cl->isMuted()) continue;

        bool anySolo = false;
        for (const auto& l : m_laneInfos) if (l.solo) { anySolo = true; break; }
        const int lane = getLaneOfClip((int) idx);
        const auto& laneInfo = (lane < (int) m_laneInfos.size())
                              ? m_laneInfos[(size_t) lane] : LaneInfo{};
        if (anySolo && !laneInfo.solo) continue;
        const float laneGainLin = juce::Decibels::decibelsToGain(laneInfo.volumeDb);

        juce::AudioBuffer<float> src;
        double srcSampleRate = sr;

        if (cl->hasStems()) {
            auto* st = cl->getStems();
            srcSampleRate = st->sampleRate;
            const int n = std::max({ st->drums.getNumSamples(),
                                      st->bass.getNumSamples(),
                                      st->other.getNumSamples(),
                                      st->vocals.getNumSamples() });
            const int chs = std::max({ st->drums.getNumChannels(),
                                        st->bass.getNumChannels(),
                                        st->other.getNumChannels(),
                                        st->vocals.getNumChannels() });
            if (n > 0 && chs > 0) {
                src.setSize(chs, n, false, true, false);
                src.clear();
                auto addIfNotMuted = [&](const juce::AudioBuffer<float>& s, int sIdx) {
                    if (cl->isStemMuted(sIdx) || s.getNumSamples() == 0) return;
                    const int N = std::min(n, s.getNumSamples());
                    for (int c = 0; c < chs; ++c) {
                        const int sc = std::min(c, s.getNumChannels() - 1);
                        if (sc < 0) continue;
                        src.addFrom(c, 0, s, sc, 0, N);
                    }
                };
                addIfNotMuted(st->drums,  0);
                addIfNotMuted(st->bass,   1);
                addIfNotMuted(st->other,  2);
                addIfNotMuted(st->vocals, 3);
            }
        } else {
            juce::File f{ juce::String::fromUTF8(cl->getTrack().filePath.c_str()) };
            if (!f.existsAsFile()) continue;
            std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
            if (!reader) continue;

            const int64_t srcStart = (int64_t) (cl->getAudioInSec()  * reader->sampleRate);
            const int64_t srcEnd   = (int64_t) (cl->getAudioOutSec() * reader->sampleRate);
            const int64_t srcLen   = std::max<int64_t>(0, srcEnd - srcStart);
            if (srcLen <= 0) continue;
            src.setSize((int) reader->numChannels, (int) srcLen, false, true, false);
            reader->read(&src, 0, (int) srcLen, srcStart, true, true);
            srcSampleRate = reader->sampleRate;
        }

        if (src.getNumSamples() <= 0) continue;

        if (cl->isReversed()) {
            for (int c = 0; c < src.getNumChannels(); ++c)
                std::reverse(src.getWritePointer(c),
                             src.getWritePointer(c) + src.getNumSamples());
        }

        const int fadeInN  = (int) (cl->getFade().inSec  * srcSampleRate);
        const int fadeOutN = (int) (cl->getFade().outSec * srcSampleRate);
        if (fadeInN > 0)
            for (int c = 0; c < src.getNumChannels(); ++c)
                src.applyGainRamp(c, 0, std::min(fadeInN, src.getNumSamples()), 0.0f, 1.0f);
        if (fadeOutN > 0) {
            const int s = std::max(0, src.getNumSamples() - fadeOutN);
            const int n = src.getNumSamples() - s;
            for (int c = 0; c < src.getNumChannels(); ++c)
                src.applyGainRamp(c, s, n, 1.0f, 0.0f);
        }

        if (!cl->getEffects().empty())
            applyClipEffectsToBuffer(src, srcSampleRate, cl->getEffects());

        const float vol = cl->getVolume() * laneGainLin;
        if (std::abs(vol - 1.0f) > 1e-6f) src.applyGain(vol);

        if (std::abs(srcSampleRate - sr) > 1.0 && src.getNumSamples() > 0) {
            const double ratio = srcSampleRate / sr;
            const int newN = (int) std::ceil(src.getNumSamples() / ratio);
            if (newN > 0) {
                juce::AudioBuffer<float> resampled(src.getNumChannels(), newN);
                resampled.clear();
                for (int c = 0; c < src.getNumChannels(); ++c) {
                    juce::LagrangeInterpolator interp;
                    interp.process(ratio,
                                   src.getReadPointer(c),
                                   resampled.getWritePointer(c),
                                   newN);
                }
                src = std::move(resampled);
            }
        }

        prep[idx].buf        = std::move(src);
        prep[idx].startFrame = (int64_t) (cl->getStartSec() * sr);
        prep[idx].endFrame   = prep[idx].startFrame + prep[idx].buf.getNumSamples();
        prep[idx].valid      = true;
    }

    // Phase 2 : identifier les zones d overlap reservees aux transitions.
    struct ReservedZone { int64_t s, e; size_t pairIdx; };
    std::vector<ReservedZone> reserved;
    for (size_t i = 0; i + 1 < m_clips.size() && i < m_transitions.size(); ++i) {
        if (!prep[i].valid || !prep[i + 1].valid) continue;
        const int64_t ovS = std::max(prep[i].startFrame,     prep[i + 1].startFrame);
        const int64_t ovE = std::min(prep[i].endFrame,       prep[i + 1].endFrame);
        if (ovE - ovS >= (int64_t) (sr * 0.005)) {
            reserved.push_back({ ovS, ovE, i });
        }
    }

    // Phase 3 : sommer les clips en sautant les zones reservees.
    auto sumRange = [&](const PreparedClip& pc, int64_t from, int64_t to) {
        if (!pc.valid) return;
        from = std::max(from, pc.startFrame);
        to   = std::min(to,   pc.endFrame);
        if (to <= from) return;
        const int dstStart = (int) from;
        const int srcStart = (int) (from - pc.startFrame);
        const int n        = (int) (to - from);
        if (dstStart < 0 || dstStart + n > (int) totalFrames) return;
        for (int outCh = 0; outCh < ch; ++outCh) {
            const int sc = std::min(outCh, pc.buf.getNumChannels() - 1);
            if (sc < 0) continue;
            m_playbackBuffer.addFrom(outCh, dstStart, pc.buf, sc, srcStart, n);
        }
    };

    for (size_t idx = 0; idx < prep.size(); ++idx) {
        auto& pc = prep[idx];
        if (!pc.valid) continue;
        std::vector<std::pair<int64_t,int64_t>> holes;
        for (const auto& z : reserved) {
            if (z.e <= pc.startFrame || z.s >= pc.endFrame) continue;
            holes.emplace_back(std::max(z.s, pc.startFrame),
                               std::min(z.e, pc.endFrame));
        }
        std::sort(holes.begin(), holes.end());
        int64_t cursor = pc.startFrame;
        for (const auto& h : holes) {
            if (cursor < h.first)  sumRange(pc, cursor, h.first);
            cursor = std::max(cursor, h.second);
        }
        if (cursor < pc.endFrame)  sumRange(pc, cursor, pc.endFrame);
    }

    // Phase 4 : calculer et ecrire chaque transition.
    for (const auto& z : reserved) {
        const auto& gap = m_transitions[z.pairIdx];
        const PreparedClip& pcA = prep[z.pairIdx];
        const PreparedClip& pcB = prep[z.pairIdx + 1];
        const int64_t ovS = z.s, ovE = z.e;
        const int N = (int) (ovE - ovS);
        if (N <= 0) continue;

        const int channels = ch;
        std::vector<float> aIL((size_t) N * channels, 0.0f);
        std::vector<float> bIL((size_t) N * channels, 0.0f);
        std::vector<float> outIL((size_t) N * channels, 0.0f);

        auto fillIL = [&](const PreparedClip& pc, std::vector<float>& dst) {
            for (int j = 0; j < N; ++j) {
                const int srcSamp = (int) (ovS + j - pc.startFrame);
                if (srcSamp < 0 || srcSamp >= pc.buf.getNumSamples()) continue;
                for (int c = 0; c < channels; ++c) {
                    const int sc = std::min(c, pc.buf.getNumChannels() - 1);
                    if (sc < 0) continue;
                    dst[(size_t) j * channels + c] = pc.buf.getSample(sc, srcSamp);
                }
            }
        };
        fillIL(pcA, aIL);
        fillIL(pcB, bIL);

        const bool useCustom = (gap.kind == TransitionKind::Custom)
                            && (gap.customCurve.size() >= 2);

        if (useCustom) {
            auto eval = [&](double t01) {
                double v = gap.customCurve.front().value;
                for (size_t k = 0; k + 1 < gap.customCurve.size(); ++k) {
                    const auto& p0 = gap.customCurve[k];
                    const auto& p1 = gap.customCurve[k + 1];
                    if (t01 >= p0.timeRel && t01 <= p1.timeRel) {
                        const double f = (t01 - p0.timeRel)
                                       / std::max(1e-6, (p1.timeRel - p0.timeRel));
                        v = p0.value + (p1.value - p0.value) * f;
                        break;
                    }
                    if (t01 > p1.timeRel) v = p1.value;
                }
                return (float) juce::jlimit(0.0, 1.0, v);
            };
            for (int j = 0; j < N; ++j) {
                const float gA = eval((double) j / std::max(1, N - 1));
                const float gB = 1.0f - gA;
                for (int c = 0; c < channels; ++c)
                    outIL[(size_t) j * channels + c] =
                          aIL[(size_t) j * channels + c] * gA
                        + bIL[(size_t) j * channels + c] * gB;
            }
        } else {
            BeatMate::Core::TransitionEngine eng;
            BeatMate::Core::TransitionParams p;
            p.bpm       = (float) std::max(60.0, getMasterBpm());
            p.duration  = (float) std::max(1, gap.bars);
            p.intensity = 0.7f;

            BeatMate::Core::TransitionType ttype = BeatMate::Core::TransitionType::XFade;
            switch (gap.kind) {
                case TransitionKind::Cut:         ttype = BeatMate::Core::TransitionType::Cut;      break;
                case TransitionKind::Fade:        ttype = BeatMate::Core::TransitionType::XFade;    break;
                case TransitionKind::EqualPower:  ttype = BeatMate::Core::TransitionType::XFade;    break;
                case TransitionKind::EchoOut:     ttype = BeatMate::Core::TransitionType::Echo;     break;
                case TransitionKind::FilterSweep: ttype = BeatMate::Core::TransitionType::Filter;   break;
                case TransitionKind::Backspin:    ttype = BeatMate::Core::TransitionType::Backspin; break;
                case TransitionKind::Custom:      ttype = BeatMate::Core::TransitionType::XFade;    break;
            }

            // Application par blocs de 1024 frames pour piloter le progress sweep.
            const int kBlock = 1024;
            for (int s = 0; s < N; s += kBlock) {
                const int blkN = std::min(kBlock, N - s);
                const float progress = (float) (s + blkN * 0.5f) / (float) std::max(1, N);
                eng.apply(aIL.data() + (size_t) s * channels,
                          bIL.data() + (size_t) s * channels,
                          outIL.data() + (size_t) s * channels,
                          blkN, channels, (int) sr, ttype, p, progress);
            }
        }

        // Écrire le résultat de la transition (overwrite, pas addFrom).
        for (int j = 0; j < N; ++j) {
            const int dst = (int) (ovS + j);
            if (dst < 0 || dst >= m_playbackBuffer.getNumSamples()) continue;
            for (int c = 0; c < channels; ++c)
                m_playbackBuffer.setSample(c, dst, outIL[(size_t) j * channels + c]);
        }
    }

    // Master ceiling à -1 dB pour éviter le clipping.
    const float peak = m_playbackBuffer.getMagnitude(0, m_playbackBuffer.getNumSamples());
    const float ceiling = std::pow(10.0f, -1.0f / 20.0f);
    if (peak > ceiling) m_playbackBuffer.applyGain(ceiling / peak);
}

void StudioTimeline::play() {
    if (m_playing.load()) return;
    if (m_useLiveSource.load()) {
        rebuildAllClipBufsSync();
    } else {
        bouncePlaybackBuffer();
    }
    prepareAudioOutput();
    m_playbackFrame.store((int64_t) (m_playhead * m_playbackSr));
    m_playing.store(true);
    m_playStartUnixMs = juce::Time::currentTimeMillis();
    m_playStartSec    = m_playhead;
    // 60 Hz si visible, 15 Hz sinon.
    startTimerHz(isShowing() ? 60 : 15);
    spdlog::info("[Studio] Play from {:.2f} s ({})", m_playhead,
                 m_useLiveSource.load() ? "LIVE" : "BOUNCE");
}

void StudioTimeline::stop() {
    m_playing.store(false);
    stopTimer();
    repaint();
}

void StudioTimeline::timerCallback() {
    if (!m_playing.load()) return;
    const double oldSec = m_playhead;
    // La position visuelle suit l audio thread (m_playbackFrame) : pas de derive.
    const double srRef = (m_playbackSr > 0.0) ? m_playbackSr : 44100.0;
    double newSec = (double) m_playbackFrame.load() / srRef;

    // Loop wrap gere cote UI en bounce ; LiveTimelineSource fait son propre wrap audio-side.
    if (m_rangeLoopEnabled && hasTimeRange() && newSec >= m_rangeEndSec) {
        m_playStartUnixMs = juce::Time::currentTimeMillis();
        m_playStartSec = m_rangeStartSec;
        m_playbackFrame.store((int64_t) (m_rangeStartSec * srRef));
        newSec = m_rangeStartSec;
    }
    setPlayheadSec(newSec);

    const float pps = (float) pixelsPerSec();
    const int oldX = (int) std::round(oldSec * pps);
    const int newX = (int) std::round(newSec * pps);
    const int x0 = std::min(oldX, newX) - 2;
    const int x1 = std::max(oldX, newX) + 2;
    repaint(juce::Rectangle<int>(x0, 0, std::max(4, x1 - x0), getHeight()));
    m_lastPlayheadX = newX;

    if (m_centerPlayhead) {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>()) {
            const int playX = newX + kSidebarW;
            const int vw = vp->getViewWidth();
            const int targetX = std::max(0, playX - vw / 2);
            const auto curPos = vp->getViewPosition();
            // Tolerance 4px pour eviter de spammer setViewPosition a chaque frame.
            if (std::abs(curPos.x - targetX) > 4) {
                vp->setViewPosition(targetX, curPos.y);
            }
        }
    }

    if (m_playhead >= getTotalLengthSec()) stop();
}

juce::String StudioTimeline::formatTimecode(double sec) const {
    const int m  = (int) sec / 60;
    const int s  = (int) sec % 60;
    const int ms = (int) ((sec - std::floor(sec)) * 1000.0);
    return juce::String::formatted("%02d:%02d.%03d", m, s, ms);
}

double StudioTimeline::getAverageBpm() const {
    double sum = 0.0;
    int    n   = 0;
    for (auto& c : m_clips) {
        if (c->getTrack().bpm > 0) { sum += c->getTrack().bpm; ++n; }
    }
    return n > 0 ? sum / n : m_masterBpm;
}

namespace {

int camelotIndex(const std::string& key) {
    if (key.empty()) return -1;
    int num = 0; char letter = 'A';
    for (char c : key) {
        if (c >= '0' && c <= '9') num = num * 10 + (c - '0');
        else if (c == 'A' || c == 'B' || c == 'a' || c == 'b')
            letter = (char) std::toupper((unsigned char) c);
    }
    if (num < 1 || num > 12) return -1;
    return (num - 1) * 2 + (letter == 'B' ? 1 : 0);
}

int camelotDistance(int a, int b) {
    if (a < 0 || b < 0) return 99;
    const int n = std::abs(a - b);
    return std::min(n, 24 - n);
}

}

void StudioTimeline::runMashupAI() {
    if (m_clips.size() < 2) {
        spdlog::warn("[Studio] Mashup IA needs â‰¥ 2 clips");
        return;
    }
    pushUndoSnapshot();
    const double targetBpm = getAverageBpm();
    setMasterBpm(targetBpm);
    const double bar = 4.0 * 60.0 / std::max(1.0, targetBpm);

    std::sort(m_clips.begin(), m_clips.end(),
              [](const std::unique_ptr<StudioClip>& a, const std::unique_ptr<StudioClip>& b) {
                  return camelotIndex(a->getTrack().camelotKey)
                       < camelotIndex(b->getTrack().camelotKey);
              });
    for (auto& c : m_clips) {
        auto* raw = c.get();
        wireClipCallback(*raw);
    }
    for (int k_b3 = 0; k_b3 < (int) m_clips.size(); ++k_b3) enqueueClipPrep(k_b3);

    double t = 0.0;
    for (size_t i = 0; i < m_clips.size(); ++i) {
        auto& c = m_clips[i];
        const double startInTrack = c->getAudioInSec();
        const double mashLen = std::min(c->getLengthSec(), bar * 32);
        c->setAudioRange(startInTrack, startInTrack + mashLen);
        c->setStartSec(t);
        c->setFadeIn (bar * 2);
        c->setFadeOut(bar * 2);
        t += mashLen - bar * 4;
    }
    if (m_transitions.size() < m_clips.size())
        m_transitions.resize(m_clips.size());
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        m_transitions[i].kind = TransitionKind::EqualPower;
        m_transitions[i].bars = 2;
    }
    layoutClips();
    repaint();
    spdlog::info("[Studio] Mashup IA pro: {} clips key-sorted, BPM={:.1f}, 8-beat EQ-power",
                 m_clips.size(), targetBpm);
}

void StudioTimeline::runMegamixAI() {
    if (m_clips.size() < 2) {
        spdlog::warn("[Studio] Megamix IA needs â‰¥ 2 clips");
        return;
    }
    pushUndoSnapshot();
    const double targetBpm = getAverageBpm();
    setMasterBpm(targetBpm);
    const double bar = 4.0 * 60.0 / std::max(1.0, targetBpm);

    auto& clips = m_clips;
    std::vector<size_t> order(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) {
                  return clips[a]->getTrack().bpm < clips[b]->getTrack().bpm;
              });
    std::vector<size_t> chain; chain.reserve(order.size());
    if (!order.empty()) {
        chain.push_back(order.front());
        order.erase(order.begin());
    }
    while (!order.empty()) {
        const auto last = chain.back();
        size_t bestIdx = 0; int bestDist = 999;
        for (size_t k = 0; k < order.size(); ++k) {
            const int d = camelotDistance(
                camelotIndex(clips[last]->getTrack().camelotKey),
                camelotIndex(clips[order[k]]->getTrack().camelotKey));
            if (d < bestDist) { bestDist = d; bestIdx = k; }
        }
        chain.push_back(order[bestIdx]);
        order.erase(order.begin() + (long) bestIdx);
    }
    std::vector<std::unique_ptr<StudioClip>> reord;
    reord.reserve(chain.size());
    for (auto i : chain) reord.push_back(std::move(clips[i]));
    m_clips = std::move(reord);
    for (auto& cp : m_clips) {
        auto* raw = cp.get();
        wireClipCallback(*raw);
    }
    for (int k_b3 = 0; k_b3 < (int) m_clips.size(); ++k_b3) enqueueClipPrep(k_b3);

    double t = 0.0;
    const double xfade = bar * 16;
    for (auto& c : m_clips) {
        c->setStartSec(t);
        const double L = c->getLengthSec();
        c->setFadeIn (xfade);
        c->setFadeOut(xfade);
        t += std::max(bar * 32, L) - xfade;
    }
    if (m_transitions.size() < m_clips.size())
        m_transitions.resize(m_clips.size());
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        m_transitions[i].kind = TransitionKind::EqualPower;
        m_transitions[i].bars = 16;
    }
    layoutClips();
    repaint();
    spdlog::info("[Studio] Megamix IA pro: {} clips Camelot-chained, BPM={:.1f}, 16-bar EQ-power",
                 m_clips.size(), targetBpm);
}

void StudioTimeline::runMedleyAI() {
    if (m_clips.empty()) return;
    pushUndoSnapshot();
    const double targetBpm = getAverageBpm();
    setMasterBpm(targetBpm);
    const double bar = 4.0 * 60.0 / std::max(1.0, targetBpm);
    const double hookBars = 16;
    const double hookSec  = bar * hookBars;
    const double xfade    = bar * 2;

    std::sort(m_clips.begin(), m_clips.end(),
              [](const std::unique_ptr<StudioClip>& a, const std::unique_ptr<StudioClip>& b) {
                  return camelotIndex(a->getTrack().camelotKey)
                       < camelotIndex(b->getTrack().camelotKey);
              });
    for (auto& cp : m_clips) {
        auto* raw = cp.get();
        wireClipCallback(*raw);
    }
    for (int k_b3 = 0; k_b3 < (int) m_clips.size(); ++k_b3) enqueueClipPrep(k_b3);

    double t = 0.0;
    for (auto& c : m_clips) {
        const auto& tr = c->getTrack();
        const double dur = tr.duration > 0.0 ? tr.duration : c->getLengthSec();
        double hookStart = std::min(30.0, std::max(0.0, dur * 0.33));
        hookStart = std::min(hookStart, std::max(0.0, dur - hookSec));
        c->setAudioRange(hookStart, hookStart + hookSec);
        c->setStartSec(t);
        c->setFadeIn (xfade);
        c->setFadeOut(xfade);
        t += hookSec - xfade;
    }
    if (m_transitions.size() < m_clips.size())
        m_transitions.resize(m_clips.size());
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        m_transitions[i].kind = TransitionKind::EqualPower;
        m_transitions[i].bars = 2;
    }
    layoutClips();
    repaint();
    spdlog::info("[Studio] Medley IA pro: {} hooks of {:.1f}s @ {:.1f} BPM",
                 m_clips.size(), hookSec, targetBpm);
}

void StudioTimeline::runRemixAI() {
    if (m_selected < 0 || m_selected >= (int) m_clips.size()) {
        spdlog::warn("[Studio] Remix IA needs a selected clip");
        return;
    }
    pushUndoSnapshot();

    auto& src = *m_clips[(size_t) m_selected];
    const double bpm = src.getTrack().bpm > 0 ? src.getTrack().bpm : m_masterBpm;
    const double bar = 4.0 * 60.0 / std::max(1.0, bpm);
    const double aIn = src.getAudioInSec();
    const double aOut = src.getAudioOutSec();
    const double mid = aIn + (aOut - aIn) * 0.5;
    const Models::Track baseTrack = src.getTrack();

    auto makeClip = [&](double rangeIn, double rangeOut, double tlStart,
                        double fadeIn, double fadeOut, bool reversed = false) {
        auto cp = std::make_unique<StudioClip>(baseTrack, *this);
        cp->setAudioRange(rangeIn, rangeOut);
        cp->setStartSec(tlStart);
        cp->setFadeIn(fadeIn);
        cp->setFadeOut(fadeOut);
        if (reversed) cp->setReversed(true);
        auto* raw = cp.get();
        wireClipCallback(*raw);
        addAndMakeVisible(*raw);
        m_clips.push_back(std::move(cp));
    };

    m_clips.erase(m_clips.begin() + m_selected);
    m_selected = -1;

    double t = 0.0;
    const double introLen   = bar * 8;
    const double buildLen   = bar * 4;
    const double dropLen    = bar * 16;
    const double breakLen   = bar * 8;
    const double dropRepeat = bar * 16;
    const double outroLen   = bar * 8;

    makeClip(aIn, std::min(aOut, aIn + introLen), t, 0.0, bar);
    t += introLen - bar;

    const double bIn  = std::max(aIn, mid - buildLen);
    makeClip(bIn, mid, t, bar * 0.5, bar * 0.5, true);
    t += buildLen - bar * 0.5;

    const double dIn  = mid;
    const double dOut = std::min(aOut, mid + dropLen);
    makeClip(dIn, dOut, t, bar * 0.5, bar);
    t += (dOut - dIn) - bar;

    const double brIn  = aIn;
    const double brOut = std::min(aOut, aIn + breakLen);
    makeClip(brIn, brOut, t, bar, bar);
    t += breakLen - bar;

    makeClip(dIn, std::min(aOut, dIn + dropRepeat), t, bar, bar);
    t += dropRepeat - bar;

    const double oIn = std::max(aIn, aOut - outroLen);
    makeClip(oIn, aOut, t, bar, 0.0);

    if (m_transitions.size() < m_clips.size())
        m_transitions.resize(m_clips.size());
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        m_transitions[i].kind = TransitionKind::EqualPower;
        m_transitions[i].bars = 4;
    }
    layoutClips();
    repaint();
    spdlog::info("[Studio] Remix IA pro: 6-section structure (intro/build/drop/break/drop2/outro)");
}

double StudioTimeline::snap(double sec) const {
    if (m_snap == SnapMode::Off) return sec;
    // snap() consulte getBpmAt(sec) pour respecter les tempo points ; Bar/HalfBar = cumul exact depuis 0.
    if (m_snap == SnapMode::Beat || m_snap == SnapMode::HalfBeat) {
        const double bpm  = std::max(1.0, getBpmAt(sec));
        const double beat = 60.0 / bpm;
        const double step = (m_snap == SnapMode::HalfBeat) ? (beat * 0.5) : beat;
        return std::round(sec / step) * step;
    }
    if (sec <= 0.0) return 0.0;
    const double granularity = (m_snap == SnapMode::HalfBar) ? 0.5 : 1.0;
    const double tsig = (double) std::max(1, m_tsigNum);
    double cursor     = 0.0;
    double prevCursor = 0.0;
    int safety = 0;
    while (cursor < sec && safety++ < 100000) {
        const double bpm  = std::max(1.0, getBpmAt(cursor));
        const double beat = 60.0 / bpm;
        const double bar  = beat * tsig * granularity;
        if (bar < 1e-6) break;
        prevCursor = cursor;
        cursor += bar;
    }
    return (sec - prevCursor) < (cursor - sec) ? prevCursor : cursor;
}

double StudioTimeline::getTotalLengthSec() const {
    double end = 0.0;
    for (auto& c : m_clips) {
        const double e = c->getStartSec() + c->getLengthSec();
        if (e > end) end = e;
    }
    return end;
}

void StudioTimeline::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xFF06060C));

    auto b = getLocalBounds();
    g.setColour(juce::Colour(0xFF0A0A14));
    g.fillRect(b.removeFromTop(m_rulerHeight));

    if (m_showBeatGrid) paintBeatGrid(g);
    paintRulerBar(g);
    paintTransitions(g);
    paintPlayhead(g);

    // Sidebar opaque peinte par-dessus les clips dans la zone gauche kSidebarW.
    paintTrackHeaders(g);

    if (m_rubbering && !m_rubber.isEmpty()) {
        g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.18f));
        g.fillRect(m_rubber);
        g.setColour(juce::Colour(0xFF22D3EE).withAlpha(0.85f));
        g.drawRect(m_rubber, 1.2f);
    }

    if (!m_phrases.empty()) {
        const float pps   = (float) pixelsPerSec();
        const int   sH    = 18;
        const int   bandY = getHeight() - sH - 14;
        g.setColour(juce::Colour(0xFF0A0A14));
        g.fillRect(0, bandY, getWidth(), 14);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        for (const auto& p : m_phrases) {
            const float x = (float)(p.sec * pps) + (float) kSidebarW;
            const float w = std::max(2.0f, (float) (p.dur * pps));
            const auto r = juce::Rectangle<float>(x, (float) bandY, w, 14.0f);
            g.setColour(p.colour.withAlpha(0.32f));
            g.fillRoundedRectangle(r.reduced(0.5f), 2.0f);
            g.setColour(p.colour);
            g.fillRect(x, (float) bandY, 1.5f, 14.0f);
            g.setColour(juce::Colour(0xFFE2E8F0));
            g.drawText(juce::String(p.label),
                       r.toNearestInt().reduced(4, 0),
                       juce::Justification::centredLeft, true);
        }
    }

    if (hasTimeRange()) {
        const float pps = (float) pixelsPerSec();
        const float x1 = (float)(m_rangeStartSec * pps) + (float) kSidebarW;
        const float x2 = (float)(m_rangeEndSec   * pps) + (float) kSidebarW;
        const float w  = std::max(2.0f, x2 - x1);
        const auto bandTop = (float) m_rulerHeight;
        const auto bandH   = (float) (getHeight() - m_rulerHeight - 18);
        g.setColour(juce::Colour(0xFFFCD34D).withAlpha(0.16f));
        g.fillRect(x1, bandTop, w, bandH);
        g.setColour(juce::Colour(0xFFFCD34D));
        g.fillRect(x1 - 0.5f, bandTop, 1.5f, bandH);
        g.fillRect(x2 - 0.5f, bandTop, 1.5f, bandH);
        g.setColour(juce::Colour(0xFF1F2937));
        g.fillRect(x1, bandTop, w, 14.0f);
        g.setColour(juce::Colour(0xFFFCD34D));
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.drawText(juce::String((m_rangeEndSec - m_rangeStartSec), 2) + " s",
                   juce::Rectangle<float>(x1, bandTop, w, 14.0f),
                   juce::Justification::centred);
    }

    {
        StudioClip* dragged = nullptr;
        for (auto& cp : m_clips) { if (cp->isDragging()) { dragged = cp.get(); break; } }
        if (dragged != nullptr) {
            const float pps  = (float) pixelsPerSec();
            const double s   = dragged->getStartSec();
            const double len = dragged->getLengthSec();
            const double bpm = std::max(1.0, getBpmAt(s));
            const double bars  = len / (60.0 / bpm * std::max(1, m_tsigNum));
            const double snapped = (m_snap == SnapMode::Off) ? s : snap(s);
            const double deltaToSnap = s - snapped;

            juce::String hud;
            hud << "Pos " << formatTimecode(s)
                << "   Len " << juce::String(len, 2) << "s"
                << " (" << juce::String(bars, 1) << " bars)";
            if (m_snap != SnapMode::Off && std::abs(deltaToSnap) > 0.001) {
                hud << "   Î” " << juce::String(deltaToSnap * 1000.0, 0) << "ms";
            }

            g.setFont(juce::Font(11.0f, juce::Font::bold));
            const int textW = juce::Font(11.0f, juce::Font::bold).getStringWidth(hud) + 16;
            const int hudH  = 22;
            const int hudY  = std::max(m_rulerHeight + 4,
                                       dragged->getY() - hudH - 4);
            const int hudX  = std::min(std::max(kSidebarW + 4,
                                                (int) std::round(s * pps) + kSidebarW),
                                       getWidth() - textW - 4);
            juce::Rectangle<int> hudR(hudX, hudY, textW, hudH);
            g.setColour(juce::Colour(0xFF111827).withAlpha(0.92f));
            g.fillRoundedRectangle(hudR.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xFFFCD34D));
            g.drawRoundedRectangle(hudR.toFloat().reduced(0.5f), 4.0f, 1.0f);
            g.setColour(juce::Colour(0xFFFEF3C7));
            g.drawText(hud, hudR.reduced(8, 0), juce::Justification::centredLeft);
        }
    }

    const int statusH = 18;
    juce::Rectangle<int> status(0, getHeight() - statusH, getWidth(), statusH);
    g.setColour(juce::Colour(0xFF0A0A14));
    g.fillRect(status);
    g.setColour(juce::Colour(0xFF1E293B));
    g.drawHorizontalLine(status.getY(), 0.0f, (float) getWidth());
    g.setColour(juce::Colour(0xFF94A3B8));
    g.setFont(juce::Font(9.5f));
    juce::String left;
    left << juce::String(getNumClips()) << " clip(s)   "
         << formatTimecode(getTotalLengthSec()) << "   "
         << "BPM " << juce::String(getAverageBpm(), 1) << "   "
         << juce::String(m_tsigNum) << "/" << juce::String(m_tsigDen);
    if (hasTimeRange()) {
        const double dur = m_rangeEndSec - m_rangeStartSec;
        const double bpm = std::max(1.0, getBpmAt(m_rangeStartSec));
        const double bars = dur / (60.0 / bpm * std::max(1, m_tsigNum));
        left << "   |   Sel " << juce::String(dur, 2) << "s ("
             << juce::String(bars, 1) << " bars)";
    }
    g.drawText(left, status.reduced(8, 2), juce::Justification::centredLeft);

    const bool isPlay = m_playing.load();
    juce::String right;
    right << (isPlay ? "PLAY" : "STOP") << "   "
          << formatTimecode(m_playhead) << "   "
          << "Vol " << juce::String((int) std::round(m_masterVol.load() * 100.0f)) << "%"
          << "   Zoom " << juce::String(pixelsPerSec(), 0) << " px/s";
    g.setColour(isPlay ? juce::Colour(0xFF22C55E) : juce::Colour(0xFF94A3B8));
    g.drawText(right, status.reduced(8, 2), juce::Justification::centredRight);
}

void StudioTimeline::resized() {
    layoutClips();
    layoutProgressOverlay();
}

void StudioTimeline::layoutClips() {
    const float pps = (float) pixelsPerSec();
    const int yTop = m_rulerHeight + 4;
    const int totalH = std::max(80, getHeight() - m_rulerHeight - 8);

    // Greedy lane assignment : les clips qui se chevauchent vont sur des lanes separees.
    std::vector<std::pair<size_t, std::pair<double,double>>> ordered;
    ordered.reserve(m_clips.size());
    for (size_t i = 0; i < m_clips.size(); ++i) {
        const double s = m_clips[i]->getStartSec();
        const double e = s + m_clips[i]->getLengthSec();
        ordered.push_back({ i, { s, e } });
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const auto& a, const auto& b) { return a.second.first < b.second.first; });

    std::vector<int> lanes(m_clips.size(), 0);
    std::vector<double> laneEnds;
    int maxLane = 0;
    for (auto& it : ordered) {
        const double s = it.second.first;
        const double e = it.second.second;
        int chosen = -1;
        for (size_t k = 0; k < laneEnds.size(); ++k) {
            if (laneEnds[k] <= s + 0.001) { chosen = (int) k; break; }
        }
        if (chosen < 0) { chosen = (int) laneEnds.size(); laneEnds.push_back(e); }
        else laneEnds[chosen] = e;
        lanes[it.first] = chosen;
        if (chosen > maxLane) maxLane = chosen;
    }
    const int numLanes = std::max(1, maxLane + 1);
    const int laneH = std::max(40, totalH / numLanes - 2);

    for (size_t i = 0; i < m_clips.size(); ++i) {
        auto& c = m_clips[i];
        const int x = kSidebarW + (int) std::round(c->getStartSec() * pps);
        const int w = std::max((int) kClipMinWidthPx,
                               (int) std::round(c->getLengthSec() * pps));
        const int y = yTop + lanes[i] * (laneH + 2);
        c->setBounds(x, y, w, laneH);
    }

    m_clipLaneCache.assign(m_clips.size(), 0);
    for (size_t i = 0; i < m_clips.size(); ++i) m_clipLaneCache[i] = lanes[i];
    ensureLaneInfos(numLanes);
    applyLaneStateToClips();
}

void StudioTimeline::paintBeatGrid(juce::Graphics& g) const {
    if (m_zoom < 1.2) return;
    const double total  = std::max(1.0, getTotalLengthSec() + 8.0);
    const float  pps    = (float) pixelsPerSec();
    const int    top    = m_rulerHeight;
    const int    bottom = getHeight();
    const int    beatsPerBar = std::max(1, m_tsigNum);

    // Dessin limite au viewport visible : gain enorme sur les projets longs.
    const auto clip = g.getClipBounds();
    const double secStart = std::max(0.0, (double)(clip.getX() - kSidebarW) / std::max(1.0, (double) pps));
    const double secEnd   = std::min(total, (double)(clip.getRight() - kSidebarW) / std::max(1.0, (double) pps));
    if (secEnd <= secStart) return;

    int beatIdx = 0;
    double t = 0.0;
    {
        double bpmCursor = getBpmAt(0.0);
        double beatLen = 60.0 / std::max(1.0, bpmCursor);
        while (t + beatLen < secStart) {
            t += beatLen;
            ++beatIdx;
            const double bpmHere = getBpmAt(t);
            if (std::abs(bpmHere - bpmCursor) > 0.01) {
                bpmCursor = bpmHere;
                beatLen = 60.0 / std::max(1.0, bpmCursor);
            }
        }
    }
    while (t < secEnd) {
        const double bpmHere = getBpmAt(t);
        const double beatLen = 60.0 / std::max(1.0, bpmHere);
        const float  x = (float)(t * pps) + (float) kSidebarW;
        if (x >= (float) clip.getX() - 1.0f && x <= (float) clip.getRight() + 1.0f) {
            const bool downbeat = (beatIdx % beatsPerBar) == 0;
            if (downbeat) {
                g.setColour(juce::Colour(0xFFFCD34D).withAlpha(0.16f));
                g.fillRect(x - 0.5f, (float) top, 1.0f, (float) (bottom - top));
            } else if (m_zoom >= 5.0) {
                g.setColour(juce::Colour(0x12FFFFFF));
                g.fillRect(x, (float) top + 10.0f, 0.5f, (float) (bottom - top - 20));
            }
        }
        t += beatLen;
        ++beatIdx;
        if (beatLen <= 0.0) break;
    }
}

void StudioTimeline::paintRulerBar(juce::Graphics& g) const {
    const double beat  = 60.0 / std::max(1.0, m_masterBpm);
    const double total = std::max(1.0, getTotalLengthSec() + 8.0);
    const float  pps   = (float) pixelsPerSec();

    g.setColour(juce::Colour(0xFF1E293B));
    g.fillRect(0, m_rulerHeight - 1, getWidth(), 1);

    g.setColour(juce::Colour(0xFF94A3B8));
    g.setFont(juce::Font(9.0f));

    const auto clip = g.getClipBounds();
    const double secStart = std::max(0.0, (double)(clip.getX() - kSidebarW) / std::max(1.0, (double) pps));
    const double secEnd   = std::min(total, (double)(clip.getRight() - kSidebarW) / std::max(1.0, (double) pps));
    const double barLen   = beat * 4.0;
    int bar = std::max(1, (int) std::floor(secStart / barLen) + 1);
    const int barLast = (int) std::ceil(secEnd / barLen) + 1;
    for (double t = (bar - 1) * barLen; bar <= barLast && t < total; t += barLen, ++bar) {
        const float x = (float)(t * pps) + (float) kSidebarW;
        if (x < (float) clip.getX() - 32.0f || x > (float) clip.getRight() + 32.0f) continue;
        g.drawText(juce::String(bar), (int) x - 16, 4, 32, m_rulerHeight - 8,
                   juce::Justification::centred);
    }

    for (auto& c : m_clips) {
        const float x1 = (float)(c->getStartSec() * pps) + (float) kSidebarW;
        const float x2 = (float)((c->getStartSec() + c->getLengthSec()) * pps) + (float) kSidebarW;
        if (x2 < 0 || x1 > getWidth()) continue;
        g.setColour(c->isSelected() ? juce::Colour(0xFF22D3EE)
                                     : juce::Colour(0xFF475569));
        g.fillRect(x1, (float) m_rulerHeight - 4.0f,
                   std::max(2.0f, x2 - x1), 3.0f);
    }

    g.setFont(juce::Font(8.5f, juce::Font::bold));
    for (size_t i = 0; i < m_tempoPoints.size(); ++i) {
        const auto& tp = m_tempoPoints[i];
        const float x = (float)(tp.sec * pps) + (float) kSidebarW;
        if (x < (float) clip.getX() - 40.0f || x > (float) clip.getRight() + 40.0f) continue;
        const float cy = (float) m_rulerHeight * 0.5f;
        g.setColour(juce::Colour(0xFF22C55E).withAlpha(0.30f));
        g.fillEllipse(x - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        const bool dragging = (m_dragTempoPointIdx == (int) i);
        g.setColour(dragging ? juce::Colour(0xFFFACC15) : juce::Colour(0xFF22C55E));
        g.fillEllipse(x - 3.0f, cy - 3.0f, 6.0f, 6.0f);
        g.setColour(juce::Colour(0xFFE2E8F0));
        g.drawText(juce::String(tp.bpm, 1) + " BPM",
                   (int) x - 28, 1, 56, 10, juce::Justification::centred);
    }
}

void StudioTimeline::paintPlayhead(juce::Graphics& g) const {
    const float x = (float)(m_playhead * pixelsPerSec()) + (float) kSidebarW;
    g.setColour(juce::Colour(0xFFEF4444));
    g.fillRect(x - 0.5f, 0.0f, 1.5f, (float) getHeight());
    juce::Path head;
    head.addTriangle(x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 10.0f);
    g.fillPath(head);
}

void StudioTimeline::paintTransitions(juce::Graphics& g) const {
    if (m_clips.size() < 2) return;
    const float pps = (float) pixelsPerSec();
    const int yTop = m_rulerHeight + 4;
    const int totalH = std::max(80, getHeight() - m_rulerHeight - 8);

    const juce::Colour kFrameBlue (0xFF3B82F6);
    const juce::Colour kCurveGold (0xFFFCD34D);

    // L enveloppe visuelle DOIT correspondre au DSP reel : delegue a TransitionEngine::sampleEnvelope.
    auto kindToEngineType = [](TransitionKind k) -> BeatMate::Core::TransitionType {
        switch (k) {
            case TransitionKind::Cut:         return BeatMate::Core::TransitionType::Cut;
            case TransitionKind::Fade:        return BeatMate::Core::TransitionType::XFade;
            case TransitionKind::EqualPower:  return BeatMate::Core::TransitionType::XFade;
            case TransitionKind::EchoOut:     return BeatMate::Core::TransitionType::Echo;
            case TransitionKind::FilterSweep: return BeatMate::Core::TransitionType::Filter;
            case TransitionKind::Backspin:    return BeatMate::Core::TransitionType::Backspin;
            case TransitionKind::Custom:      return BeatMate::Core::TransitionType::XFade;
        }
        return BeatMate::Core::TransitionType::XFade;
    };

    auto envelopeAt = [&](TransitionKind kind, float t) -> float {
        // gainB = 1 - gainA (sauf cas particuliers : Fade lineaire stricte).
        if (kind == TransitionKind::Fade) return juce::jlimit(0.0f, 1.0f, t);
        if (kind == TransitionKind::Custom) return juce::jlimit(0.0f, 1.0f, t);
        const float gainA = BeatMate::Core::TransitionEngine::sampleEnvelope(
                                kindToEngineType(kind),
                                juce::jlimit(0.0f, 1.0f, t));
        return juce::jlimit(0.0f, 1.0f, 1.0f - gainA);
    };

    auto kindLabel = [](TransitionKind kind) -> const char* {
        switch (kind) {
            case TransitionKind::Cut:         return "Cut";
            case TransitionKind::Fade:        return "Fade";
            case TransitionKind::EqualPower:  return "EqualPower";
            case TransitionKind::EchoOut:     return "EchoOut";
            case TransitionKind::FilterSweep: return "FilterSweep";
            case TransitionKind::Backspin:    return "Backspin";
            case TransitionKind::Custom:      return "Custom";
        }
        return "?";
    };

    auto drawGlyph = [&](TransitionKind kind, float cx, float cy) {
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        switch (kind) {
            case TransitionKind::Cut: {
                // Ciseaux : 2 cercles + lame
                g.drawEllipse(cx - 6.0f, cy - 4.0f, 5.0f, 5.0f, 1.0f);
                g.drawEllipse(cx - 6.0f, cy + 0.0f, 5.0f, 5.0f, 1.0f);
                g.drawLine(cx - 1.0f, cy + 0.0f, cx + 6.0f, cy - 3.0f, 1.2f);
                g.drawLine(cx - 1.0f, cy + 3.0f, cx + 6.0f, cy + 3.0f, 1.2f);
                break;
            }
            case TransitionKind::Fade: {
                juce::Path p;
                p.addTriangle(cx - 7.0f, cy + 4.0f, cx + 7.0f, cy - 4.0f, cx + 7.0f, cy + 4.0f);
                g.fillPath(p);
                break;
            }
            case TransitionKind::EqualPower: {
                // Sablier
                juce::Path p;
                p.startNewSubPath(cx - 6.0f, cy - 5.0f);
                p.lineTo         (cx + 6.0f, cy - 5.0f);
                p.lineTo         (cx - 6.0f, cy + 5.0f);
                p.lineTo         (cx + 6.0f, cy + 5.0f);
                p.closeSubPath();
                g.strokePath(p, juce::PathStrokeType(1.2f));
                break;
            }
            case TransitionKind::EchoOut: {
                // 3 cercles décroissants
                for (int k = 0; k < 3; ++k) {
                    g.setColour(juce::Colours::white.withAlpha(0.92f - k * 0.27f));
                    g.drawEllipse(cx - 7.0f + k * 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.1f);
                }
                break;
            }
            case TransitionKind::FilterSweep: {
                juce::Path p;
                for (int k = 0; k <= 18; ++k) {
                    const float fx = cx - 8.0f + (float) k;
                    const float fy = cy + 4.0f - 8.0f / (1.0f + std::exp((k - 9.0f) * 0.55f));
                    if (k == 0) p.startNewSubPath(fx, fy); else p.lineTo(fx, fy);
                }
                g.strokePath(p, juce::PathStrokeType(1.3f));
                break;
            }
            case TransitionKind::Backspin: {
                juce::Path p;
                p.startNewSubPath(cx - 5.0f, cy - 4.0f);
                p.lineTo         (cx + 5.0f, cy - 4.0f);
                p.lineTo         (cx + 0.0f, cy + 5.0f);
                p.closeSubPath();
                g.fillPath(p);
                break;
            }
            case TransitionKind::Custom: {
                for (int k = 0; k < 5; ++k)
                    g.fillEllipse(cx - 8.0f + k * 4.0f, cy - 1.0f, 2.0f, 2.0f);
                break;
            }
        }
    };

    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        const auto& a = *m_clips[i];
        const auto& b = *m_clips[i + 1];
        const float ax = (float)((a.getStartSec() + a.getLengthSec()) * pps) + (float) kSidebarW;
        const float bx = (float)(b.getStartSec() * pps) + (float) kSidebarW;
        if (bx <= ax + 1.0f) continue;

        TransitionKind kind = TransitionKind::EqualPower;
        int            bars = 8;
        if (i < m_transitions.size()) {
            kind = m_transitions[i].kind;
            bars = m_transitions[i].bars;
        }

        const juce::Rectangle<float> tr(ax, (float) yTop, bx - ax, (float) totalH);

        g.setColour(kFrameBlue.withAlpha(0.16f));
        g.fillRoundedRectangle(tr, 6.0f);

        g.setColour(kFrameBlue.withAlpha(0.85f));
        g.drawRoundedRectangle(tr, 6.0f, 1.4f);

        const float headerH = 14.0f;
        juce::Rectangle<float> hdr(tr.getX(), tr.getY(), tr.getWidth(), headerH);
        g.setColour(kFrameBlue.withAlpha(0.95f));
        g.fillRoundedRectangle(hdr, 4.0f);

        drawGlyph(kind, hdr.getX() + 12.0f, hdr.getCentreY());

        if (hdr.getWidth() > 80.0f) {
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions("Segoe UI", 10.0f, juce::Font::bold));
            const juce::String lab = juce::String(kindLabel(kind)) + "  ·  " + juce::String(bars) + " bars";
            g.drawText(lab,
                       hdr.toNearestInt().withTrimmedLeft(24).withTrimmedRight(8),
                       juce::Justification::centredLeft, true);
        }

        juce::Rectangle<float> body = tr.withTrimmedTop(headerH + 2.0f).reduced(4.0f, 4.0f);
        const int N = std::max(24, (int) body.getWidth() / 4);
        juce::Path curve;
        for (int k = 0; k <= N; ++k) {
            const float t = (float) k / (float) N;
            float v;
            if (kind == TransitionKind::Custom
                && i < m_transitions.size()
                && !m_transitions[i].customCurve.empty()) {
                const auto& cpts = m_transitions[i].customCurve;
                v = (float) cpts.front().value;
                for (size_t kk = 0; kk + 1 < cpts.size(); ++kk) {
                    const auto& p0 = cpts[kk];
                    const auto& p1 = cpts[kk + 1];
                    if (t >= (float) p0.timeRel && t <= (float) p1.timeRel) {
                        const float f = (t - (float) p0.timeRel)
                                      / std::max(1e-4f, (float)(p1.timeRel - p0.timeRel));
                        v = (float) p0.value + ((float) p1.value - (float) p0.value) * f;
                        break;
                    }
                    if (t > (float) p1.timeRel) v = (float) p1.value;
                }
            } else {
                v = envelopeAt(kind, t);
            }
            const float x = body.getX() + t * body.getWidth();
            const float y2 = body.getBottom() - v * body.getHeight();
            if (k == 0) curve.startNewSubPath(x, y2);
            else        curve.lineTo(x, y2);
        }
        g.setColour(kCurveGold.withAlpha(0.95f));
        g.strokePath(curve, juce::PathStrokeType(1.8f));

        if (kind == TransitionKind::Custom
            && i < m_transitions.size()) {
            const auto& cpts = m_transitions[i].customCurve;
            for (size_t j = 0; j < cpts.size(); ++j) {
                const float px = body.getX() + (float) cpts[j].timeRel * body.getWidth();
                const float py = body.getBottom() - (float) cpts[j].value * body.getHeight();
                const bool isDragged = ((int) i == m_dragXfadeGap && (int) j == m_dragXfadePt);
                g.setColour(isDragged ? juce::Colour(0xFFEF4444) : juce::Colours::white);
                g.fillEllipse(px - 4.0f, py - 4.0f, 8.0f, 8.0f);
                g.setColour(kFrameBlue);
                g.drawEllipse(px - 4.0f, py - 4.0f, 8.0f, 8.0f, 1.5f);
            }
        }

        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillRoundedRectangle(tr.getX() + 1.0f,        tr.getCentreY() - 8.0f, 2.0f, 16.0f, 1.0f);
        g.fillRoundedRectangle(tr.getRight() - 3.0f,    tr.getCentreY() - 8.0f, 2.0f, 16.0f, 1.0f);
    }
}

void StudioTimeline::mouseMove(const juce::MouseEvent& e) {
    m_lastMouseTimelineX = e.getPosition().x;
    m_lastMouseTimelineY = e.getPosition().y;

    const float pps = (float) pixelsPerSec();

    if (e.getPosition().y < m_rulerHeight) {
        for (const auto& tp : m_tempoPoints) {
            const float px = xOfSec(tp.sec);
            if (std::abs((float) e.getPosition().x - px) <= 6.0f) {
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                return;
            }
        }
    }

    if (hasTimeRange() && e.getPosition().y >= m_rulerHeight) {
        const int xL = (int) std::round(xOfSec(m_rangeStartSec));
        const int xR = (int) std::round(xOfSec(m_rangeEndSec));
        if (std::abs(e.getPosition().x - xL) <= 4 || std::abs(e.getPosition().x - xR) <= 4) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    {
        int gapIdx = -1, ptIdx = -1;
        if (hitCrossfadePoint(e.getPosition(), gapIdx, ptIdx) == 0) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void StudioTimeline::mouseDown(const juce::MouseEvent& e) {
    if (e.getPosition().x < kSidebarW
        && e.getPosition().y >= m_rulerHeight
        && e.getPosition().y < getHeight() - 18) {
        handleSidebarMouseDown(e);
        return;
    }

    const float pps = (float) pixelsPerSec();
    const double t  = secAtX(e.getPosition().x);
    spdlog::info("[Studio][MOUSE] tl down x={} y={} L={} R={} shift={} ctrl={} clicks={}",
                 e.getPosition().x, e.getPosition().y,
                 e.mods.isLeftButtonDown(), e.mods.isRightButtonDown(),
                 e.mods.isShiftDown(), e.mods.isCtrlDown(),
                 e.getNumberOfClicks());

    if (!m_phrases.empty()) {
        const int sH = 18;
        const int bandY = getHeight() - sH - 14;
        if (e.getPosition().y >= bandY && e.getPosition().y < bandY + 14) {
            for (const auto& p : m_phrases) {
                const float x1 = xOfSec(p.sec);
                const float x2 = xOfSec(p.sec + p.dur);
                if ((float) e.getPosition().x >= x1 && (float) e.getPosition().x <= x2) {
                    setTimeRange(p.sec, p.sec + p.dur);
                    grabKeyboardFocus();
                    return;
                }
            }
        }
    }

    {
        int gapIdx = -1, ptIdx = -1;
        if (hitCrossfadePoint(e.getPosition(), gapIdx, ptIdx) == 0) {
            if (e.getNumberOfClicks() >= 2) {
                pushUndoSnapshot();
                removeCrossfadePoint(gapIdx, ptIdx);
                repaint();
                return;
            }
            // Un snapshot undo par drag (au mouseDown), pas par pixel.
            pushUndoSnapshot();
            m_dragXfadeGap = gapIdx;
            m_dragXfadePt  = ptIdx;
            m_dragXfadeFrom = e.getPosition();
            const auto& curve = getTransitionCustomCurve(gapIdx);
            if (ptIdx < (int) curve.size()) {
                m_dragXfadeStartTimeRel = curve[(size_t) ptIdx].timeRel;
                m_dragXfadeStartValue   = curve[(size_t) ptIdx].value;
            }
            return;
        }
        if (e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown()) {
            for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
                const auto& a = *m_clips[i];
                const auto& b = *m_clips[i + 1];
                const float ax = xOfSec(a.getStartSec() + a.getLengthSec());
                const float bx = xOfSec(b.getStartSec());
                if (bx <= ax) continue;
                if (e.getPosition().x >= (int) ax && e.getPosition().x <= (int) bx
                    && e.getPosition().y >= m_rulerHeight + 4
                    && e.getPosition().y <= getHeight() - 8) {
                    if (e.mods.isAltDown()) {
                        const double timeRel = ((double) e.getPosition().x - ax) / (bx - ax);
                        const double yMin = m_rulerHeight + 4;
                        const double yMax = getHeight() - 8;
                        const double valueY = 1.0 - ((double) e.getPosition().y - yMin) / (yMax - yMin);
                        pushUndoSnapshot();
                        addCrossfadePoint((int) i, timeRel, juce::jlimit(0.0, 1.0, valueY));
                        repaint();
                    } else {
                        const juce::Rectangle<int> gapRect(
                            (int) ax, m_rulerHeight + 4,
                            std::max(1, (int) (bx - ax)),
                            std::max(1, getHeight() - m_rulerHeight - 12));
                        openTransitionEditor((int) i, gapRect);
                    }
                    return;
                }
            }
        }
    }

    auto hitTempoPoint = [&]() -> int {
        if (e.getPosition().y >= m_rulerHeight) return -1;
        for (size_t i = 0; i < m_tempoPoints.size(); ++i) {
            const float px = xOfSec(m_tempoPoints[i].sec);
            if (std::abs((float) e.getPosition().x - px) <= 6.0f) return (int) i;
        }
        return -1;
    };

    if (e.mods.isPopupMenu() && e.getPosition().y < m_rulerHeight) {
        const int hit = hitTempoPoint();
        const double atSec = snap(t);
        juce::PopupMenu menu;
        if (hit >= 0) {
            menu.addItem(101, juce::String::fromUTF8("Modifier BPM…"));
            menu.addItem(102, juce::String::fromUTF8("Supprimer ce point tempo"));
        } else {
            menu.addItem(100, juce::String::formatted("Ajouter point tempo ici (%.1f BPM)", m_masterBpm));
        }
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this, hit, atSec](int r) {
                if (r == 100) addTempoPoint(atSec, m_masterBpm);
                else if (r == 102) removeTempoPoint(hit);
                else if (r == 101 && hit >= 0 && hit < (int) m_tempoPoints.size()) {
                    auto* alert = new juce::AlertWindow(
                        juce::String::fromUTF8("Modifier BPM"),
                        juce::String::fromUTF8("Nouveau BPM (40..220) :"),
                        juce::AlertWindow::NoIcon);
                    alert->addTextEditor("bpm", juce::String(m_tempoPoints[hit].bpm, 1));
                    alert->addButton("OK",     1);
                    alert->addButton("Annuler", 0);
                    alert->enterModalState(true,
                        juce::ModalCallbackFunction::create(
                            [this, alert, hit](int result) {
                                if (result == 1 && hit < (int) m_tempoPoints.size()) {
                                    const double bpm = alert->getTextEditorContents("bpm").getDoubleValue();
                                    setTempoPoint(hit, m_tempoPoints[hit].sec, bpm);
                                }
                                delete alert;
                            }), false);
                }
            });
        return;
    }

    if (e.mods.isLeftButtonDown() && e.getPosition().y < m_rulerHeight) {
        const int hit = hitTempoPoint();
        if (hit >= 0) {
            m_dragTempoPointIdx = hit;
            m_dragTempoStartSec = m_tempoPoints[hit].sec;
            m_dragTempoStartBpm = m_tempoPoints[hit].bpm;
            m_dragTempoFrom     = e.getPosition();
            return;
        }
    }

    if (e.mods.isLeftButtonDown() && e.getNumberOfClicks() >= 2
        && e.getPosition().y < m_rulerHeight) {
        addTempoPoint(snap(t), m_masterBpm);
        return;
    }

    if (e.mods.isPopupMenu() && e.getPosition().y >= m_rulerHeight) {
        const double atSec = snap(t);
        juce::PopupMenu menu;
        menu.addItem(1, juce::String::fromUTF8("Coller ici"), !m_clipboard.empty());
        menu.addItem(2, juce::String::fromUTF8("Coller au playhead"), !m_clipboard.empty());
        menu.addSeparator();
        const bool hasRange = hasTimeRange();
        menu.addItem(10, juce::String::fromUTF8("Copier la région  (Ctrl+C)"), hasRange);
        menu.addItem(11, juce::String::fromUTF8("Couper la région  (Ctrl+X)"), hasRange);
        menu.addItem(12, juce::String::fromUTF8("Effacer la sélection région (Esc)"), hasRange);
        menu.addItem(13, juce::String::fromUTF8("Zoom sur la sélection (Z)"), hasRange);
        menu.addSeparator();
        menu.addItem(20, juce::String::fromUTF8("Extraire stems sur la région (Demucs)"), hasRange);
        const bool hasClipSel = (m_selected >= 0) || !m_multiSel.empty();
        menu.addItem(21, juce::String::fromUTF8("Extraire stems sur le clip sélectionné"),
                     hasClipSel && !hasRange);
        menu.addSeparator();
        menu.addItem(40, juce::String::fromUTF8("Razor (split aux bords)"), hasRange);
        menu.addItem(41, juce::String::fromUTF8("Inverser la région (R)"), hasRange);
        menu.addItem(42, juce::String::fromUTF8("Bounce région â†’ nouveau clip"), hasRange);
        menu.addItem(43, juce::String::fromUTF8("Audition (P)"), hasRange);
        menu.addItem(44, juce::String::fromUTF8("Loop sur la région (L)"),
                     true, m_rangeLoopEnabled && hasRange);
        menu.addSeparator();
        juce::PopupMenu fadeMenu;
        fadeMenu.addItem(50, juce::String::fromUTF8("Fade-in 0.25 s"));
        fadeMenu.addItem(51, juce::String::fromUTF8("Fade-in 0.5 s"));
        fadeMenu.addItem(52, juce::String::fromUTF8("Fade-in 1 s"));
        fadeMenu.addItem(53, juce::String::fromUTF8("Fade-out 0.25 s"));
        fadeMenu.addItem(54, juce::String::fromUTF8("Fade-out 0.5 s"));
        fadeMenu.addItem(55, juce::String::fromUTF8("Fade-out 1 s"));
        fadeMenu.addItem(56, juce::String::fromUTF8("Fades in+out 0.5 s"));
        menu.addSubMenu(juce::String::fromUTF8("Fades sur région"), fadeMenu, hasRange);
        juce::PopupMenu gainMenu;
        gainMenu.addItem(60, juce::String::fromUTF8("Mute (0%)"));
        gainMenu.addItem(61, juce::String::fromUTF8("Gain 25%"));
        gainMenu.addItem(62, juce::String::fromUTF8("Gain 50%"));
        gainMenu.addItem(63, juce::String::fromUTF8("Gain 75%"));
        gainMenu.addItem(64, juce::String::fromUTF8("Gain 100%"));
        menu.addSubMenu(juce::String::fromUTF8("Gain région"), gainMenu, hasRange);
        juce::PopupMenu stretchMenu;
        stretchMenu.addItem(70, juce::String::fromUTF8("Tempo  -10% (plus lent)"));
        stretchMenu.addItem(71, juce::String::fromUTF8("Tempo  -5%"));
        stretchMenu.addItem(72, juce::String::fromUTF8("Tempo  +5%"));
        stretchMenu.addItem(73, juce::String::fromUTF8("Tempo  +10% (plus rapide)"));
        stretchMenu.addSeparator();
        stretchMenu.addItem(74, juce::String::fromUTF8("Pitch  -2 semitons"));
        stretchMenu.addItem(75, juce::String::fromUTF8("Pitch  -1 semiton"));
        stretchMenu.addItem(76, juce::String::fromUTF8("Pitch  +1 semiton"));
        stretchMenu.addItem(77, juce::String::fromUTF8("Pitch  +2 semitons"));
        menu.addSubMenu(juce::String::fromUTF8("Time-stretch / Pitch (région)"),
                         stretchMenu, hasRange);
        juce::PopupMenu stemRangeMenu;
        stemRangeMenu.addItem(80, juce::String::fromUTF8("Mute Drums sur région"));
        stemRangeMenu.addItem(81, juce::String::fromUTF8("Mute Bass sur région"));
        stemRangeMenu.addItem(82, juce::String::fromUTF8("Mute Other sur région"));
        stemRangeMenu.addItem(83, juce::String::fromUTF8("Mute Vocals sur région"));
        stemRangeMenu.addSeparator();
        stemRangeMenu.addItem(84, juce::String::fromUTF8("Unmute tous les stems sur région"));
        menu.addSubMenu(juce::String::fromUTF8("Stems (région)"), stemRangeMenu, hasRange);
        menu.addSeparator();
        menu.addItem(90, juce::String::fromUTF8("Bounce in place (région â†’ clip plat)"), hasRange);
        menu.addSeparator();
        menu.addItem(30, juce::String::fromUTF8("Tout sélectionner clips (Ctrl+A)"), !m_clips.empty());
        menu.addItem(31, juce::String::fromUTF8("Tout désélectionner"), !m_clips.empty());
        menu.showMenuAsync(juce::PopupMenu::Options(),
            [this, atSec](int r) {
                switch (r) {
                    case 1:  pasteAt(atSec);              break;
                    case 2:  pasteAtPlayhead();           break;
                    case 10: copySelection();             break;
                    case 11: cutSelection();              break;
                    case 12: clearTimeRange();            break;
                    case 13: {
                        if (hasTimeRange()) {
                            const double dur = std::max(0.5, m_rangeEndSec - m_rangeStartSec);
                            const double targetPps = std::max(40.0, (double) getWidth() * 0.85 / dur);
                            setZoom(targetPps / 40.0);
                            setPlayheadSec(m_rangeStartSec);
                        }
                        break;
                    }
                    case 20: extractStemsForSelection(); break;
                    case 21: extractStemsForSelection(); break;
                    case 40: razorAtRange();             break;
                    case 41: reverseRange();             break;
                    case 42: bounceRangeToClip();        break;
                    case 43: auditionRange();            break;
                    case 44: setRangeLoopEnabled(!isRangeLoopEnabled()); break;
                    case 50: applyFadesToRange(0.25, 0.0); break;
                    case 51: applyFadesToRange(0.50, 0.0); break;
                    case 52: applyFadesToRange(1.00, 0.0); break;
                    case 53: applyFadesToRange(0.0, 0.25); break;
                    case 54: applyFadesToRange(0.0, 0.50); break;
                    case 55: applyFadesToRange(0.0, 1.00); break;
                    case 56: applyFadesToRange(0.5, 0.5);  break;
                    case 60: applyGainToRange(0.0f);   break;
                    case 61: applyGainToRange(0.25f);  break;
                    case 62: applyGainToRange(0.50f);  break;
                    case 63: applyGainToRange(0.75f);  break;
                    case 64: applyGainToRange(1.00f);  break;
                    case 70: stretchRange(0.90, 0.0);  break;
                    case 71: stretchRange(0.95, 0.0);  break;
                    case 72: stretchRange(1.05, 0.0);  break;
                    case 73: stretchRange(1.10, 0.0);  break;
                    case 74: stretchRange(1.00, -2.0); break;
                    case 75: stretchRange(1.00, -1.0); break;
                    case 76: stretchRange(1.00,  1.0); break;
                    case 77: stretchRange(1.00,  2.0); break;
                    case 80: setStemMuteOnRange(0, true); break;
                    case 81: setStemMuteOnRange(1, true); break;
                    case 82: setStemMuteOnRange(2, true); break;
                    case 83: setStemMuteOnRange(3, true); break;
                    case 84:
                        setStemMuteOnRange(0, false);
                        setStemMuteOnRange(1, false);
                        setStemMuteOnRange(2, false);
                        setStemMuteOnRange(3, false);
                        break;
                    case 90: bounceInPlace();          break;
                    case 30: selectAll();                break;
                    case 31: deselectAll();              break;
                    default: break;
                }
            });
        return;
    }

    grabKeyboardFocus();
    m_lastMouseTimelineX = e.getPosition().x;
    m_lastMouseTimelineY = e.getPosition().y;

    if (e.getPosition().y < m_rulerHeight) {
        setPlayheadSec(snap(t));
        return;
    }

    if (hasTimeRange() && e.mods.isLeftButtonDown() && !e.mods.isShiftDown()) {
        const int xL = (int) std::round(xOfSec(m_rangeStartSec));
        const int xR = (int) std::round(xOfSec(m_rangeEndSec));
        if (std::abs(e.getPosition().x - xL) <= 4) {
            m_rangeEdgeDrag = RangeEdgeDrag::Left;
            return;
        }
        if (std::abs(e.getPosition().x - xR) <= 4) {
            m_rangeEdgeDrag = RangeEdgeDrag::Right;
            return;
        }
    }

    if (e.mods.isLeftButtonDown() && e.mods.isShiftDown()) {
        spdlog::info("[Studio][RANGE] start drag at sec={:.3f}", t);
        m_rangeDragActive = true;
        m_rangeDragAnchorSec = snap(t);
        m_rangeStartSec = m_rangeDragAnchorSec;
        m_rangeEndSec   = m_rangeDragAnchorSec;
        repaint();
        return;
    }

    if (e.mods.isLeftButtonDown() && !e.mods.isShiftDown()) {
        m_rubbering = true;
        m_rubber.setBounds(e.getPosition().x, e.getPosition().y, 0, 0);
        if (hasTimeRange()) clearTimeRange();
    }
    setPlayheadSec(snap(t));
}

void StudioTimeline::mouseDrag(const juce::MouseEvent& e) {
    m_lastMouseTimelineX = e.getPosition().x;
    m_lastMouseTimelineY = e.getPosition().y;

    if (m_draggingLaneFader >= 0) {
        const int dy = e.getPosition().y - m_draggingLaneFaderFrom.y;
        // 100 px = 24 dB de variation (sensibilité raisonnable).
        const float deltaDb = -(float) dy * (24.0f / 100.0f);
        const float newDb = juce::jlimit(-60.0f, 6.0f,
                                         m_draggingLaneFaderStartDb + deltaDb);
        setLaneVolumeDb(m_draggingLaneFader, newDb);
        return;
    }

    if (m_dragXfadeGap >= 0 && m_dragXfadePt >= 0) {
        const float pps = (float) pixelsPerSec();
        if (m_dragXfadeGap >= (int) m_clips.size() - 1) { m_dragXfadeGap = -1; m_dragXfadePt = -1; return; }
        const auto& a = *m_clips[(size_t) m_dragXfadeGap];
        const auto& b = *m_clips[(size_t) m_dragXfadeGap + 1];
        const float ax = xOfSec(a.getStartSec() + a.getLengthSec());
        const float bx = xOfSec(b.getStartSec());
        if (bx > ax) {
            const double yMin = m_rulerHeight + 4;
            const double yMax = getHeight() - 8;
            const double timeRel = juce::jlimit(0.0, 1.0,
                ((double) e.getPosition().x - ax) / (bx - ax));
            const double value = juce::jlimit(0.0, 1.0,
                1.0 - ((double) e.getPosition().y - yMin) / (yMax - yMin));
            setCrossfadePoint(m_dragXfadeGap, m_dragXfadePt, timeRel, value);
        }
        return;
    }

    if (m_dragTempoPointIdx >= 0
        && m_dragTempoPointIdx < (int) m_tempoPoints.size()) {
        const float pps = (float) pixelsPerSec();
        const int dx = e.getPosition().x - m_dragTempoFrom.x;
        const int dy = m_dragTempoFrom.y - e.getPosition().y; // up = + bpm
        const double newSec = std::max(0.0, m_dragTempoStartSec + (double) dx / pps);
        const double newBpm = juce::jlimit(40.0, 220.0,
                                            m_dragTempoStartBpm + (double) dy * 0.5);
        setTempoPoint(m_dragTempoPointIdx, newSec, newBpm);
        return;
    }

    if (m_rangeEdgeDrag != RangeEdgeDrag::None) {
        const double t2 = secAtX(e.getPosition().x);
        const double snapped = snap(t2);
        if (m_rangeEdgeDrag == RangeEdgeDrag::Left) {
            m_rangeStartSec = std::min(snapped, m_rangeEndSec - 0.05);
        } else {
            m_rangeEndSec = std::max(snapped, m_rangeStartSec + 0.05);
        }
        repaint();
        return;
    }

    if (m_rangeDragActive) {
        const double t = secAtX(e.getPosition().x);
        const double snapped = snap(t);
        if (snapped < m_rangeDragAnchorSec) {
            m_rangeStartSec = snapped;
            m_rangeEndSec   = m_rangeDragAnchorSec;
        } else {
            m_rangeStartSec = m_rangeDragAnchorSec;
            m_rangeEndSec   = snapped;
        }
        repaint();
        return;
    }

    if (m_rubbering) {
        m_rubber = juce::Rectangle<int>::leftTopRightBottom(
            std::min(m_rubber.getX(), e.getPosition().x),
            std::min(m_rubber.getY(), e.getPosition().y),
            std::max(m_rubber.getX() + m_rubber.getWidth(), e.getPosition().x),
            std::max(m_rubber.getY() + m_rubber.getHeight(), e.getPosition().y));
        rubberBandSelect(m_rubber);
        repaint(m_rubber.expanded(2));
    }
}

void StudioTimeline::mouseUp(const juce::MouseEvent&) {
    if (m_draggingLaneFader >= 0) {
        m_draggingLaneFader = -1;
        repaint();
    }

    if (m_dragXfadeGap >= 0) {
        m_dragXfadeGap = -1;
        m_dragXfadePt  = -1;
        repaint();
    }

    if (m_dragTempoPointIdx >= 0) {
        m_dragTempoPointIdx = -1;
        std::sort(m_tempoPoints.begin(), m_tempoPoints.end(),
                  [](const TempoPoint& a, const TempoPoint& b){ return a.sec < b.sec; });
        repaint();
    }

    if (m_rangeDragActive) {
        m_rangeDragActive = false;
        if (std::abs(m_rangeEndSec - m_rangeStartSec) < 0.05) {
            clearTimeRange();
        }
        return;
    }
    if (m_rangeEdgeDrag != RangeEdgeDrag::None) {
        m_rangeEdgeDrag = RangeEdgeDrag::None;
        return;
    }

    const auto oldRubber = m_rubber;
    m_rubbering = false;
    m_rubber = juce::Rectangle<int>();
    repaint(oldRubber.expanded(2));
}

bool StudioTimeline::keyPressed(const juce::KeyPress& key) {
    using K = juce::KeyPress;
    const auto& mods = key.getModifiers();
    const bool ctrl = mods.isCommandDown();
    spdlog::info("[Studio][KEY] code={} char='{}' ctrl={} shift={} alt={} hasFocus={}",
                 key.getKeyCode(),
                 (char) (key.getKeyCode() < 128 ? key.getKeyCode() : '?'),
                 ctrl, mods.isShiftDown(), mods.isAltDown(),
                 hasKeyboardFocus(true));

    if (ctrl && key.getKeyCode() == 'C') { copySelection();   return true; }
    if (ctrl && key.getKeyCode() == 'X') { cutSelection();    return true; }
    if (ctrl && key.getKeyCode() == 'V') { pasteAtMouse();    return true; }
    if (ctrl && key.getKeyCode() == 'A') { selectAll();       return true; }
    if (ctrl && key.getKeyCode() == 'B') { splitAtPlayhead(); return true; }
    if (ctrl && key.getKeyCode() == 'D') {
        if (m_selected >= 0) duplicateClip(m_selected);
        return true;
    }
    if (ctrl && key.getKeyCode() == 'Z') {
        if (mods.isShiftDown()) redo(); else undo();
        return true;
    }
    if (ctrl && key.getKeyCode() == 'Y') { redo(); return true; }
    if (ctrl && key.getKeyCode() == 'G') {
        if (mods.isShiftDown()) ungroupSelectedClips();
        else                    groupSelectedClips();
        return true;
    }
    if (key == K::deleteKey || key == K::backspaceKey) {
        deleteSelection();
        return true;
    }

    if (!ctrl && key.getKeyCode() == 'I') {
        const double e = (hasTimeRange() && m_rangeEndSec > m_playhead)
                             ? m_rangeEndSec : m_playhead + 1.0;
        setTimeRange(m_playhead, e);
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'O') {
        const double s = (hasTimeRange() && m_rangeStartSec < m_playhead)
                             ? m_rangeStartSec : std::max(0.0, m_playhead - 1.0);
        setTimeRange(s, m_playhead);
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'Z') {
        if (hasTimeRange()) {
            const double dur = std::max(0.5, m_rangeEndSec - m_rangeStartSec);
            const double targetPps = std::max(40.0, (double) getWidth() * 0.85 / dur);
            setZoom(targetPps / 40.0);
            setPlayheadSec(m_rangeStartSec);
        } else {
            fitView();
        }
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'P') { auditionRange(); return true; }
    if (!ctrl && key.getKeyCode() == 'L') {
        setRangeLoopEnabled(!isRangeLoopEnabled());
        return true;
    }
    if (!ctrl && key.getKeyCode() == 'R') {
        if (hasTimeRange()) reverseRange();
        return true;
    }
    if (key == K::escapeKey) {
        if (hasTimeRange()) { clearTimeRange(); return true; }
        deselectAll();
        return true;
    }
    return false;
}

void StudioTimeline::mouseWheelMove(const juce::MouseEvent& e,
                                     const juce::MouseWheelDetails& d) {
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        const int    mouseX = e.getPosition().x;
        const double tMouse = secAtX(mouseX);
        const double factor = (d.deltaY > 0) ? 1.12 : (1.0 / 1.12);
        setZoom(juce::jlimit(0.05, 200.0, m_zoom * factor));

        // Reposition le viewport pour que tMouse reste sous la souris.
        if (auto* parent = getParentComponent()) {
            if (auto* vp = dynamic_cast<juce::Viewport*>(parent)) {
                const int target = (int) std::round(xOfSec(tMouse)) - mouseX
                                 + vp->getViewPositionX();
                vp->setViewPosition(std::max(0, target), vp->getViewPositionY());
            }
        }
    } else {
        if (auto* parent = getParentComponent())
            parent->mouseWheelMove(e, d);
    }
}

bool StudioTimeline::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3"  || ext == ".flac"
         || ext == ".aif" || ext == ".aiff" || ext == ".m4a"
         || ext == ".ogg" || ext == ".opus") return true;
    }
    return false;
}

void StudioTimeline::filesDropped(const juce::StringArray& paths, int x, int /*y*/) {
    if (paths.isEmpty()) return;
    pushUndoSnapshot();

    static juce::AudioFormatManager s_dropFmt;
    static bool s_dropFmtInited = false;
    if (!s_dropFmtInited) { s_dropFmt.registerBasicFormats(); s_dropFmtInited = true; }

    const float pps   = (float) pixelsPerSec();
    const int   tlX   = std::max(0, x - kSidebarW);
    double dropSec    = std::max(0.0, (double) tlX / std::max(1.0f, pps));
    dropSec           = snap(dropSec);

    int firstAdded = -1;
    for (const auto& path : paths) {
        juce::File f { path };
        if (!f.existsAsFile()) continue;
        std::unique_ptr<juce::AudioFormatReader> reader(s_dropFmt.createReaderFor(f));
        if (!reader) continue;

        Models::Track t;
        t.id         = (int64_t) juce::Time::currentTimeMillis() + (int64_t) m_clips.size();
        t.filePath   = f.getFullPathName().toStdString();
        t.title      = f.getFileNameWithoutExtension().toStdString();
        t.duration   = reader->lengthInSamples / std::max(1.0, reader->sampleRate);
        t.sampleRate = (int) reader->sampleRate;
        t.channels   = (int) reader->numChannels;
        t.fileFormat = f.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();
        t.fileSize   = f.getSize();

        const int idx = addClip(t);
        if (idx >= 0) {
            if (firstAdded < 0) firstAdded = idx;
            if (auto* c = getClip(idx)) {
                c->setStartSec(dropSec);
                dropSec += c->getLengthSec() + 0.05;   // 50 ms gap entre fichiers
            }
        }
    }
    layoutClips();
    repaint();
    m_listeners.call([&paths](Listener& l){ l.filesDropped(paths); });
}

void StudioTimeline::setTimeRange(double s, double e) {
    m_rangeStartSec = std::max(0.0, std::min(s, e));
    m_rangeEndSec   = std::max(s, e);
    // Sync atomics pour TimelineAudioSource (audio thread).
    m_audioLoopStart.store(m_rangeStartSec);
    m_audioLoopEnd  .store(m_rangeEndSec);
    repaint();
}

void StudioTimeline::clearTimeRange() {
    if (m_rangeStartSec < 0.0 && m_rangeEndSec < 0.0) return;
    m_rangeStartSec = -1.0;
    m_rangeEndSec   = -1.0;
    m_rangeDragActive = false;
    // Sync atomics : le loop devient invalide.
    m_audioLoopStart.store(-1.0);
    m_audioLoopEnd  .store(-1.0);
    m_audioLoopOn   .store(false);
    repaint();
}

void StudioTimeline::razorAtRange() {
    if (!hasTimeRange()) return;
    const double rs = m_rangeStartSec;
    const double re = m_rangeEndSec;
    auto splitOnce = [&](double atSec) {
        std::vector<std::unique_ptr<StudioClip>> rebuilt;
        rebuilt.reserve(m_clips.size() + 4);
        for (auto& cp : m_clips) {
            const double cs = cp->getStartSec();
            const double ce = cs + cp->getLengthSec();
            if (atSec <= cs + 0.01 || atSec >= ce - 0.01) {
                rebuilt.push_back(std::move(cp));
                continue;
            }
            const double aIn0 = cp->getAudioInSec();
            const double leftLen  = atSec - cs;
            const double rightLen = ce - atSec;
            auto left = std::make_unique<StudioClip>(cp->getTrack(), *this);
            left->setAudioRange(aIn0, aIn0 + leftLen);
            left->setStartSec(cs);
            addAndMakeVisible(*left);
            rebuilt.push_back(std::move(left));

            auto right = std::make_unique<StudioClip>(cp->getTrack(), *this);
            right->setAudioRange(aIn0 + leftLen, aIn0 + leftLen + rightLen);
            right->setStartSec(atSec);
            addAndMakeVisible(*right);
            rebuilt.push_back(std::move(right));
        }
        m_clips = std::move(rebuilt);
    };
    splitOnce(rs);
    splitOnce(re);
    for (auto& cp : m_clips) {
        auto* raw = cp.get();
        wireClipCallback(*raw);
    }
    for (int k_b3 = 0; k_b3 < (int) m_clips.size(); ++k_b3) enqueueClipPrep(k_b3);
    layoutClips();
    repaint();
}

void StudioTimeline::reverseRange() {
    if (!hasTimeRange()) return;
    razorAtRange();
    for (auto& cp : m_clips) {
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        if (cs >= m_rangeStartSec - 0.001 && ce <= m_rangeEndSec + 0.001) {
            cp->setReversed(!cp->isReversed());
        }
    }
    repaint();
}

void StudioTimeline::applyFadesToRange(double fadeInSec, double fadeOutSec) {
    if (!hasTimeRange()) return;
    razorAtRange();
    for (auto& cp : m_clips) {
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        if (cs >= m_rangeStartSec - 0.001 && ce <= m_rangeEndSec + 0.001) {
            const double maxFade = (ce - cs) * 0.5;
            cp->setFadeIn (std::min(fadeInSec, maxFade));
            cp->setFadeOut(std::min(fadeOutSec, maxFade));
        }
    }
    repaint();
}

void StudioTimeline::applyGainToRange(float gain) {
    if (!hasTimeRange()) return;
    razorAtRange();
    const float g = juce::jlimit(0.0f, 4.0f, gain);
    for (auto& cp : m_clips) {
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        if (cs >= m_rangeStartSec - 0.001 && ce <= m_rangeEndSec + 0.001) {
            cp->setVolume(std::min(1.0f, g));
        }
    }
    repaint();
}

void StudioTimeline::bounceRangeToClip() {
    if (!hasTimeRange()) return;
    const double rs = m_rangeStartSec;
    const double re = m_rangeEndSec;
    const double sr = m_playbackSr;
    const int    ch = 2;
    const int64_t totalFrames = (int64_t) std::ceil((re - rs) * sr);
    if (totalFrames <= 0) return;

    juce::AudioBuffer<float> mix(ch, (int) totalFrames);
    mix.clear();
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();

    for (auto& cp : m_clips) {
        if (cp->isMuted()) continue;
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        const double s  = std::max(rs, cs);
        const double e  = std::min(re, ce);
        if (e <= s + 0.01) continue;

        juce::File f{ juce::String::fromUTF8(cp->getTrack().filePath.c_str()) };
        if (!f.existsAsFile()) continue;
        std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
        if (!reader) continue;

        const double aIn  = cp->getAudioInSec() + (s - cs);
        const double aOut = cp->getAudioInSec() + (e - cs);
        const int64_t srcStart = (int64_t) (aIn  * reader->sampleRate);
        const int64_t srcEnd   = (int64_t) (aOut * reader->sampleRate);
        const int64_t srcLen   = std::max<int64_t>(0, srcEnd - srcStart);
        if (srcLen <= 0) continue;

        juce::AudioBuffer<float> src((int) reader->numChannels, (int) srcLen);
        reader->read(&src, 0, (int) srcLen, srcStart, true, true);
        const int64_t mixStart = (int64_t) ((s - rs) * sr);
        const int64_t writable = std::min<int64_t>(srcLen, totalFrames - mixStart);
        if (writable <= 0) continue;
        const float vol = cp->getVolume();
        for (int outCh = 0; outCh < ch; ++outCh) {
            const int sc = std::min(outCh, src.getNumChannels() - 1);
            mix.addFrom(outCh, (int) mixStart, src, sc, 0, (int) writable, vol);
        }
    }

    const auto outDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("BeatMate_bounce");
    outDir.createDirectory();
    const auto outFile = outDir.getChildFile(
        "bounce_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(outFile.createOutputStream());
        if (!out) return;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(out.get(), sr, (unsigned int) ch, 16, {}, 0));
        if (!w) return;
        out.release();
        w->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples());
    }

    Models::Track t;
    t.id = (int64_t) juce::Time::currentTimeMillis();
    t.title = ("Bounce " + outFile.getFileNameWithoutExtension()).toStdString();
    t.filePath = outFile.getFullPathName().toStdString();
    t.duration = (re - rs);
    t.bpm = m_masterBpm;

    auto clip = std::make_unique<StudioClip>(t, *this);
    clip->setAudioRange(0.0, re - rs);
    clip->setStartSec(re);
    auto* raw = clip.get();
    wireClipCallback(*raw);
    addAndMakeVisible(*raw);
    m_clips.push_back(std::move(clip));
    enqueueClipPrep((int) m_clips.size() - 1);
    layoutClips();
    repaint();
}

void StudioTimeline::setRangeLoopEnabled(bool b) {
    m_rangeLoopEnabled = b;
    // Sync atomic pour TimelineAudioSource.
    m_audioLoopOn.store(b);
}

void StudioTimeline::auditionRange() {
    if (!hasTimeRange()) return;
    setPlayheadSec(m_rangeStartSec);
    setRangeLoopEnabled(true);
    if (!m_playing.load()) play();
}

void StudioTimeline::groupSelectedClips() {
    static int s_nextGroupId = 1;
    std::vector<int> indices;
    if (m_selected >= 0 && m_selected < (int) m_clips.size())
        indices.push_back(m_selected);
    indices.insert(indices.end(), m_multiSel.begin(), m_multiSel.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.size() < 2) return;
    const int gid = s_nextGroupId++;
    for (int i : indices) {
        if (i >= 0 && i < (int) m_clips.size()) m_clips[(size_t) i]->setGroupId(gid);
    }
    spdlog::info("[Studio] groupSelectedClips: {} clips grouped under id={}",
                 (int) indices.size(), gid);
}

void StudioTimeline::ungroupSelectedClips() {
    std::vector<int> indices;
    if (m_selected >= 0 && m_selected < (int) m_clips.size())
        indices.push_back(m_selected);
    indices.insert(indices.end(), m_multiSel.begin(), m_multiSel.end());
    if (indices.empty()) return;
    std::set<int> gids;
    for (int i : indices) {
        if (i >= 0 && i < (int) m_clips.size()) {
            const int g = m_clips[(size_t) i]->getGroupId();
            if (g > 0) gids.insert(g);
        }
    }
    if (gids.empty()) return;
    for (auto& cp : m_clips) {
        if (gids.count(cp->getGroupId()) > 0) cp->setGroupId(0);
    }
}

void StudioTimeline::propagateGroupMove(int groupId, double deltaSec, StudioClip* originator) {
    if (groupId <= 0 || std::abs(deltaSec) < 1e-6) return;
    // Guard re-entrance (thread_local) : evite la cascade recursive en drag multi-select de groupe.
    static thread_local bool s_groupMoveInFlight = false;
    if (s_groupMoveInFlight) return;
    s_groupMoveInFlight = true;
    const float pps = (float) pixelsPerSec();
    for (auto& cp : m_clips) {
        if (cp.get() == originator) continue;
        if (cp->getGroupId() != groupId) continue;
        const double ns = std::max(0.0, cp->getStartSec() + deltaSec);
        cp->setStartSecQuiet(ns);
        const int x = (int) std::round(ns * pps);
        cp->setBounds(x, cp->getY(), cp->getWidth(), cp->getHeight());
    }
    s_groupMoveInFlight = false;
}

void StudioTimeline::setStemMuteOnRange(int stemIndex, bool muted) {
    if (!hasTimeRange()) return;
    if (stemIndex < 0 || stemIndex > 3) return;
    razorAtRange();
    for (auto& cp : m_clips) {
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        if (cs >= m_rangeStartSec - 0.001 && ce <= m_rangeEndSec + 0.001) {
            if (cp->hasStems()) cp->setStemMute(stemIndex, muted);
        }
    }
    repaint();
}

void StudioTimeline::bounceInPlace() {
    if (!hasTimeRange()) return;
    const double rs = m_rangeStartSec;
    const double re = m_rangeEndSec;
    const double sr = m_playbackSr;
    const int    ch = 2;
    const int64_t totalFrames = (int64_t) std::ceil((re - rs) * sr);
    if (totalFrames <= 0) return;

    juce::AudioBuffer<float> mix(ch, (int) totalFrames);
    mix.clear();
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();

    for (auto& cp : m_clips) {
        if (cp->isMuted()) continue;
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        const double s  = std::max(rs, cs);
        const double e  = std::min(re, ce);
        if (e <= s + 0.01) continue;

        juce::AudioBuffer<float> src;
        double srcSr = sr;

        if (cp->hasStems()) {
            auto* st = cp->getStems();
            const int n = std::max({ st->drums.getNumSamples(), st->bass.getNumSamples(),
                                      st->other.getNumSamples(), st->vocals.getNumSamples() });
            const int chs = std::max({ st->drums.getNumChannels(), st->bass.getNumChannels(),
                                        st->other.getNumChannels(), st->vocals.getNumChannels() });
            if (n > 0 && chs > 0) {
                src.setSize(chs, n, false, true, false);
                src.clear();
                auto addIfNotMuted = [&](const juce::AudioBuffer<float>& sb, int idx) {
                    if (cp->isStemMuted(idx) || sb.getNumSamples() == 0) return;
                    const int N = std::min(n, sb.getNumSamples());
                    for (int c = 0; c < chs; ++c) {
                        const int sc = std::min(c, sb.getNumChannels() - 1);
                        if (sc < 0) continue;
                        src.addFrom(c, 0, sb, sc, 0, N);
                    }
                };
                addIfNotMuted(st->drums,  0);
                addIfNotMuted(st->bass,   1);
                addIfNotMuted(st->other,  2);
                addIfNotMuted(st->vocals, 3);
                srcSr = st->sampleRate;
            }
            const double inSecRel  = s - cs;
            const double outSecRel = e - cs;
            const int srcStart = (int) (inSecRel  * srcSr);
            const int srcEnd   = (int) (outSecRel * srcSr);
            const int srcLen   = std::max(0, srcEnd - srcStart);
            if (srcLen <= 0) continue;
            juce::AudioBuffer<float> trimmed(src.getNumChannels(), srcLen);
            for (int c = 0; c < src.getNumChannels(); ++c)
                trimmed.copyFrom(c, 0, src, c, srcStart, srcLen);
            src = std::move(trimmed);
        } else {
            juce::File f{ juce::String::fromUTF8(cp->getTrack().filePath.c_str()) };
            if (!f.existsAsFile()) continue;
            std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
            if (!reader) continue;
            const double aIn  = cp->getAudioInSec() + (s - cs);
            const double aOut = cp->getAudioInSec() + (e - cs);
            const int64_t srcStart = (int64_t) (aIn  * reader->sampleRate);
            const int64_t srcEnd   = (int64_t) (aOut * reader->sampleRate);
            const int64_t srcLen   = std::max<int64_t>(0, srcEnd - srcStart);
            if (srcLen <= 0) continue;
            src.setSize((int) reader->numChannels, (int) srcLen, false, true, false);
            reader->read(&src, 0, (int) srcLen, srcStart, true, true);
            srcSr = reader->sampleRate;
        }

        const int64_t mixStart = (int64_t) ((s - rs) * sr);
        const int64_t writable = std::min<int64_t>((int64_t) src.getNumSamples(),
                                                    totalFrames - mixStart);
        if (writable <= 0) continue;
        const float vol = cp->getVolume();
        for (int outCh = 0; outCh < ch; ++outCh) {
            const int sc = std::min(outCh, src.getNumChannels() - 1);
            mix.addFrom(outCh, (int) mixStart, src, sc, 0, (int) writable, vol);
        }
    }

    const auto outDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("BeatMate_bounce");
    outDir.createDirectory();
    const auto outFile = outDir.getChildFile(
        "bip_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(outFile.createOutputStream());
        if (!out) return;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(out.get(), sr, (unsigned int) ch, 16, {}, 0));
        if (!w) return;
        out.release();
        w->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples());
    }

    cutSelection();

    Models::Track t;
    t.id = (int64_t) juce::Time::currentTimeMillis();
    t.title = "Bounce in place";
    t.filePath = outFile.getFullPathName().toStdString();
    t.duration = (re - rs);
    t.bpm = m_masterBpm;

    auto clip = std::make_unique<StudioClip>(t, *this);
    clip->setAudioRange(0.0, t.duration);
    clip->setStartSec(rs);
    auto* raw = clip.get();
    wireClipCallback(*raw);
    addAndMakeVisible(*raw);
    m_clips.push_back(std::move(clip));
    enqueueClipPrep((int) m_clips.size() - 1);
    layoutClips();
    repaint();
}

void StudioTimeline::stretchRange(double tempoRatio, double pitchSemitones) {
    if (!hasTimeRange()) return;
    const double rs = m_rangeStartSec;
    const double re = m_rangeEndSec;
    const double sr = m_playbackSr;
    const int    ch = 2;
    const int64_t inFrames = (int64_t) std::ceil((re - rs) * sr);
    if (inFrames <= 0) return;

    juce::AudioBuffer<float> mix(ch, (int) inFrames);
    mix.clear();
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();

    for (auto& cp : m_clips) {
        if (cp->isMuted()) continue;
        const double cs = cp->getStartSec();
        const double ce = cs + cp->getLengthSec();
        const double s  = std::max(rs, cs);
        const double e  = std::min(re, ce);
        if (e <= s + 0.01) continue;

        juce::File f{ juce::String::fromUTF8(cp->getTrack().filePath.c_str()) };
        if (!f.existsAsFile()) continue;
        std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
        if (!reader) continue;

        const double aIn  = cp->getAudioInSec() + (s - cs);
        const double aOut = cp->getAudioInSec() + (e - cs);
        const int64_t srcStart = (int64_t) (aIn  * reader->sampleRate);
        const int64_t srcEnd   = (int64_t) (aOut * reader->sampleRate);
        const int64_t srcLen   = std::max<int64_t>(0, srcEnd - srcStart);
        if (srcLen <= 0) continue;

        juce::AudioBuffer<float> src((int) reader->numChannels, (int) srcLen);
        reader->read(&src, 0, (int) srcLen, srcStart, true, true);
        const int64_t mixStart = (int64_t) ((s - rs) * sr);
        const int64_t writable = std::min<int64_t>(srcLen, inFrames - mixStart);
        if (writable <= 0) continue;
        const float vol = cp->getVolume();
        for (int outCh = 0; outCh < ch; ++outCh) {
            const int sc = std::min(outCh, src.getNumChannels() - 1);
            mix.addFrom(outCh, (int) mixStart, src, sc, 0, (int) writable, vol);
        }
    }

    soundtouch::SoundTouch st;
    st.setSampleRate((unsigned int) sr);
    st.setChannels(ch);
    st.setSetting(SETTING_USE_AA_FILTER, 1);
    st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
    st.setSetting(SETTING_USE_QUICKSEEK, 0);
    st.setSetting(SETTING_SEQUENCE_MS,    60);
    st.setSetting(SETTING_SEEKWINDOW_MS,  25);
    st.setSetting(SETTING_OVERLAP_MS,     12);
    st.setTempo(tempoRatio > 0.01 ? tempoRatio : 1.0);
    st.setPitchSemiTones((float) pitchSemitones);

    std::vector<float> interleaved((size_t) inFrames * (size_t) ch);
    for (int i = 0; i < inFrames; ++i)
        for (int c = 0; c < ch; ++c)
            interleaved[(size_t) i * (size_t) ch + (size_t) c] = mix.getSample(c, i);

    st.putSamples(interleaved.data(), (unsigned int) inFrames);
    st.flush();

    std::vector<float> outBuf;
    outBuf.reserve(interleaved.size() * 2);
    constexpr unsigned int CHUNK = 4096;
    std::vector<float> tmp((size_t) CHUNK * (size_t) ch);
    while (true) {
        unsigned int got = st.receiveSamples(tmp.data(), CHUNK);
        if (got == 0) break;
        outBuf.insert(outBuf.end(), tmp.begin(), tmp.begin() + (size_t) got * (size_t) ch);
    }
    if (outBuf.empty()) return;

    const int outFrames = (int) (outBuf.size() / (size_t) ch);
    juce::AudioBuffer<float> outMix(ch, outFrames);
    for (int i = 0; i < outFrames; ++i)
        for (int c = 0; c < ch; ++c)
            outMix.setSample(c, i, outBuf[(size_t) i * (size_t) ch + (size_t) c]);

    const auto outDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("BeatMate_stretch");
    outDir.createDirectory();
    const auto outFile = outDir.getChildFile(
        "stretch_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out(outFile.createOutputStream());
        if (!out) return;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wav.createWriterFor(out.get(), sr, (unsigned int) ch, 16, {}, 0));
        if (!w) return;
        out.release();
        w->writeFromAudioSampleBuffer(outMix, 0, outFrames);
    }

    cutSelection();

    Models::Track t;
    t.id = (int64_t) juce::Time::currentTimeMillis();
    t.title = "Stretch " + outFile.getFileNameWithoutExtension().toStdString();
    t.filePath = outFile.getFullPathName().toStdString();
    t.duration = (double) outFrames / sr;
    t.bpm = m_masterBpm * tempoRatio;

    auto clip = std::make_unique<StudioClip>(t, *this);
    clip->setAudioRange(0.0, t.duration);
    clip->setStartSec(rs);
    auto* raw = clip.get();
    wireClipCallback(*raw);
    addAndMakeVisible(*raw);
    m_clips.push_back(std::move(clip));
    enqueueClipPrep((int) m_clips.size() - 1);
    layoutClips();
    repaint();
}

class StudioTimeline::ProgressOverlay : public juce::Component, private juce::Timer {
public:
    ProgressOverlay() {
        setInterceptsMouseClicks(true, false);
        setOpaque(false);
        startTimerHz(20);
    }
    ~ProgressOverlay() override { stopTimer(); }

    void setTitle(const juce::String& t) { m_title = t; repaint(); }
    void setProgress(float pct, const juce::String& phase) {
        m_pct = juce::jlimit(0.0f, 1.0f, pct);
        m_phase = phase;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.65f));
        g.fillRect(b);

        const float W = std::min(540.0f, b.getWidth() * 0.75f);
        const float H = 150.0f;
        auto card = juce::Rectangle<float>(
            (b.getWidth() - W) * 0.5f,
            (b.getHeight() - H) * 0.5f, W, H);

        g.setColour(juce::Colour(0xFF111319));
        g.fillRoundedRectangle(card, 10.0f);
        g.setColour(juce::Colour(0xFFE3A942).withAlpha(0.85f));
        g.drawRoundedRectangle(card, 10.0f, 1.4f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(m_title, card.reduced(20.0f, 14.0f).withHeight(22.0f),
                   juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xFFCBD5E1));
        g.setFont(juce::Font(11.5f));
        const auto phaseR = card.reduced(20.0f, 8.0f).withY(card.getY() + 42.0f).withHeight(18.0f);
        g.drawText(m_phase, phaseR, juce::Justification::centredLeft);

        const auto bar = juce::Rectangle<float>(
            card.getX() + 20.0f, card.getY() + 78.0f,
            card.getWidth() - 40.0f, 10.0f);
        g.setColour(juce::Colour(0xFF1F2937));
        g.fillRoundedRectangle(bar, 4.0f);
        g.setColour(juce::Colour(0xFFE3A942));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * m_pct), 4.0f);

        g.setColour(juce::Colour(0xFFFCD34D));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        const auto pctR = bar.translated(0, 18.0f).withHeight(20.0f);
        g.drawText(juce::String((int) std::round(m_pct * 100.0f)) + " %",
                   pctR, juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent&) override {}

private:
    void timerCallback() override { repaint(); }
    juce::String m_title { juce::String::fromUTF8("Traitement…") };
    juce::String m_phase { juce::String::fromUTF8("Préparation…") };
    float        m_pct   { 0.0f };
};

void StudioTimeline::showProgress(const juce::String& title) {
    if (!m_progress) {
        m_progress = std::make_unique<ProgressOverlay>();
        addAndMakeVisible(m_progress.get());
    }
    m_progress->setTitle(title);
    m_progress->setProgress(0.0f, juce::String::fromUTF8("Préparation…"));
    m_progress->setBounds(getLocalBounds());
    m_progress->toFront(false);
    m_progress->setVisible(true);
}

void StudioTimeline::updateProgress(float pct, const juce::String& phase) {
    if (m_progress) m_progress->setProgress(pct, phase);
}

void StudioTimeline::hideProgress() {
    if (m_progress) m_progress->setVisible(false);
}

void StudioTimeline::layoutProgressOverlay() {
    if (m_progress && m_progress->isVisible())
        m_progress->setBounds(getLocalBounds());
}

int StudioTimeline::addTempoPoint(double sec, double bpm) {
    TempoPoint p { std::max(0.0, sec), juce::jlimit(40.0, 220.0, bpm) };
    m_tempoPoints.push_back(p);
    std::sort(m_tempoPoints.begin(), m_tempoPoints.end(),
              [](const TempoPoint& a, const TempoPoint& b){ return a.sec < b.sec; });
    repaint();
    return (int) m_tempoPoints.size() - 1;
}

void StudioTimeline::removeTempoPoint(int index) {
    if (index < 0 || index >= (int) m_tempoPoints.size()) return;
    m_tempoPoints.erase(m_tempoPoints.begin() + index);
    repaint();
}

void StudioTimeline::setTempoPoint(int index, double sec, double bpm) {
    if (index < 0 || index >= (int) m_tempoPoints.size()) return;
    m_tempoPoints[index].sec = std::max(0.0, sec);
    m_tempoPoints[index].bpm = juce::jlimit(40.0, 220.0, bpm);
    repaint();
}

double StudioTimeline::getBpmAt(double sec) const {
    if (m_tempoPoints.empty()) return m_masterBpm;
    // Stepped interpolation : on prend le BPM du dernier point â‰¤ sec.
    double bpm = m_tempoPoints.front().bpm;
    for (const auto& p : m_tempoPoints) {
        if (p.sec <= sec) bpm = p.bpm; else break;
    }
    return bpm;
}

void StudioTimeline::setTransitionCustomCurve(int gapIndex,
                                               std::vector<CrossfadeCurvePoint> curve) {
    if (gapIndex < 0) return;
    if ((int) m_transitions.size() <= gapIndex)
        m_transitions.resize((size_t) gapIndex + 1);
    m_transitions[gapIndex].kind = TransitionKind::Custom;
    m_transitions[gapIndex].customCurve = std::move(curve);
    repaint();
}

const std::vector<CrossfadeCurvePoint>&
StudioTimeline::getTransitionCustomCurve(int gapIndex) const {
    static const std::vector<CrossfadeCurvePoint> empty;
    if (gapIndex < 0 || gapIndex >= (int) m_transitions.size()) return empty;
    return m_transitions[(size_t) gapIndex].customCurve;
}

int StudioTimeline::addCrossfadePoint(int gapIndex, double timeRel, double value) {
    if (gapIndex < 0) return -1;
    if ((int) m_transitions.size() <= gapIndex)
        m_transitions.resize((size_t) gapIndex + 1);
    auto& tr = m_transitions[(size_t) gapIndex];
    tr.kind = TransitionKind::Custom;
    CrossfadeCurvePoint pt;
    pt.timeRel = juce::jlimit(0.0, 1.0, timeRel);
    pt.value   = juce::jlimit(0.0, 1.0, value);
    auto it = std::lower_bound(tr.customCurve.begin(), tr.customCurve.end(), pt,
        [](const CrossfadeCurvePoint& a, const CrossfadeCurvePoint& b){ return a.timeRel < b.timeRel; });
    const int idx = (int) std::distance(tr.customCurve.begin(), it);
    tr.customCurve.insert(it, pt);
    repaint();
    return idx;
}

void StudioTimeline::removeCrossfadePoint(int gapIndex, int ptIndex) {
    if (gapIndex < 0 || gapIndex >= (int) m_transitions.size()) return;
    auto& curve = m_transitions[(size_t) gapIndex].customCurve;
    if (ptIndex < 0 || ptIndex >= (int) curve.size()) return;
    curve.erase(curve.begin() + ptIndex);
    repaint();
}

void StudioTimeline::setCrossfadePoint(int gapIndex, int ptIndex, double timeRel, double value) {
    if (gapIndex < 0 || gapIndex >= (int) m_transitions.size()) return;
    auto& curve = m_transitions[(size_t) gapIndex].customCurve;
    if (ptIndex < 0 || ptIndex >= (int) curve.size()) return;
    const double tMin = (ptIndex > 0) ? curve[(size_t) ptIndex - 1].timeRel : 0.0;
    const double tMax = (ptIndex + 1 < (int) curve.size()) ? curve[(size_t) ptIndex + 1].timeRel : 1.0;
    curve[(size_t) ptIndex].timeRel = juce::jlimit(tMin, tMax, juce::jlimit(0.0, 1.0, timeRel));
    curve[(size_t) ptIndex].value   = juce::jlimit(0.0, 1.0, value);
    repaint();
}

int StudioTimeline::hitCrossfadePoint(juce::Point<int> pos, int& outGapIndex, int& outPtIndex) const {
    outGapIndex = -1; outPtIndex = -1;
    const double yMin = (double) (m_rulerHeight + 4);
    const double yMax = (double) (getHeight() - 8);
    if (yMax <= yMin) return -1;
    for (size_t i = 0; i + 1 < m_clips.size(); ++i) {
        if (i >= m_transitions.size()) break;
        if (m_transitions[i].kind != TransitionKind::Custom) continue;
        const auto& a = *m_clips[i];
        const auto& b = *m_clips[i + 1];
        const double ax = (double) xOfSec(a.getStartSec() + a.getLengthSec());
        const double bx = (double) xOfSec(b.getStartSec());
        if (bx <= ax) continue;
        const auto& curve = m_transitions[i].customCurve;
        for (size_t j = 0; j < curve.size(); ++j) {
            const double tx = ax + curve[j].timeRel * (bx - ax);
            const double ty = yMin + (1.0 - curve[j].value) * (yMax - yMin);
            const double dx = (double) pos.x - tx;
            const double dy = (double) pos.y - ty;
            if (dx * dx + dy * dy <= 64.0) {
                outGapIndex = (int) i;
                outPtIndex  = (int) j;
                return 0;
            }
        }
    }
    return -1;
}

void StudioTimeline::addClipEffect(int clipIndex, const std::string& type) {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return;
    m_clips[(size_t) clipIndex]->addEffect(type);
}

void StudioTimeline::removeClipEffect(int clipIndex, int fxIndex) {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return;
    m_clips[(size_t) clipIndex]->removeEffect(fxIndex);
}

void StudioTimeline::setClipEffectParam(int clipIndex, int fxIndex,
                                         const std::string& paramName, float value) {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return;
    m_clips[(size_t) clipIndex]->setEffectParam(fxIndex, paramName, value);
}

int StudioTimeline::getNumClipEffects(int clipIndex) const {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return 0;
    return (int) m_clips[(size_t) clipIndex]->getEffects().size();
}

namespace {

void applyClipEffectsToBuffer(juce::AudioBuffer<float>& buf,
                               double sampleRate,
                               const std::vector<ClipEffect>& effects) {
    if (effects.empty() || buf.getNumSamples() <= 0 || buf.getNumChannels() <= 0)
        return;

    const int n   = buf.getNumSamples();
    const int chs = buf.getNumChannels();
    std::vector<float> il((size_t) n * (size_t) chs, 0.0f);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < chs; ++c)
            il[(size_t) i * (size_t) chs + (size_t) c] = buf.getSample(c, i);

    auto getParam = [&](const ClipEffect& fx, const std::string& name, float def) {
        for (const auto& p : fx.params) if (p.first == name) return p.second;
        return def;
    };

    for (const auto& fx : effects) {
        if (!fx.enabled) continue;
        std::unique_ptr<BeatMate::Core::DSPProcessor> proc;
        if (fx.type == "reverb") {
            auto* p = new BeatMate::Core::ReverbProcessor();
            p->setRoomSize(getParam(fx, "size",     0.6f));
            p->setDamping (getParam(fx, "damping",  0.5f));
            p->setWet     (getParam(fx, "wet",      0.35f));
            p->setDry     (getParam(fx, "dry",      0.85f));
            proc.reset(p);
        } else if (fx.type == "delay") {
            auto* p = new BeatMate::Core::DelayProcessor();
            p->setDelayTime(getParam(fx, "ms",       350.0f));
            p->setFeedback (getParam(fx, "feedback",  0.45f));
            p->setMix      (getParam(fx, "mix",       0.30f));
            proc.reset(p);
        } else if (fx.type == "eq3") {
            auto* p = new BeatMate::Core::EQProcessor();
            p->setLow (getParam(fx, "low",  0.0f));
            p->setMid (getParam(fx, "mid",  0.0f));
            p->setHigh(getParam(fx, "high", 0.0f));
            proc.reset(p);
        } else if (fx.type == "compressor") {
            auto* p = new BeatMate::Core::CompressorProcessor();
            p->setThreshold (getParam(fx, "threshold", -18.0f));
            p->setRatio     (getParam(fx, "ratio",       4.0f));
            p->setAttack    (getParam(fx, "attack",     10.0f));
            p->setRelease   (getParam(fx, "release",   100.0f));
            p->setMakeupGain(getParam(fx, "makeup",      2.0f));
            proc.reset(p);
        } else if (fx.type == "filter") {
            auto* p = new BeatMate::Core::FilterProcessor();
            p->setType(BeatMate::Core::FilterType::LowPass);
            p->setFrequency(getParam(fx, "freq", 8000.0f));
            p->setQ        (getParam(fx, "q",       0.707f));
            proc.reset(p);
        }
        if (!proc) continue;
        proc->setSampleRate(sampleRate);
        proc->process(il.data(), n, chs);
    }

    for (int i = 0; i < n; ++i)
        for (int c = 0; c < chs; ++c)
            buf.setSample(c, i, il[(size_t) i * (size_t) chs + (size_t) c]);
}

bool loadStemBuffer(const juce::File& f,
                    double audioInSec, double audioOutSec,
                    juce::AudioBuffer<float>& out, double& outSr) {
    if (!f.existsAsFile()) return false;
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
    if (!reader) return false;
    outSr = reader->sampleRate;
    const int64_t s0 = (int64_t) (audioInSec  * outSr);
    const int64_t s1 = (int64_t) (audioOutSec * outSr);
    const int64_t n  = std::max<int64_t>(0, std::min<int64_t>(
                          (int64_t) reader->lengthInSamples - s0, s1 - s0));
    if (n <= 0) return false;
    out.setSize((int) reader->numChannels, (int) n, false, true, false);
    return reader->read(&out, 0, (int) n, s0, true, true);
}

} // namespace

void StudioTimeline::extractStemsForSelection() {
    struct Job { StudioClip* clip; double ai; double ao; };
    std::vector<Job> jobs;

    if (hasTimeRange()) {
        for (auto& cp : m_clips) {
            const double cs = cp->getStartSec();
            const double ce = cs + cp->getLengthSec();
            const double s  = std::max(m_rangeStartSec, cs);
            const double e  = std::min(m_rangeEndSec,   ce);
            if (e <= s + 0.05) continue;
            const double aIn  = cp->getAudioInSec() + (s - cs);
            const double aOut = cp->getAudioInSec() + (e - cs);
            jobs.push_back({ cp.get(), aIn, aOut });
        }
    } else {
        std::vector<int> indices;
        if (m_selected >= 0 && m_selected < (int) m_clips.size())
            indices.push_back(m_selected);
        else
            indices = m_multiSel;
        for (int idx : indices) {
            if (idx < 0 || idx >= (int) m_clips.size()) continue;
            auto* cp = m_clips[(size_t) idx].get();
            jobs.push_back({ cp, cp->getAudioInSec(), cp->getAudioOutSec() });
        }
    }
    if (jobs.empty()) {
        spdlog::warn("[Studio] extractStemsForSelection ABORT: no selection / no range");
        return;
    }

    extern BeatMate::ServiceLocator* g_serviceLocator;
    bool ultra = false;
    if (g_serviceLocator) {
        if (auto* settings = g_serviceLocator->tryGet<Services::Config::SettingsManager>())
            ultra = settings->get<bool>("stems_ultra_mdx_gpu", false);
    }
    const bool useUltra = ultra && Core::Stems::StemSepSotaService::isAvailable();

    showProgress(useUltra
        ? juce::String::fromUTF8("Extraction stems (Ultra MDX-GPU)")
        : juce::String::fromUTF8("Extraction stems (Demucs)"));
    juce::Component::SafePointer<StudioTimeline> selfTl { this };

    auto totalJobs = (int) jobs.size();
    auto done = std::make_shared<std::atomic<int>>(0);

    for (auto& j : jobs) {
        juce::Component::SafePointer<StudioClip> safe { j.clip };
        const std::string in = j.clip->getTrack().filePath;
        const double ai = j.ai, ao = j.ao;
        std::thread([selfTl, safe, in, ai, ao, totalJobs, done, useUltra]() {
            auto data = std::make_unique<StudioClip::StemData>();
            double sr = 44100.0;

            if (useUltra) {
                Core::Stems::StemSepSotaService sota;
                auto res = sota.separate(juce::File(in),
                    Core::Stems::StemSepSotaService::Model::FourStems,
                    [selfTl](float pct, const juce::String& phase) {
                        juce::MessageManager::callAsync([selfTl, pct, phase]() {
                            if (selfTl) selfTl->updateProgress(pct, phase);
                        });
                    });
                if (!res.ok) {
                    juce::MessageManager::callAsync([selfTl, msg = res.message]() {
                        if (selfTl) selfTl->updateProgress(1.0f,
                            juce::String::fromUTF8("Echec : ") + msg);
                    });
                    return;
                }
                const bool okD = loadStemBuffer(res.drums,  ai, ao, data->drums,  sr);
                const bool okB = loadStemBuffer(res.bass,   ai, ao, data->bass,   sr);
                const bool okO = loadStemBuffer(res.other,  ai, ao, data->other,  sr);
                const bool okV = loadStemBuffer(res.vocals, ai, ao, data->vocals, sr);
                data->ready = okD && okB && okO && okV;
                data->sampleRate = sr;
            } else {
                BeatMate::Core::Stems::DemucsStemService demucs;
                if (BeatMate::Core::Stems::DemucsStemService::findDemucsLauncher().empty()) {
                    juce::MessageManager::callAsync([selfTl]() {
                        if (selfTl) selfTl->updateProgress(1.0f,
                            juce::String::fromUTF8("Demucs introuvable (pip install demucs)"));
                    });
                    return;
                }
                const juce::File outDir = juce::File::getSpecialLocation(
                    juce::File::tempDirectory).getChildFile("BeatMate_stems");
                outDir.createDirectory();

                auto res = demucs.separate(in, outDir.getFullPathName().toStdString(),
                    [selfTl](float pct, const std::string& phase){
                        juce::MessageManager::callAsync([selfTl, pct, phase]() {
                            if (selfTl) selfTl->updateProgress(pct, juce::String(phase));
                        });
                    });
                if (!res.ok) {
                    juce::MessageManager::callAsync([selfTl, msg = res.message]() {
                        if (selfTl) selfTl->updateProgress(1.0f,
                            juce::String::fromUTF8("Echec : ") + juce::String(msg));
                    });
                    return;
                }
                const bool okD = loadStemBuffer(juce::File{ res.stemPaths[0] }, ai, ao, data->drums,  sr);
                const bool okB = loadStemBuffer(juce::File{ res.stemPaths[1] }, ai, ao, data->bass,   sr);
                const bool okO = loadStemBuffer(juce::File{ res.stemPaths[2] }, ai, ao, data->other,  sr);
                const bool okV = loadStemBuffer(juce::File{ res.stemPaths[3] }, ai, ao, data->vocals, sr);
                data->ready = okD && okB && okO && okV;
                data->sampleRate = sr;
            }

            juce::MessageManager::callAsync(
                [selfTl, safe, raw = data.release(), totalJobs, done]() mutable {
                    std::unique_ptr<StudioClip::StemData> owned(raw);
                    if (auto* c = safe.getComponent()) c->attachStems(std::move(owned));
                    const int n = ++(*done);
                    if (selfTl) {
                        selfTl->updateProgress((float) n / (float) totalJobs,
                            juce::String::fromUTF8("Stems prêts ") +
                            juce::String(n) + " / " + juce::String(totalJobs));
                        if (n >= totalJobs) {
                            juce::Timer::callAfterDelay(700, [selfTl]() {
                                if (selfTl) selfTl->hideProgress();
                            });
                        }
                    }
                });
        }).detach();
    }
}

bool StudioTimeline::clipHasStems(int clipIndex) const {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return false;
    return m_clips[(size_t) clipIndex]->hasStems();
}

void StudioTimeline::setClipStemMute(int clipIndex, int stemIndex, bool muted) {
    if (clipIndex < 0 || clipIndex >= (int) m_clips.size()) return;
    m_clips[(size_t) clipIndex]->setStemMute(stemIndex, muted);
}


int StudioTimeline::getNumLanes() const {
    int m = 0;
    for (int l : m_clipLaneCache) m = std::max(m, l);
    return std::max(1, m + 1);
}

void StudioTimeline::ensureLaneInfos(int n) {
    static const juce::Colour kPalette[] = {
        juce::Colour(0xFF3B82F6), juce::Colour(0xFFEC4899),
        juce::Colour(0xFFF59E0B), juce::Colour(0xFF10B981),
        juce::Colour(0xFF8B5CF6), juce::Colour(0xFFEF4444),
        juce::Colour(0xFF22C55E), juce::Colour(0xFF06B6D4)
    };
    while ((int) m_laneInfos.size() < n) {
        LaneInfo li;
        li.name = "Track " + juce::String((int) m_laneInfos.size() + 1);
        li.color = kPalette[m_laneInfos.size() % 8];
        m_laneInfos.push_back(li);
    }
}

LaneInfo StudioTimeline::getLaneInfo(int i) const {
    if (i < 0 || i >= (int) m_laneInfos.size()) return LaneInfo{};
    return m_laneInfos[(size_t) i];
}

void StudioTimeline::setLaneInfo(int i, const LaneInfo& info) {
    if (i < 0) return;
    ensureLaneInfos(i + 1);
    m_laneInfos[(size_t) i] = info;
    applyLaneStateToClips();
    repaint();
}

void StudioTimeline::setLaneMuted (int i, bool m){ auto x=getLaneInfo(i); x.muted =m; setLaneInfo(i,x); }
void StudioTimeline::setLaneSolo  (int i, bool s){ auto x=getLaneInfo(i); x.solo  =s; setLaneInfo(i,x); }
void StudioTimeline::setLaneArmed (int i, bool a){ auto x=getLaneInfo(i); x.armed =a; setLaneInfo(i,x); }
void StudioTimeline::setLaneLocked(int i, bool l){ auto x=getLaneInfo(i); x.locked=l; setLaneInfo(i,x); }
void StudioTimeline::setLaneVolumeDb(int i, float db){
    auto x = getLaneInfo(i);
    x.volumeDb = juce::jlimit(-60.0f, 6.0f, db);
    setLaneInfo(i, x);
}
void StudioTimeline::setLaneName (int i, const juce::String& n){ auto x=getLaneInfo(i); x.name =n; setLaneInfo(i,x); }
void StudioTimeline::setLaneColor(int i, juce::Colour c)        { auto x=getLaneInfo(i); x.color=c; setLaneInfo(i,x); }

int StudioTimeline::getLaneOfClip(int idx) const {
    if (idx < 0 || idx >= (int) m_clipLaneCache.size()) return 0;
    return m_clipLaneCache[(size_t) idx];
}

void StudioTimeline::applyLaneStateToClips() {
    bool anySolo = false;
    for (const auto& l : m_laneInfos) if (l.solo) { anySolo = true; break; }
    for (size_t i = 0; i < m_clips.size(); ++i) {
        const int lane = getLaneOfClip((int) i);
        const auto& li = (lane < (int) m_laneInfos.size()) ? m_laneInfos[(size_t) lane] : LaneInfo{};
        const bool effMute = li.muted || (anySolo && !li.solo);
        m_clips[i]->setMuted(effMute);
        if (li.locked) m_clips[i]->setLocked(true);
    }
}

void StudioTimeline::handleSidebarMouseDown(const juce::MouseEvent& e) {
    const int numL   = getNumLanes();
    if (numL <= 0) return;
    ensureLaneInfos(numL);

    const int yTop   = m_rulerHeight + 4;
    const int totalH = std::max(80, getHeight() - m_rulerHeight - 26);
    const int laneH  = std::max(40, totalH / std::max(1, numL) - 2);

    const int rel = e.getPosition().y - yTop;
    if (rel < 0) return;
    const int lane = rel / std::max(1, (laneH + 2));
    if (lane >= numL) return;

    const int y = yTop + lane * (laneH + 2);
    const int x = e.getPosition().x;

    if (x < 8) {
        static const juce::Colour kPalette[] = {
            juce::Colour(0xFF3B82F6), juce::Colour(0xFFEC4899),
            juce::Colour(0xFFF59E0B), juce::Colour(0xFF10B981),
            juce::Colour(0xFF8B5CF6), juce::Colour(0xFFEF4444),
            juce::Colour(0xFF22C55E), juce::Colour(0xFF06B6D4)
        };
        const auto cur = m_laneInfos[(size_t) lane].color;
        int next = 0;
        for (int k = 0; k < 8; ++k) {
            if (kPalette[k].getARGB() == cur.getARGB()) { next = (k + 1) % 8; break; }
        }
        setLaneColor(lane, kPalette[next]);
        return;
    }

    const int btnY = y + 22, btnW = 20, gap = 3;
    if (x >= 14 && x < 14 + 4 * (btnW + gap) && e.getPosition().y >= btnY
        && e.getPosition().y < btnY + 16) {
        const int idx = (x - 14) / (btnW + gap);
        if (idx >= 0 && idx < 4) {
            auto info = m_laneInfos[(size_t) lane];
            switch (idx) {
                case 0: info.muted  = !info.muted;  break;
                case 1: info.solo   = !info.solo;   break;
                case 2: info.armed  = !info.armed;  break;
                case 3: info.locked = !info.locked; break;
            }
            setLaneInfo(lane, info);
        }
        return;
    }

    if (x >= 14 && x < kSidebarW - 26 && e.getPosition().y >= y
        && e.getPosition().y < y + 18) {
        if (e.getNumberOfClicks() >= 2) {
            const auto cur = m_laneInfos[(size_t) lane].name;
            juce::Component::SafePointer<StudioTimeline> safe { this };
            auto* aw = new juce::AlertWindow("Renommer la piste",
                                             "Nouveau nom :",
                                             juce::AlertWindow::NoIcon);
            aw->addTextEditor("name", cur);
            aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
            aw->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            aw->enterModalState(true,
                juce::ModalCallbackFunction::create([safe, lane, aw](int result) {
                    if (safe && result == 1) {
                        const auto txt = aw->getTextEditorContents("name").trim();
                        if (txt.isNotEmpty()) safe->setLaneName(lane, txt);
                    }
                    delete aw;
                }), false);
        }
        return;
    }

    if (x >= kSidebarW - 26 && x < kSidebarW) {
        m_draggingLaneFader = lane;
        m_draggingLaneFaderStartDb = m_laneInfos[(size_t) lane].volumeDb;
        m_draggingLaneFaderFrom = e.getPosition();
        return;
    }
}

void StudioTimeline::paintTrackHeaders(juce::Graphics& g) const {
    // const : pas de mutation pendant paint ; m_laneInfos garanti par layoutClips, fallback sinon.
    const int numL = getNumLanes();
    static const LaneInfo kDefault{};

    const int yTop   = m_rulerHeight + 4;
    const int totalH = std::max(80, getHeight() - m_rulerHeight - 26);
    const int laneH  = std::max(40, totalH / std::max(1, numL) - 2);

    // Fond opaque sidebar (couvre tout clip dépassant à gauche)
    g.setColour(juce::Colour(0xFF0F1118));
    g.fillRect(0, m_rulerHeight, kSidebarW, getHeight() - m_rulerHeight - 18);

    g.setColour(juce::Colour(0xFF1F2937));
    g.drawVerticalLine(kSidebarW - 1, (float) m_rulerHeight,
                       (float)(getHeight() - 18));

    for (int i = 0; i < numL; ++i) {
        const auto& li = (i < (int) m_laneInfos.size())
                          ? m_laneInfos[(size_t) i]
                          : kDefault;
        const int y = yTop + i * (laneH + 2);

        g.setColour(li.color);
        g.fillRect(0, y, 8, laneH);

        g.setColour(juce::Colour(0xFFF1F5F9));
        g.setFont(juce::FontOptions("Segoe UI", 11.0f, juce::Font::bold));
        g.drawText(li.name, 14, y + 4, kSidebarW - 32, 16,
                   juce::Justification::centredLeft);

        const int btnY = y + 22, btnW = 20, gap = 3;
        struct B { const char* lbl; bool on; juce::Colour col; } btns[] = {
            { "M", li.muted,  juce::Colour(0xFFEF4444) },
            { "S", li.solo,   juce::Colour(0xFFFBBF24) },
            { "R", li.armed,  juce::Colour(0xFFEC4899) },
            { "L", li.locked, juce::Colour(0xFF94A3B8) },
        };
        for (int b = 0; b < 4; ++b) {
            const int bx = 14 + b * (btnW + gap);
            juce::Rectangle<int> br(bx, btnY, btnW, 16);
            g.setColour(btns[b].on ? btns[b].col : juce::Colour(0xFF1F2937));
            g.fillRoundedRectangle(br.toFloat(), 3.5f);
            g.setColour(btns[b].on ? juce::Colours::white : juce::Colour(0xFF6B7280));
            g.setFont(juce::FontOptions("Segoe UI", 9.5f, juce::Font::bold));
            g.drawText(btns[b].lbl, br, juce::Justification::centred);
        }

        const int fX = kSidebarW - 22, fY = y + 4, fH = std::max(20, laneH - 8);
        g.setColour(juce::Colour(0xFF1F2937));
        g.fillRect(fX, fY, 4, fH);
        const float v01 = (li.volumeDb + 60.0f) / 66.0f;
        const int knobY = fY + (int)((1.0f - v01) * (fH - 8));
        g.setColour(li.color);
        g.fillRoundedRectangle((float)(fX - 6), (float) knobY, 16.0f, 8.0f, 2.0f);

        if (laneH >= 50) {
            g.setColour(juce::Colour(0xFF94A3B8));
            g.setFont(juce::FontOptions("Segoe UI", 8.5f, juce::Font::plain));
            g.drawText(juce::String(li.volumeDb, 1) + " dB",
                       fX - 40, y + laneH - 12, 36, 10,
                       juce::Justification::centredRight);
        }

        g.setColour(juce::Colour(0xFF1F2937));
        g.drawHorizontalLine(y + laneH, 0.0f, (float) kSidebarW);
    }
}

} // namespace BeatMate::UI::Studio
