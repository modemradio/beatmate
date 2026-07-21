#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../../models/Track.h"
#include "../../models/CuePoint.h"
#include "../../core/effects/CrossfadeEngine.h"
#include "../../core/audio/AudioPlayer.h"
#include "../../services/preparation/SetCompatibilityScorer.h"

namespace BeatMate::Core {
    class AudioEngine;
    class LiveSetRecorder;
    class RealtimePitchProcessor;
}

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {
    class LibraryBrowserPanel;
    class MiniWaveformInline;
    class PeakMeterDual;
    class GainReductionMeter;
    namespace Studio { class StudioTimeline; }
}

namespace BeatMate::UI {

class MixLabView : public juce::Component,
                     public juce::DragAndDropContainer,
                     public juce::KeyListener
{
public:
    class TimelineComponent;

    enum class TransitionType { Cut = 0, Fade, EQBlend, FilterSweep, EchoOut, Backspin, Slam };
    enum class CurveType { Linear = 0, EqualPower, SCurve, ConstantPower };
    // Shape applied at each end of a transition (independent of the curve preset).
    enum class EndShape { Soft = 0, Cut, Curve };
    enum class PitchMode { Vinyl = 0, RePitch, BeatSlice, FormantCorrection };
    enum class BeatGridMode { Manual = 0, Fixed, AI, AIFlex, Rekordbox };
    enum class TempoMode { Auto = 0, Manual, Fixed };

    enum class StudioTab {
        Zoom = 0, Playlist, Transition, Track, Video, Effects, Master, Samples
    };

    struct EQAutomationPoint
    {
        double time = 0.0;
        float value = 1.0f;
    };

    struct TempoPoint
    {
        double time = 0.0;
        double bpm = 120.0;
    };

    struct TransitionInfo
    {
        int indexA = -1, indexB = -1;
        TransitionType type = TransitionType::Fade;
        CurveType curve = CurveType::EqualPower;
        // Default = Beatmix 8 (Studio convention) — 8 bars sync au beat.
        int durationBars = 8;
        double durationSeconds = 0.0;
        // Sync auto BPM Aâ†”B sur la transition (Studio Synchronize Tempo).
        bool syncTempo = true;
        int compatibilityScore = 0;
        juce::Colour color;
        std::vector<EQAutomationPoint> eqHiPoints, eqMidPoints, eqLoPoints;
        std::vector<EQAutomationPoint> stemDrumsPoints, stemBassPoints, stemMelodyPoints, stemVocalsPoints;
        // Crossfade gain envelope: points (timeRel âˆˆ [0,1] over la transition,
        std::vector<EQAutomationPoint> crossfadePoints;
        // Studio-style end shape per side. Default Soft = legacy behavior.
        EndShape leftEnd  = EndShape::Soft;
        EndShape rightEnd = EndShape::Soft;
        // Mute par stem (Drums, Bass, Melody, Vocals) appliqué uniquement
        bool muteStems[4] = { false, false, false, false };
        Services::Preparation::SetCompatibilityScorer::CompatibilityResult fullResult;
    };

    class TimelineTrackBlock : public juce::Component
    {
    public:
        TimelineTrackBlock();
        explicit TimelineTrackBlock(const Models::Track& track);
        ~TimelineTrackBlock() override = default;

        const Models::Track& getTrack() const noexcept { return m_track; }
        void setTrack(const Models::Track& track) { m_track = track; repaint(); }

        double getStartTime() const noexcept { return m_startTime; }
        void setStartTime(double t) { m_startTime = t; }
        double getEndTime() const noexcept { return m_endTime; }
        void setEndTime(double t) { m_endTime = t; }

        double getSelectionStart() const noexcept { return m_selStart; }
        double getSelectionEnd() const noexcept { return m_selEnd; }
        bool hasSelection() const noexcept { return m_selEnd > m_selStart; }
        void clearSelection() { m_selStart = m_selEnd = 0; repaint(); }

        // Yellow handle on top for repositioning
        bool isHandleHovered() const { return m_handleHovered; }

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

        bool showStems = false;

        struct AutoPt { double time; float value; };
        std::vector<AutoPt> autoVolume;
        std::vector<AutoPt> autoEqHi, autoEqMid, autoEqLo;
        std::vector<AutoPt> autoFilter, autoPitch;
        std::vector<AutoPt> autoStemDrums, autoStemBass, autoStemMelody, autoStemVocals;

        bool showAutoVolume = true;
        bool showAutoEq = true;
        bool showAutoFilter = false;
        bool showAutoPitch = false;
        bool showAutoStems = false;

        void addAutomationPoint(int laneId, double time, float value);
        void clearAllAutomation();

        std::vector<Models::CuePoint> cuePoints;
        void setCuePoints(std::vector<Models::CuePoint> cues) {
            cuePoints = std::move(cues);
            repaint();
        }

        double loopInTime  = -1.0;
        double loopOutTime = -1.0;
        bool   loopEnabled = false;
        void setLoop(double inSec, double outSec, bool enabled = true) {
            loopInTime  = inSec;
            loopOutTime = outSec;
            loopEnabled = enabled;
            repaint();
        }
        void clearLoop() { loopInTime = loopOutTime = -1.0; loopEnabled = false; repaint(); }

        enum class FadeCurve { Linear, SCurve, Exp };
        double    fadeInSec    = 0.0;
        double    fadeOutSec   = 0.0;
        FadeCurve fadeInCurve  = FadeCurve::Linear;
        FadeCurve fadeOutCurve = FadeCurve::Linear;
        void setFadeIn (double sec, FadeCurve c = FadeCurve::Linear) { fadeInSec  = std::max(0.0, sec); fadeInCurve  = c; repaint(); }
        void setFadeOut(double sec, FadeCurve c = FadeCurve::Linear) { fadeOutSec = std::max(0.0, sec); fadeOutCurve = c; repaint(); }
        // Retourne le multiplicateur de volume 0..1
        float evalFadeAt(double offsetSec) const;

        // Offset dans le fichier source quand le bloc est un slice trim
        double getAudioStartSec() const noexcept { return m_audioStartSec; }
        double getAudioEndSec()   const noexcept { return m_audioEndSec;   }
        void setAudioRange(double start, double end) {
            m_audioStartSec = start;
            m_audioEndSec   = end;
            // Update the track duration so paint/layout reflect the slice.
            m_track.duration = end - start;
            repaint();
        }

        std::vector<double> beatGridPositions;   // all beats
        std::vector<double> beatGridBarPositions; // downbeats only
        int beatGridMode = 2; // 0=Manual 1=Fixed 2=AI 3=AIFlex 4=Rekordbox (mirror)
        bool beatGridIsVariable = false;         // AIFlex hint
        int manualBeatDragIndex = -1;

        std::function<void()> onClipChanged;

        enum class DragMode { None, MoveClip, ResizeLeft, ResizeRight,
                              FadeInHandle, FadeOutHandle, SelectRange,
                              ManualBeat };

    private:
        DragMode pickDragMode(juce::Point<int> p) const;

        Models::Track m_track;
        double m_startTime = 0.0, m_endTime = 0.0;
        double m_selStart = 0, m_selEnd = 0;
        double m_audioStartSec = 0.0;
        double m_audioEndSec   = 0.0;  // 0 = use full track.duration
        bool m_dragging = false, m_selecting = false, m_handleHovered = false;
        int m_dragStartX = 0;

        DragMode m_dragMode = DragMode::None;
        double   m_dragStartTime = 0.0;
        double   m_dragStartAudioStart = 0.0;
        double   m_dragStartAudioEnd   = 0.0;
        double   m_dragStartFadeIn     = 0.0;
        double   m_dragStartFadeOut    = 0.0;
        juce::Point<int> m_dragOrigin;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineTrackBlock)
    };

