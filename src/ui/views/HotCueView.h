#pragma once
#include <atomic>
#include <memory>
#include <functional>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../../models/Track.h"
#include "../../core/cue/IntelligentCueCreator.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI { class LibraryMirrorWindow; }

namespace BeatMate::UI {

class WaveformWidget;
class CuePadGrid;

namespace CueColors {
    inline const juce::Colour kPalette[16] = {
        juce::Colour(0xFFEF4444),
        juce::Colour(0xFFF97316),
        juce::Colour(0xFFF59E0B),
        juce::Colour(0xFF22C55E),
        juce::Colour(0xFF06B6D4),
        juce::Colour(0xFF3B82F6),
        juce::Colour(0xFF8B5CF6),
        juce::Colour(0xFFEC4899),
        juce::Colour(0xFFF1F5F9),
        juce::Colour(0xFFFF6B6B),
        juce::Colour(0xFFFFB347),
        juce::Colour(0xFFFDE68A),
        juce::Colour(0xFF6EE7B7),
        juce::Colour(0xFF67E8F9),
        juce::Colour(0xFF93C5FD),
        juce::Colour(0xFFC084FC),
    };
    inline const char* kNames[16] = {
        "Rouge", "Orange", "Jaune", "Vert",
        "Cyan", "Bleu", "Violet", "Rose",
        "Blanc", "Rouge clair", "Orange clair", "Jaune clair",
        "Vert clair", "Cyan clair", "Bleu clair", "Violet clair"
    };
    inline const char* kDefaultCueNames[8] = {
        "Intro", "Verse", "Chorus", "Build",
        "Drop", "Bridge", "Outro", "End"
    };
}

class HotCueView : public juce::Component,
                   public juce::Timer,
                   public BeatMate::UI::IRetranslatable {
public:
    HotCueView();
    explicit HotCueView(Services::Library::TrackDataProvider* provider);
    ~HotCueView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;

    void retranslateUi() override;

    void setTrack(const juce::String& title, const juce::String& artist);
    void setTrackDuration(double durationSeconds);
    void setWaveformData(const std::vector<float>& peaks);
    void setBeatGrid(const std::vector<double>& beats);

    std::function<void(double positionSeconds)> onPreviewCue;
    std::function<void()> onStopPreview;
    std::function<void(const juce::String& filePath)> onLoadTrack;
    std::function<void()> onPlayPause;
    std::function<void(double seconds)> onSeek;
    std::function<double()> onGetPosition;
    std::function<double()> onGetDuration;

    int64_t getCurrentTrackId() const { return m_currentTrackId; }

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void cuePointSet(int number, double positionSeconds) {}
        virtual void cuePointRemoved(int number) {}
        virtual void cuePointColorChanged(int number, juce::Colour color) {}
        virtual void cuePointRenamed(int number, const juce::String& name) {}
        virtual void autoGenerateRequested() {}
        virtual void clearAllCuesRequested() {}
        virtual void exportCuesRequested(const juce::String& targetFormat) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    struct CueData {
        bool active = false;
        double positionSeconds = 0.0;
        juce::Colour color;
        juce::String name;
        int colorIndex = 0;
    };
    std::array<CueData, 8> m_cues;

    struct TrackEntry {
        int64_t id = 0;
        juce::String title;
        juce::String artist;
        juce::String bpm;
        juce::String key;
        juce::String genre;
        bool hasCues = false;
        int cueCount = 0;
        std::string filePath;
    };

    class TrackListModel : public juce::ListBoxModel {
    public:
        std::vector<TrackEntry> entries;
        std::function<void(int row)> onRowSelected;

        int getNumRows() override { return static_cast<int>(entries.size()); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    };

    void setupUI();
    void initCueDefaults();
    void populateTrackList();
    void filterTrackList();
    void loadTrackByIndex(int index);
    void loadTrackEntry(const TrackEntry& entry);
public:
    void loadTrackById(int64_t id);
private:

    void setCueAtPosition(int index, double positionSeconds);
    void persistCuesToDatabase();
    void removeCue(int index);
    void updateCueButton(int index);
    double snapToNearestBeat(double positionSeconds) const;
    juce::String formatTime(double seconds) const;

    void onPadPress(int index);
    void onPadRelease(int index);
    void showCueEditPopover(int index);
    void showCueEditPopover(int index, juce::Rectangle<int> screenAnchor);
    void selectCue(int index);
    void nudgeCue(int index, double deltaSeconds);
    void centerCueInView();
    void renameCue(int index, const juce::String& name);
    void applyCueColor(int index, int paletteIndex);
    void snapCueToTransient(int index);
    void setQuantizeEnabled(bool enabled);
    void updateNudgeButtons();
    void loadBeatGridForCurrentTrack();
    void pushBeatGridToWaveform();
    void requestWaveformFromService(const std::string& filePath, bool suppressPartials);

    void onWaveformClicked(double normalizedPosition);
    void updateWaveformCueMarkers();

    struct WaveformClickForwarder;
    std::unique_ptr<WaveformClickForwarder> m_waveformClickForwarder;

    int m_previewingCue = -1;
    double m_previewStartTime = 0.0;
    static constexpr double kPreviewDuration = 5.0;

    bool m_holdPreview = false;
    double m_holdReturnPosition = 0.0;
    bool m_wasPlayingBeforeHold = false;

    double m_playheadPosition = 0.0;
    double m_lastPolledPos = -1.0;
    double m_loadWallTime = 0.0;
    bool m_advanceLogged = false;

    double m_trackDuration = 0.0;
    double m_trackBpm = 0.0;
    std::vector<double> m_beatGridPositions;
    std::string m_currentTrackPath;
    int64_t m_currentTrackId = 0;
    bool m_isPlaying = false;
    bool m_trialLimitReached = false;
    double m_trialPlaybackSeconds = 0.0;

    int m_selectedCue = -1;
    bool m_quantizeEnabled = true;

    std::vector<Models::Track> m_allTracksCache;
    std::set<int64_t> m_cueStatusCache;
    std::map<int64_t, int> m_cueCountCache;
    bool m_cueStatusCacheLoaded = false;

    std::unique_ptr<juce::Label> m_titleLabel;

    std::unique_ptr<juce::TextEditor> m_searchEditor;
    std::unique_ptr<juce::ComboBox> m_genreFilter;
    std::unique_ptr<juce::ComboBox> m_cueStatusFilter;
    std::unique_ptr<TrackListModel> m_trackListModel;
    std::unique_ptr<juce::ListBox> m_trackListBox;

    class FolderTreeItem : public juce::TreeViewItem
    {
    public:
        FolderTreeItem(const juce::String& name, const juce::String& fullPath, HotCueView& owner);
        bool mightContainSubItems() override { return getNumSubItems() > 0; }
        void paintItem(juce::Graphics& g, int width, int height) override;
        void itemClicked(const juce::MouseEvent&) override;
        juce::String getUniqueName() const override { return m_fullPath; }
        void addSubFolder(std::unique_ptr<FolderTreeItem> child);
        int m_trackCount = 0;
        int m_cueCount = 0;
    private:
        juce::String m_name;
        juce::String m_fullPath;
        HotCueView& m_owner;
    };
    std::unique_ptr<juce::TreeView> m_folderTree;
    std::unique_ptr<FolderTreeItem> m_rootTreeItem;
    std::unique_ptr<juce::TextButton> m_tabListBtn;
    std::unique_ptr<juce::TextButton> m_tabFolderBtn;
    std::unique_ptr<juce::TextButton> m_libraryPopupBtn;
    bool m_showFolderTree = false;
    void buildFolderTree();
    void toggleLibraryMirror();
    void filterByFolder(const juce::String& folderPath);
    void switchBrowserTab(bool showFolders);

    std::unique_ptr<juce::TextButton> m_playPauseBtn;

    std::unique_ptr<juce::Label> m_trackTitle, m_trackArtist;

    std::unique_ptr<WaveformWidget> m_waveform;

    std::unique_ptr<juce::TextButton> m_quantizeBtn;
    std::unique_ptr<juce::Slider> m_zoomSlider;
    std::unique_ptr<juce::Label> m_zoomLabel;

    std::unique_ptr<juce::Slider> m_waveformScrollBar;

    std::unique_ptr<juce::Label> m_positionLabel;

    std::unique_ptr<CuePadGrid> m_padGrid;
    std::array<std::unique_ptr<juce::TextButton>, 4> m_nudgeBtns;

    struct CueDetailRow {
        std::unique_ptr<juce::TextEditor> nameEditor;
        std::unique_ptr<juce::Label> positionLabel;
    };
    std::array<CueDetailRow, 8> m_cueRows;

    std::unique_ptr<juce::TextButton> m_autoGenerateBtn;
    std::unique_ptr<juce::TextButton> m_clearAllBtn;
    std::unique_ptr<juce::ComboBox> m_exportTargetCombo;
    std::unique_ptr<juce::TextButton> m_exportBtn;

    std::shared_ptr<std::atomic<bool>> m_aliveFlag = std::make_shared<std::atomic<bool>>(true);

    class AutoGenerateThread : public juce::Thread
    {
    public:
        AutoGenerateThread(HotCueView& owner, const std::string& path)
            : juce::Thread("AutoGenerate"), m_owner(owner), m_path(path) {}
        void run() override;
    private:
        HotCueView& m_owner;
        std::string m_path;
    };

    std::unique_ptr<AutoGenerateThread> m_autoGenThread;
    void applyAutoGenerateResult(const Core::IntelligentCueResult& result);

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    std::unique_ptr<LibraryMirrorWindow> m_libraryMirror;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HotCueView)
};

} // namespace BeatMate::UI
