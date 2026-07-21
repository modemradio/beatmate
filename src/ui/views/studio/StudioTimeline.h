#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <vector>
#include <functional>

#include "../../../models/Track.h"
#include "../../../models/CuePoint.h"
#include "../../../services/ai/AIMashupOrchestrator.h"

namespace BeatMate::UI::Studio {

enum class SnapMode { Off, Beat, HalfBeat, Bar, HalfBar };
enum class TransitionKind { Cut, Fade, EqualPower, EchoOut, FilterSweep, Backspin, Custom };
enum class AssistMode { Manual, MashupAI, MegamixAI, MedleyAI };

struct TempoPoint {
    double sec;
    double bpm;
};

struct PhraseMarker {
    double      sec;        // début (s)
    double      dur;        // durée (s)
    std::string label;      // "Intro", "Couplet", "Refrain", etc.
    juce::Colour colour { 0xFF4A9EFF };
};

struct CrossfadeCurvePoint {
    double timeRel;
    double value;
};

struct ClipEffect {
    std::string type;
    bool        enabled { true };
    std::vector<std::pair<std::string, float>> params;
};

struct ClipFade {
    double inSec  = 0.0;
    double outSec = 0.0;
};

struct ClipMarker {
    int bar = -1;
    std::string label;
    juce::Colour colour = juce::Colours::transparentBlack;
};

struct LaneInfo {
    juce::String  name      { "Track" };
    juce::Colour  color     { 0xFF3B82F6 };
    bool          muted     { false };
    bool          solo      { false };
    bool          armed     { false };
    bool          locked    { false };
    float         volumeDb  { 0.0f };  // -inf..+6dB (clampé en UI).
};

class StudioTimeline;

class StudioClip : public juce::Component,
                   public  juce::SettableTooltipClient,
                   private juce::ChangeListener
{
public:
    StudioClip(const Models::Track& track, StudioTimeline& owner);
    ~StudioClip() override;

    const Models::Track& getTrack() const noexcept { return m_track; }

    void setTitle(const juce::String& newTitle) {
        const auto t = newTitle.toStdString();
        if (m_track.title == t) return;
        m_track.title = t;
        juce::String tip;
        tip << newTitle;
        if (!m_track.artist.empty()) tip << "\n" << juce::String(m_track.artist);
        if (m_track.bpm > 0.0)       tip << "\n" << juce::String(m_track.bpm, 1) << " BPM";
        setTooltip(tip);
        repaint();
        if (onChanged) onChanged();
    }

    double getStartSec() const noexcept { return m_startSec; }
    void   setStartSec(double s);
    void   setStartSecQuiet(double s) { m_startSec = std::max(0.0, s); }

    double getAudioInSec()  const noexcept { return m_audioInSec; }
    double getAudioOutSec() const noexcept { return m_audioOutSec; }
    void   setAudioRange(double inSec, double outSec);

    double getLengthSec() const noexcept { return m_audioOutSec - m_audioInSec; }

    ClipFade getFade() const noexcept { return m_fade; }
    void     setFadeIn(double s);
    void     setFadeOut(double s);

    bool isSelected() const noexcept { return m_selected; }
    void setSelected(bool s) {
        if (m_selected == s) return;
        m_selected = s;
        repaint(getLocalBounds());
    }

    bool isLocked()   const noexcept { return m_locked; }
    void setLocked(bool l) {
        if (m_locked == l) return;
        m_locked = l;
        repaint(getLocalBounds());
    }

    void setShowStems(bool s) {
        if (m_showStems == s) return;
        m_showStems = s;
        repaint(getLocalBounds());
    }
    void setMuted(bool m) {
        if (m_muted == m) return;
        m_muted = m;
        repaint(getLocalBounds());
    }
    bool isMuted() const noexcept { return m_muted; }
    void setVolume(float v01) {
        const float nv = juce::jlimit(0.0f, 1.0f, v01);
        if (std::abs(m_volume - nv) < 1e-4f) return;
        m_volume = nv;
        repaint(getLocalBounds());
    }
    float getVolume() const noexcept { return m_volume; }
    void setReversed(bool r) {
        if (m_reversed == r) return;
        m_reversed = r;
        repaint(getLocalBounds());
    }
    bool isReversed() const noexcept { return m_reversed; }

    void setPitchSemitones(double st) {
        const double nv = juce::jlimit(-24.0, 24.0, st);
        if (std::abs(m_pitchSemitones - nv) < 1e-4) return;
        m_pitchSemitones = nv;
        if (onChanged) onChanged();
        repaint(getLocalBounds());
    }
    double getPitchSemitones() const noexcept { return m_pitchSemitones; }
    void setTempoRatio(double ratio) {
        const double nv = juce::jlimit(0.25, 4.0, ratio);
        if (std::abs(m_tempoRatio - nv) < 1e-4) return;
        m_tempoRatio = nv;
        if (onChanged) onChanged();
        repaint(getLocalBounds());
    }
    double getTempoRatio() const noexcept { return m_tempoRatio; }

    struct AutoPoint { double timeRel; float value; };
    void addVolumeAutoPoint(double t, float v);
    void addEqHiAutoPoint  (double t, float v);
    void addEqMidAutoPoint (double t, float v);
    void addEqLoAutoPoint  (double t, float v);
    void clearAutomation();
    const std::vector<AutoPoint>& getVolumeAutomation() const noexcept { return m_autoVol; }
    const std::vector<AutoPoint>& getEqHiAutomation()   const noexcept { return m_autoEqHi; }
    const std::vector<AutoPoint>& getEqMidAutomation()  const noexcept { return m_autoEqMid; }
    const std::vector<AutoPoint>& getEqLoAutomation()   const noexcept { return m_autoEqLo; }

    void setMarker(const std::string& name, juce::Colour col);
    const ClipMarker& getMarker() const noexcept { return m_marker; }

    void setLoop(double inRel, double outRel, bool enabled);
    void setCues(std::vector<Models::CuePoint> cues);
    struct LoopState { double inRel; double outRel; bool on; };
    LoopState getLoop() const noexcept { return { m_loopInRel, m_loopOutRel, m_loopOn }; }
    const std::vector<Models::CuePoint>& getCues() const noexcept { return m_cues; }

    juce::AudioThumbnail* getThumbnail() const { return m_thumb.get(); }

    void addEffect(const std::string& type);
    void removeEffect(int idx);
    void setEffectParam(int fxIdx, const std::string& name, float v);
    void setEffectEnabled(int fxIdx, bool enabled);
    const std::vector<ClipEffect>& getEffects() const noexcept { return m_effects; }

    struct StemData {
        juce::AudioBuffer<float> vocals, drums, bass, other;
        bool   ready { false };
        double sampleRate { 44100.0 };
    };
    bool       hasStems() const noexcept { return m_stems != nullptr && m_stems->ready; }
    StemData*  getStems() noexcept { return m_stems.get(); }
    void       attachStems(std::unique_ptr<StemData> s);
    void       setStemMute(int stemIdx, bool muted);
    bool       isStemMuted(int stemIdx) const noexcept;

    int        getGroupId() const noexcept { return m_groupId; }
    void       setGroupId(int id) { m_groupId = id; repaint(getLocalBounds()); }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;

    std::function<void()> onChanged;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    enum class DragMode { None, Move, ResizeLeft, ResizeRight, FadeIn, FadeOut,
                           AutoPoint, HotCue, LoopLeft, LoopRight };
    DragMode hitTest(juce::Point<int> p) const;
    int      hitAutoPoint(juce::Point<int> p) const;
    int      hitHotCue   (juce::Point<int> p) const;
    int      hitLoopHandle(juce::Point<int> p) const;
    void paintWaveform(juce::Graphics& g, juce::Rectangle<float> r);
    void paintStems   (juce::Graphics& g, juce::Rectangle<float> r);
    void paintHeader  (juce::Graphics& g, juce::Rectangle<float> r);
    void paintFades   (juce::Graphics& g, juce::Rectangle<float> r);
    void paintCues    (juce::Graphics& g, juce::Rectangle<float> r);
    void paintLoop    (juce::Graphics& g, juce::Rectangle<float> r);
    void paintAutomation(juce::Graphics& g, juce::Rectangle<float> r);
    void paintMarker  (juce::Graphics& g, juce::Rectangle<float> r);

    Models::Track                   m_track;
    StudioTimeline&                 m_owner;
    std::unique_ptr<juce::AudioThumbnail> m_thumb;

    double     m_startSec    { 0.0 };
    double     m_audioInSec  { 0.0 };
    double     m_audioOutSec { 0.0 };

    ClipFade   m_fade;
    bool       m_selected { false };
    bool       m_locked   { false };
    ClipMarker m_marker;

    double     m_loopInRel  { -1.0 };
    double     m_loopOutRel { -1.0 };
    bool       m_loopOn     { false };
    std::vector<Models::CuePoint> m_cues;

    bool       m_showStems  { false };
    bool       m_muted      { false };
    bool       m_reversed   { false };
    float      m_volume     { 1.0f };
    double     m_pitchSemitones { 0.0 };
    double     m_tempoRatio     { 1.0 };
    std::vector<AutoPoint> m_autoVol;
    std::vector<AutoPoint> m_autoEqHi, m_autoEqMid, m_autoEqLo;

    std::vector<ClipEffect>     m_effects;
    std::unique_ptr<StemData>   m_stems;
    bool                        m_stemMutes[4] { false, false, false, false };
    int                         m_groupId { 0 };

    DragMode          m_dragMode  { DragMode::None };
    juce::Point<int>  m_dragFrom;
    double            m_dragStartSec    { 0.0 };
    double            m_dragAudioIn     { 0.0 };
    double            m_dragAudioOut    { 0.0 };
    double            m_dragFadeIn      { 0.0 };
    double            m_dragFadeOut     { 0.0 };
    int               m_dragAutoIdx     { -1 };
    int               m_dragCueIdx      { -1 };

    juce::Image  m_waveCache;
    int          m_waveCacheW { 0 };
    int          m_waveCacheH { 0 };
    double       m_waveCacheZoom { -1.0 };
    bool         m_waveCacheValid { false };
    bool         m_waveCacheHadRgb { false };
    void         invalidateWaveCache() noexcept { m_waveCacheValid = false; }

public:
    bool isDragging() const noexcept { return m_dragMode != DragMode::None; }

    void setHovered(bool h) noexcept {
        if (m_hovered == h) return;
        m_hovered = h;
        repaint(getLocalBounds());
    }
    bool isHovered() const noexcept { return m_hovered; }

private:
    bool m_hovered { false };
};

class StudioTimeline : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
private:
    void timerCallback() override;
public:
public:
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void clipSelected(int index)            {}
        virtual void clipChanged (int index)            {}
        virtual void clipsReordered()                   {}
        virtual void playheadMoved(double seconds)      {}
        virtual void filesDropped(const juce::StringArray& paths) {}
    };

    StudioTimeline();
    ~StudioTimeline() override;

    void addListener(Listener* l)    { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    int         addClip(const Models::Track& track);
    bool        removeClip(int index);
    bool        duplicateClip(int index);
    bool        splitAtPlayhead();
    void        reorderClip(int from, int to);
    void        renameClip(int index, const juce::String& newTitle);

    int         getNumClips() const noexcept { return (int) m_clips.size(); }
    StudioClip* getClip(int i) const;
    int         indexOf(const StudioClip* c) const;

    int         getSelectedIndex() const noexcept { return m_selected; }
    void        setSelectedIndex(int i);

    void        selectAll();
    void        deselectAll();

    double      getPlayheadSec() const noexcept { return m_playhead; }
    void        setPlayheadSec(double s);
    void        jumpToStart()  { setPlayheadSec(0.0); }
    void        jumpToEnd();
    void        nudgePlayhead(double delta);
    void        jumpToNextTransition();
    void        jumpToPrevTransition();

    double      getZoom() const noexcept { return m_zoom; }
    void        setZoom(double z);

    double      secAtX(int xPixel) const noexcept {
        const double pps = std::max(1.0, pixelsPerSec());
        return std::max(0.0, (double)(xPixel - kSidebarW) / pps);
    }
    float       xOfSec(double sec) const noexcept {
        return (float)(sec * pixelsPerSec()) + (float) kSidebarW;
    }
    void        zoomIn();
    void        zoomOut();
    void        fitView();

    SnapMode    getSnap() const noexcept { return m_snap; }
    void        setSnap(SnapMode s) { m_snap = s; repaint(); }

    void        setCenterMode(bool on) { m_centerPlayhead = on; repaint(); }
    bool        isCenterMode() const noexcept { return m_centerPlayhead; }

    void        setShowBeatGrid(bool on)   { m_showBeatGrid = on; repaint(); }
    void        setShowStems   (bool on);
    bool        showsBeatGrid() const noexcept { return m_showBeatGrid; }
    bool        showsStems    () const noexcept { return m_showStems; }

    void        cutSelection();
    void        copySelection();
    void        pasteAtPlayhead();
    void        pasteAt(double atSec);
    void        pasteAtMouse();
    void        deleteSelection();
    void        toggleSelectClip(int i);
    void        rubberBandSelect(juce::Rectangle<int> screenRect);
    std::vector<int> getMultiSelection() const noexcept { return m_multiSel; }

    void        undo();
    void        redo();
    void        pushUndoSnapshot();

    void        setClipMute  (int i, bool muted);
    void        setClipVolume(int i, float gain01);
    void        setClipFadeIn (int i, double sec);
    void        setClipFadeOut(int i, double sec);
    void        setClipLooped(int i, double inRel, double outRel, bool on);

    void        setTransitionType(int gapIndex, TransitionKind k);
    void        setTransitionDurationBars(int gapIndex, int bars);
    void        setTransitionDurationBeats(int gapIndex, int beats);
    void        applyTransitionPresetAll(TransitionKind k, int beats);
    int         getNumTransitions() const noexcept;

    bool        saveProject(const juce::File& bmproj) const;
    bool        loadProject(const juce::File& bmproj);

    void        exportMixToWav(const juce::File& outFile);
    void        exportToAbleton(const juce::File& alsFile);

    void        runMashupAI();
    void        runMegamixAI();
    void        runMedleyAI();
    void        runRemixAI();

    void        applyMashupPlan(const BeatMate::Services::AI::MashupResult& plan);

    void        play();
    void        stop();
    bool        isPlaying() const noexcept { return m_playing.load(); }
    void        toggleMetronome() { m_metronome = !m_metronome; }
    void        setMasterVolume(float v) { m_masterVol.store(juce::jlimit(0.0f, 2.0f, v)); }
    float       getMasterVolume() const noexcept { return m_masterVol.load(); }

    void        setMasterCompressorAmount(float amt0to10);
    float       getMasterCompressorAmount() const noexcept { return m_masterCompAmount.load(); }
    float       getMasterCompressorGainReduction() const noexcept;

    // Master L/R peak amplitude (linear 0..1, decayed in audio thread).
    float       getMasterPeakL() const noexcept { return m_masterPeakL.load(); }
    float       getMasterPeakR() const noexcept { return m_masterPeakR.load(); }

    void        setTimeSignature(int num, int den) { m_tsigNum = num; m_tsigDen = den; repaint(); }

    juce::String formatTimecode(double sec) const;
    double      getAverageBpm() const;

    void        quantizeAllClips();
    void        alignClipToNextBeat(int i);
    void        alignClipToPrevBeat(int i);

    void        beginMashupAssist(AssistMode m);
    AssistMode  getAssistMode() const noexcept { return m_assist; }

    double      snap(double sec) const;

    double      getTotalLengthSec() const;
    double      pixelsPerSec() const noexcept { return 40.0 * m_zoom; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp  (const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d) override;
    bool keyPressed(const juce::KeyPress& key) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    double getMasterBpm() const noexcept { return m_masterBpm; }
    void   setMasterBpm(double b) { m_masterBpm = b; repaint(); }

    int    addTempoPoint(double sec, double bpm);    // return index
    void   removeTempoPoint(int index);
    void   setTempoPoint(int index, double sec, double bpm);
    int    getNumTempoPoints() const noexcept { return (int) m_tempoPoints.size(); }
    const std::vector<TempoPoint>& getTempoPoints() const noexcept { return m_tempoPoints; }
    double getBpmAt(double sec) const;

    void   setTransitionCustomCurve(int gapIndex, std::vector<CrossfadeCurvePoint> curve);
    const std::vector<CrossfadeCurvePoint>& getTransitionCustomCurve(int gapIndex) const;

    int    addCrossfadePoint(int gapIndex, double timeRel, double value);
    void   removeCrossfadePoint(int gapIndex, int ptIndex);
    void   setCrossfadePoint(int gapIndex, int ptIndex, double timeRel, double value);
    int    hitCrossfadePoint(juce::Point<int> pos, int& outGapIndex, int& outPtIndex) const;

    void   addClipEffect(int clipIndex, const std::string& type);
    void   removeClipEffect(int clipIndex, int fxIndex);
    void   setClipEffectParam(int clipIndex, int fxIndex,
                              const std::string& paramName, float value);
    int    getNumClipEffects(int clipIndex) const;

    void   extractStemsForSelection();
    bool   clipHasStems(int clipIndex) const;
    void   setClipStemMute(int clipIndex, int stemIndex, bool muted);

    bool   hasTimeRange() const noexcept {
        return m_rangeStartSec >= 0.0 && m_rangeEndSec > m_rangeStartSec;
    }
    double getRangeStartSec() const noexcept { return m_rangeStartSec; }
    double getRangeEndSec()   const noexcept { return m_rangeEndSec; }
    void   setTimeRange(double s, double e);
    void   clearTimeRange();

    void   razorAtRange();
    void   reverseRange();
    void   applyFadesToRange(double fadeInSec, double fadeOutSec);
    void   applyGainToRange(float gain);
    void   bounceRangeToClip();
    bool   isRangeLoopEnabled() const noexcept { return m_rangeLoopEnabled; }
    void   setRangeLoopEnabled(bool b);
    void   auditionRange();
    void   stretchRange(double tempoRatio, double pitchSemitones);
    void   groupSelectedClips();
    void   ungroupSelectedClips();
    void   propagateGroupMove(int groupId, double deltaSec, StudioClip* originator);

    void   setPhrases(std::vector<PhraseMarker> phrases) {
        m_phrases = std::move(phrases);
        repaint();
    }
    void   addPhrase(double sec, double dur, const std::string& label,
                     juce::Colour col = juce::Colour(0xFF4A9EFF)) {
        m_phrases.push_back({ sec, dur, label, col });
        repaint();
    }
    const std::vector<PhraseMarker>& getPhrases() const noexcept { return m_phrases; }
    void   clearPhrases() { m_phrases.clear(); repaint(); }
    void   setStemMuteOnRange(int stemIndex, bool muted);
    void   bounceInPlace();

    void   paintBeatGrid   (juce::Graphics& g) const;
    void   paintPlayhead   (juce::Graphics& g) const;
    void   paintTransitions(juce::Graphics& g) const;
    void   paintRulerBar   (juce::Graphics& g) const;
    void   paintTrackHeaders(juce::Graphics& g) const;

    static constexpr int kSidebarW = 180;
    int    getNumLanes() const;
    void   ensureLaneInfos(int n);
    LaneInfo getLaneInfo(int laneIdx) const;
    void   setLaneInfo(int laneIdx, const LaneInfo& info);
    void   setLaneMuted (int laneIdx, bool m);
    void   setLaneSolo  (int laneIdx, bool s);
    void   setLaneArmed (int laneIdx, bool a);
    void   setLaneLocked(int laneIdx, bool l);
    void   setLaneVolumeDb(int laneIdx, float db);
    void   setLaneName  (int laneIdx, const juce::String& name);
    void   setLaneColor (int laneIdx, juce::Colour c);
    int    getLaneOfClip(int clipIdx) const;
    void   applyLaneStateToClips();   // propage mute aux clips de la lane

private:
    void   handleSidebarMouseDown(const juce::MouseEvent& e);
public:

    struct ClipboardEntry {
        Models::Track track;
        double audioIn  = 0.0;
        double audioOut = 0.0;
        double fadeIn   = 0.0;
        double fadeOut  = 0.0;
        float  volume   = 1.0f;
        bool   reversed = false;
        bool   muted    = false;
        bool   locked   = false;
        int    groupId  = 0;
        std::vector<ClipEffect> effects;
        std::vector<StudioClip::AutoPoint> autoVol, autoEqHi, autoEqMid, autoEqLo;
        std::vector<Models::CuePoint> cues;
        double loopInRel  = -1.0;
        double loopOutRel = -1.0;
        bool   loopOn     = false;
        ClipMarker marker;
        double pitchSemitones = 0.0;
        double tempoRatio     = 1.0;
        double startRelToFirst = 0.0;
    };

    void layoutClips();

    std::function<void()> onMashupAssistRequested;

private:
    juce::ListenerList<Listener> m_listeners;

    std::vector<std::unique_ptr<StudioClip>> m_clips;
    int        m_selected       { -1 };
    double     m_playhead       { 0.0 };
    double     m_zoom           { 1.0 };
    double     m_masterBpm      { 128.0 };
    SnapMode   m_snap           { SnapMode::Beat };
    bool       m_centerPlayhead { false };
    bool       m_showBeatGrid   { true };
    bool       m_showStems      { false };
    AssistMode m_assist         { AssistMode::Manual };

    std::vector<ClipboardEntry> m_clipboard;

    struct GapTransition {
        TransitionKind kind = TransitionKind::EqualPower;
        int            bars = 2;
        std::vector<CrossfadeCurvePoint> customCurve;
    };
    std::vector<GapTransition> m_transitions;

    std::vector<TempoPoint>    m_tempoPoints;
    std::vector<PhraseMarker>  m_phrases;

    std::vector<LaneInfo>      m_laneInfos;
    std::vector<int>           m_clipLaneCache;     // lane assigné par clip (rempli par layoutClips)
    int                        m_renamingLane { -1 }; // -1 = aucun
    int                        m_draggingLaneFader { -1 };
    float                      m_draggingLaneFaderStartDb { 0.0f };
    juce::Point<int>           m_draggingLaneFaderFrom;

    int    m_dragTempoPointIdx { -1 };
    double m_dragTempoStartSec { 0.0 };
    double m_dragTempoStartBpm { 0.0 };
    juce::Point<int> m_dragTempoFrom;

    int    m_dragXfadeGap { -1 };
    int    m_dragXfadePt  { -1 };
    juce::Point<int> m_dragXfadeFrom;
    double m_dragXfadeStartTimeRel { 0.0 };
    double m_dragXfadeStartValue { 0.0 };

    std::vector<int>           m_multiSel;
    juce::Rectangle<int>       m_rubber;
    bool                       m_rubbering { false };

    double m_rangeStartSec { -1.0 };
    double m_rangeEndSec   { -1.0 };
    bool   m_rangeDragActive { false };
    double m_rangeDragAnchorSec { 0.0 };
    enum class RangeEdgeDrag { None, Left, Right };
    RangeEdgeDrag m_rangeEdgeDrag { RangeEdgeDrag::None };

    class ProgressOverlay;
    std::unique_ptr<ProgressOverlay> m_progress;
    void showProgress(const juce::String& title);
    void updateProgress(float pct, const juce::String& phase);
    void hideProgress();
    void layoutProgressOverlay();

    class TransitionInlineEditor;
    std::unique_ptr<TransitionInlineEditor> m_transitionEditor;
    int  m_editingGapIdx { -1 };
    void openTransitionEditor(int gapIdx, juce::Rectangle<int> gapRect);
    void closeTransitionEditor();

    struct UndoSnapshot {
        struct ClipState {
            int64_t trackId;
            std::string filePath;
            std::string title;
            double startSec;
            double audioIn;
            double audioOut;
            double fadeIn;
            double fadeOut;
            bool   locked;
            bool   muted;
            bool   reversed;
            float  volume;
            int    groupId;
            std::vector<StudioClip::AutoPoint> autoVol, autoEqHi, autoEqMid, autoEqLo;
            std::vector<ClipEffect> effects;
            std::array<bool, 4> stemMutes { { false, false, false, false } };
            double loopInRel { -1.0 }, loopOutRel { -1.0 };
            bool   loopOn { false };
            std::vector<Models::CuePoint> cues;
            ClipMarker marker;
            double pitchSemitones { 0.0 };
            double tempoRatio     { 1.0 };
        };
        struct GapState {
            TransitionKind kind { TransitionKind::EqualPower };
            int            bars { 2 };
            std::vector<CrossfadeCurvePoint> custom;
        };
        std::vector<ClipState> clips;
        std::vector<LaneInfo>  lanes;
        std::vector<GapState>  gaps;
        double                 playhead { 0.0 };
        double                 masterBpm { 128.0 };
        int                    tsigNum { 4 }, tsigDen { 4 };
        float                  masterVol { 1.0f };
        float                  masterCompAmount { 0.0f };
        std::vector<TempoPoint>   tempoPoints;
        std::vector<PhraseMarker> phrases;
        SnapMode   snap            { SnapMode::Beat };
        double     zoom            { 1.0 };
        bool       centerPlayhead  { false };
        bool       showBeatGrid    { true };
        bool       showStems       { false };
        AssistMode assist          { AssistMode::Manual };
        double     rangeStartSec   { -1.0 };
        double     rangeEndSec     { -1.0 };
        bool       rangeLoopEnabled{ false };
    };
    std::vector<UndoSnapshot> m_undo;
    std::vector<UndoSnapshot> m_redo;
    void applySnapshot(const UndoSnapshot&);

    int                m_rulerHeight { 28 };
    std::atomic<bool>  m_playing     { false };
    bool               m_metronome   { false };
    std::atomic<float> m_masterVol   { 1.0f };
    std::atomic<float> m_masterCompAmount { 0.0f };
    // Audio-thread written, UI-thread read. Used by MasterPanel PeakMeterDual.
    std::atomic<float> m_masterPeakL { 0.0f };
    std::atomic<float> m_masterPeakR { 0.0f };
    int                m_tsigNum     { 4 };
    int                m_tsigDen     { 4 };
    int64_t            m_playStartUnixMs { 0 };
    double             m_playStartSec    { 0.0 };

    int                m_lastMouseTimelineX { -1 };
    int                m_lastMouseTimelineY { -1 };

    // Cached playhead X for dirty-region repaint.
    int                m_lastPlayheadX { -1 };

    bool               m_rangeLoopEnabled { false };
    std::atomic<bool>   m_audioLoopOn    { false };
    std::atomic<double> m_audioLoopStart { -1.0 };
    std::atomic<double> m_audioLoopEnd   { -1.0 };

    juce::AudioBuffer<float>          m_playbackBuffer;
    double                            m_playbackSr      { 44100.0 };
    std::atomic<int64_t>              m_playbackFrame   { 0 };
    juce::AudioDeviceManager          m_deviceManager;
    juce::AudioSourcePlayer           m_sourcePlayer;
    class TimelineAudioSource;
    class LiveTimelineSource;
    std::unique_ptr<TimelineAudioSource> m_audioSource;
    std::unique_ptr<LiveTimelineSource>  m_liveSource;
    bool                              m_audioInited     { false };

public:
    struct PreparedClipBuffer {
        juce::AudioBuffer<float> buf;
        double                   sampleRate { 44100.0 };
        int                      version    { 0 };
    };
private:
    std::vector<std::shared_ptr<PreparedClipBuffer>> m_clipBufs;
    mutable std::mutex                m_renderModelMutex;
    std::atomic<bool>                 m_useLiveSource { false };
    std::unique_ptr<juce::ThreadPool> m_prepPool;
    mutable std::atomic<int>          m_clipPrepGen { 0 }; // version pour detecter prep stales
public:
    void  setLivePlaybackEnabled(bool on);
    bool  isLivePlaybackEnabled() const noexcept { return m_useLiveSource.load(); }

private:
    void prepareAudioOutput();
    void bouncePlaybackBuffer();

    std::shared_ptr<PreparedClipBuffer> prepareClipBufferSync(int clipIdx) const;
    // Enqueue une re-prep en background (idempotent : coalesce si en cours).
    void enqueueClipPrep(int clipIdx);
    // Re-prep all clips on the calling thread (used at play() in live mode).
    void rebuildAllClipBufsSync();
    void wireClipCallback(StudioClip& clip);
};

} // namespace BeatMate::UI::Studio