    class NavigationBar : public juce::Component
    {
    public:
        NavigationBar();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;

        void setTracks(const std::vector<TimelineTrackBlock*>& blocks) { m_blocks = blocks; repaint(); }
        void setPlayhead(double pos) { m_playheadPos = pos; repaint(); }
        void setViewport(double startSec, double endSec) { m_viewStart = startSec; m_viewEnd = endSec; repaint(); }

        std::function<void(double)> onSeek;
        std::function<void(double, double)> onZoom;

    private:
        std::vector<TimelineTrackBlock*> m_blocks;
        double m_playheadPos = 0;
        double m_viewStart = 0, m_viewEnd = 0;
        bool m_draggingView = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavigationBar)
    };

    class TempoLane : public juce::Component
    {
    public:
        TempoLane();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp  (const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

        void setMode(TempoMode mode) { m_mode = mode; repaint(); }
        TempoMode getMode() const { return m_mode; }

        void addPoint(double time, double bpm);
        void removePoint(int index);
        const std::vector<TempoPoint>& getPoints() const { return m_points; }

        std::function<void()> onChanged;

    private:
        int hitPoint(int x, int y, int hitRadiusPx = 8) const;

        struct PointPx { float x, y; };
        PointPx pointToPx(double time, double bpm) const;
        bool    pxToPoint(int x, int y, double& outTime, double& outBpm) const;

        TempoMode m_mode = TempoMode::Auto;
        std::vector<TempoPoint> m_points;
        int m_draggedPoint = -1;
        double m_minBpm = 60, m_maxBpm = 200;
        double m_visibleSeconds = 600.0;  // 10 min visible window

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoLane)
    };

    class MixerStrip : public juce::Component
    {
    public:
        MixerStrip(const juce::String& label);
        void paint(juce::Graphics& g) override;
        void resized() override;

        std::unique_ptr<juce::Slider> m_eqHiKnob, m_eqMidKnob, m_eqLoKnob;
        std::unique_ptr<juce::Slider> m_filterKnob;  // Hybrid Low/High cut
        std::unique_ptr<juce::Slider> m_volumeFader;
        std::unique_ptr<juce::TextButton> m_soloBtn, m_muteBtn, m_autoBtn;

        bool isSolo = false, isMuted = false, isAutoBypass = false;

    private:
        juce::String m_label;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerStrip)
    };

    class TimelineComponent : public juce::Component
    {
    public:
        TimelineComponent();
        ~TimelineComponent() override = default;

        void addTrack(const Models::Track& track);
        void removeTrack(int index);
        void reorderTrack(int from, int to);
        void insertBlock(int index, std::unique_ptr<TimelineTrackBlock> block);
        int getTrackCount() const { return static_cast<int>(m_blocks.size()); }
        TimelineTrackBlock& getBlock(int i) { return *m_blocks[i]; }
        TimelineTrackBlock* tryGetBlock(int i) const noexcept {
            return (i >= 0 && i < (int)m_blocks.size()) ? m_blocks[i].get() : nullptr;
        }

        double getPlayheadPosition() const noexcept { return m_playheadPos; }
        void setPlayheadPosition(double seconds) { m_playheadPos = seconds; repaint(); }

        double getZoomLevel() const noexcept { return m_zoomLevel; }
        void setZoomLevel(double z) { m_zoomLevel = juce::jlimit(0.1, 20.0, z); resized(); }

        const std::vector<TransitionInfo>& getTransitions() const noexcept { return m_transitions; }
        std::vector<TransitionInfo>& getTransitions() { return m_transitions; }

        void deleteSelection();
        void copySelection();
        void pasteSelection();
        void splitAtPlayhead();

        void goToStart();
        void goToEnd();
        void nudgePlayhead(double deltaSeconds);
        void gotoNextTransition();
        void gotoPrevTransition();
        void toggleCenterPlayhead() { m_centerPlayheadMode = !m_centerPlayheadMode; repaint(); }
        bool isCenterPlayheadMode() const noexcept { return m_centerPlayheadMode; }

        void setShowBeatGrid(bool on)  { showBeatGrid  = on; repaint(); }
        void setShowStems(bool on)     { showStems    = on; for (auto& b : m_blocks) b->showStems = showStems; resized(); }
        void setShowCuePoints(bool on) { m_showCuePoints = on; repaint(); }
        bool showingCuePoints() const noexcept { return m_showCuePoints; }

        int  quantizeAllClipsToBeat();

        enum class SnapMode { Off, Beat, Bar, HalfBar, Sixteenth };
        void setSnapMode(SnapMode m) { m_snapMode = m; }
        SnapMode getSnapMode() const noexcept { return m_snapMode; }

        // Ctrl+clic bascule, Shift+clic étend la plage
        const std::vector<int>& getMultiSelection() const noexcept { return m_multiSelection; }
        void clearMultiSelection() { m_multiSelection.clear(); repaint(); }
        void addToMultiSelection(int idx);
        void removeFromMultiSelection(int idx);
        bool isInMultiSelection(int idx) const;

        void setSelectedBlock(int index);
        int  getSelectedBlock() const noexcept { return m_selectedBlock; }

        bool showBeatGrid = true;

        bool showStems = false;
        void toggleStemsView() { showStems = !showStems; for (auto& b : m_blocks) b->showStems = showStems; resized(); }

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

        void paintTransitions(juce::Graphics& g) const;
        void paintBeatGrid(juce::Graphics& g) const;
        void handleContentClick(const juce::MouseEvent& e);

        class Listener
        {
        public:
            virtual ~Listener() = default;
            virtual void trackClicked(int trackIndex) {}
            virtual void transitionClicked(int transitionIndex) {}
            virtual void playheadMoved(double newPositionSeconds) {}
        };
        void addListener(Listener* l) { m_listeners.add(l); }
        void removeListener(Listener* l) { m_listeners.remove(l); }

        int m_selectedBlock = -1;
        int m_selectedTransition = -1;
        // Hover index for transition drag handles (-1 = none).
        int m_hoverTransitionHandle = -1;

    private:
        std::vector<std::unique_ptr<TimelineTrackBlock>> m_blocks;
        std::vector<TransitionInfo> m_transitions;
        std::vector<float> m_clipboard;

        double m_playheadPos = 0.0;
        // Zoom par défaut généreux : un track de 4 min ≈ 7200 px
        double m_zoomLevel = 3.0;
        double m_scrollOffset = 0.0;

        bool                m_centerPlayheadMode = false;
        bool                m_showCuePoints      = true;

        SnapMode            m_snapMode = SnapMode::Beat;
        std::vector<int>    m_multiSelection;
        juce::Rectangle<int> m_rubberBand;
        bool                 m_rubberBanding = false;

        std::unique_ptr<juce::Viewport> m_viewport;
        juce::ListenerList<Listener> m_listeners;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
    };

    class TransportBar : public juce::Component
    {
    public:
        TransportBar();
        void paint(juce::Graphics& g) override;
        void resized() override;

        std::unique_ptr<juce::TextButton> m_playBtn, m_replayTransBtn, m_prevTransBtn, m_nextTransBtn;
        std::unique_ptr<juce::TextButton> m_soloBtn, m_metronomeBtn;
        std::unique_ptr<juce::TextButton> m_addTracksBtn, m_harmonizeBtn;
        std::unique_ptr<juce::TextButton> m_mashupAIBtn, m_megamixAIBtn, m_medleyAIBtn;
        std::unique_ptr<juce::TextButton> m_undoBtn, m_redoBtn;
        std::unique_ptr<juce::TextButton> m_splitBtn, m_centerPlayheadBtn;
        std::unique_ptr<juce::TextButton> m_zoomInBtn, m_zoomOutBtn;
        std::unique_ptr<juce::TextButton> m_liveBtn;   // toggle LiveTimelineSource

        std::function<void()> onPlay, onReplayTrans, onPrevTrans, onNextTrans;
        std::function<void()> onSolo, onMetronome, onAddTracks, onHarmonize;
        std::function<void()> onMashupAI, onMegamixAI, onMedleyAI, onRemixAI;
        std::function<void()> onUndo, onRedo, onSplit, onCenterPlayhead, onZoomIn, onZoomOut;
        std::function<void()> onToggleLive;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
    };

    class StudioTabs : public juce::TabbedComponent
    {
    public:
        StudioTabs();
        void currentTabChanged(int newCurrentTabIndex, const juce::String& newCurrentTabName) override;

        std::function<void(StudioTab)> onTabChanged;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioTabs)
    };

    class TrackInfoPanel : public juce::Component
    {
    public:
        TrackInfoPanel();
        void paint(juce::Graphics& g) override;
        void resized() override;

        void loadTrack(const Models::Track& track);

        std::unique_ptr<juce::TextEditor> m_titleEdit, m_artistEdit, m_genreEdit, m_notesEdit;
        std::unique_ptr<juce::ComboBox> m_pitchModeCombo, m_beatGridModeCombo;
        std::unique_ptr<juce::ToggleButton> m_stabilizeTempoCheck;
        std::unique_ptr<juce::Slider> m_ratingSlider;

        std::unique_ptr<juce::Slider>       m_stemFaders[4];
        std::unique_ptr<juce::ToggleButton> m_stemMute[4];
        std::unique_ptr<juce::ToggleButton> m_stemSolo[4];
        std::function<void(int stemIndex, float volume)> onStemVolume;
        std::function<void(int stemIndex, bool muted)>   onStemMute;
        std::function<void(int stemIndex, bool soloed)>  onStemSolo;

    private:
        Models::Track m_track;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackInfoPanel)
    };

    class EffectsPanel : public juce::Component
    {
    public:
        EffectsPanel();
        void paint(juce::Graphics& g) override;
        void resized() override;

        std::unique_ptr<juce::Slider> m_gainKnob, m_compKnob;
        std::unique_ptr<juce::Slider> m_eqHi, m_eqMid, m_eqLo;
        std::unique_ptr<juce::Slider> m_pitchSlider, m_hpfKnob, m_lpfKnob, m_qKnob;
        std::unique_ptr<juce::Slider> m_echoKnob, m_reverbKnob;
        std::unique_ptr<juce::ComboBox> m_fxPackCombo;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
    };

    class MasterPanel : public juce::Component, private juce::Timer
    {
    public:
        MasterPanel();
        ~MasterPanel() override;
        void paint(juce::Graphics& g) override;
        void resized() override;

        void setStudioTimeline(Studio::StudioTimeline* tl) noexcept;

        // Câblé dans le ctor sur AudioEngine::setMasterVolume
        std::unique_ptr<juce::Slider> m_masterGainSlider;
        std::unique_ptr<juce::Slider> m_limiterSlider, m_compressorSlider;
        std::vector<float> m_spectrumData;

        std::unique_ptr<PeakMeterDual>      m_peakMeter;
        std::unique_ptr<GainReductionMeter> m_grMeter;

    private:
        void timerCallback() override;
        Studio::StudioTimeline* m_studioTimeline { nullptr };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterPanel)
    };

    class StudioToolbar : public juce::Component
    {
    public:
        StudioToolbar();
        void paint(juce::Graphics& g) override;
        void resized() override;

        void setBPMLabel(double bpm);
        void setKeyLabel(const juce::String& key);
        void setTotalDurationLabel(double seconds);

        std::function<void()> onNew, onOpen, onSave, onExport, onRecord, onUndo, onRedo;
        std::function<void()> onCut, onCopy, onPaste, onSplit, onDelete;
        std::function<void()> onToggleStems, onGenerateStems, onImportXml;
        std::function<void()> onQuantizeAll;
        std::function<void()> onAiMix;
        std::function<void()> onMashup, onMegamix, onMedley, onRemix;
        std::function<void()> onZoomOut, onZoomIn, onFit;
        std::function<void(int)> onSnapModeChanged;  // 0=Off 1=Beat 2=Bar 3=HalfBar 4=Sixteenth

        std::unique_ptr<juce::TextButton> m_newBtn, m_openBtn, m_saveBtn, m_exportBtn;
        std::unique_ptr<juce::TextButton> m_undoBtn, m_redoBtn;
        std::unique_ptr<juce::TextButton> m_cutBtn, m_copyBtn, m_pasteBtn, m_splitBtn, m_delBtn;
        std::unique_ptr<juce::ComboBox>   m_snapCombo;
        std::unique_ptr<juce::TextButton> m_quantizeBtn;
        std::unique_ptr<juce::TextButton> m_aiMixBtn, m_stemsGenBtn;
        std::unique_ptr<juce::TextButton> m_mashupBtn, m_megamixBtn, m_medleyBtn, m_remixBtn;
        std::unique_ptr<juce::TextButton> m_zoomOutBtn, m_zoomInBtn, m_fitBtn, m_stemsToggleBtn;
        std::unique_ptr<juce::Label>      m_bpmLabel, m_keyLabel, m_totalDurationLabel;
        std::unique_ptr<juce::TextButton> m_recordBtn;
        // caché — import XML Rekordbox déplacé dans Réglages > DJ Software
        std::unique_ptr<juce::TextButton> m_xmlImportBtn;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioToolbar)
    };

    MixLabView();
    explicit MixLabView(Services::Library::TrackDataProvider* provider);
    ~MixLabView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* originating) override;

    juce::UndoManager m_undoManager;

    struct BlockClipboardEntry {
        Models::Track track;
        // Si selEnd > selStart, un slice a été copié
        double selStart        { 0.0 };
        double selEnd          { 0.0 };
        double startRelToFirst { 0.0 };  // pour multi-block, decalage rel/anchor
        double fadeInSec       { 0.0 };
        double fadeOutSec      { 0.0 };
        int    fadeInCurve     { 0 };
        int    fadeOutCurve    { 0 };
        double loopInTime      { -1.0 };
        double loopOutTime     { -1.0 };
        bool   loopEnabled     { false };
        std::vector<Models::CuePoint> cuePoints;
        std::vector<double> beatGridPositions;
        std::vector<double> beatGridBarPositions;
        int  beatGridMode { 2 };
        bool showStems    { false };
        std::vector<std::pair<double, float>> autoVolume;
        std::vector<std::pair<double, float>> autoEqHi, autoEqMid, autoEqLo;
        std::vector<std::pair<double, float>> autoFilter, autoPitch;
        std::vector<std::pair<double, float>> autoStemDrums, autoStemBass,
                                              autoStemMelody, autoStemVocals;
    };
    static std::vector<BlockClipboardEntry> s_blockClipboard;
    // Compat anciens callers — laisser declares mais non utilises.
    static std::vector<float> s_clipboard;
    static double s_clipboardDuration;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void mixExportRequested() {}
        virtual void trackPreviewRequested(const juce::String&, const juce::String&, const juce::String&) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onNewProject();
    void onSaveProject();
    void onExportMix();
    void onExportAbleton();
    void onExportPackage(const juce::String& platformName);
    void onRecord();
    void onAddTrack(const Models::Track& track);
    void onHarmonize();

    std::function<void(const juce::String&, const juce::String&, const juce::String&)> onTrackPreview;

private:
    void setupUI();
    void updateToolbarInfo();

    std::unique_ptr<StudioToolbar> m_toolbar;
    std::unique_ptr<NavigationBar> m_navBar;
    std::unique_ptr<TransportBar> m_transportBar;
    std::unique_ptr<TimelineComponent> m_timeline;
    std::unique_ptr<Studio::StudioTimeline> m_studioTimeline;

    class ClipInspectorPanel;
    std::unique_ptr<ClipInspectorPanel> m_inspector;
    std::unique_ptr<TempoLane> m_tempoLane;
    std::unique_ptr<MixerStrip> m_mixerA, m_mixerB;
    std::unique_ptr<StudioTabs> m_tabs;
    std::unique_ptr<TrackInfoPanel> m_trackInfoPanel;
    std::unique_ptr<EffectsPanel> m_effectsPanel;
    std::unique_ptr<MasterPanel> m_masterPanel;
    std::unique_ptr<LibraryBrowserPanel> m_libraryBrowser;

    // Adapters possédés — lifetime lié à la vue
    struct ListenerAdapters;
    std::unique_ptr<ListenerAdapters> m_listenerAdapters;

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::vector<Models::Track> m_projectTracks;
    juce::ListenerList<Listener> m_listeners;

    std::unique_ptr<Core::LiveSetRecorder> m_recorder;
    juce::String m_lastRecordingPath;

    enum class PlaybackState { Stopped, Playing, Paused, Seeking };
    std::atomic<PlaybackState> m_playbackState{PlaybackState::Stopped};

    std::unique_ptr<Core::AudioPlayer> m_player;
    std::unique_ptr<Core::AudioPlayer> m_playerB;

    std::unique_ptr<Core::RealtimePitchProcessor> m_pitchProcessor;
    std::atomic<PitchMode> m_pitchMode{PitchMode::Vinyl};
    std::atomic<double> m_pitchSemitones{0.0};
    void applyPitchModeFromUI();

    // Non-owning : AudioEngine sur lequel notre callback est enregistré
    Core::AudioEngine* m_audioEngine = nullptr;

    int m_currentPlayingBlock = -1;  // -1 = stoppé

    // Flag legacy conservé pour le câblage transport existant
    bool m_isPlaying = false;

    void playMix();
    void stopMix();
    void togglePlayPause();

    bool loadBlockIntoPlayer(int idx, double offsetSeconds);

    void onBlockFinished();

    std::atomic<bool>   m_crossfading{false};
    std::atomic<double> m_crossfadeStartPosA{0.0};
    std::atomic<double> m_crossfadeDurSec{0.0};
    std::atomic<int>    m_crossfadeCurve{0}; // CurveType ord
    double computeTransitionDurationSec(int blockIdx) const;
    bool   startCrossfade(int nextBlockIdx, double posSec);
    void   updateCrossfadeGains(double posSec);
    void   finalizeCrossfade();
    // Calcul de gain pur, partagé avec le callback audio (sample accurate)
    void   computeXfadeGains(double t, float& gA, float& gB) const;
    // Appelé depuis le callback position du player (dispatché UI-thread)
    void   handlePlaybackTick(double posSec);

    // UI thread, ~30 ms
    void syncPlayheadFromPlayer();

    void regenerateBeatGrid(int blockIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixLabView)
};

} // namespace BeatMate::UI
