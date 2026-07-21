#pragma once
#include <memory>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../../models/Track.h"
#include "../../models/Playlist.h"
#include "../../services/preparation/SetCompatibilityScorer.h"
#include "../../services/library/SmartPlaylistEngine.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../widgets/smartplaylist/NestedSmartRuleEditor.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {

class PlaylistPreparationView : public juce::Component,
                                 public juce::DragAndDropContainer,
                                 public BeatMate::UI::IRetranslatable
{
public:
    PlaylistPreparationView();
    explicit PlaylistPreparationView(Services::Library::TrackDataProvider* provider);
    ~PlaylistPreparationView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void retranslateUi() override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void playlistExportRequested(const juce::String& format) {}
        virtual void playlistSaved() {}
        virtual void trackPreviewRequested(const juce::String& filePath) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onNewPlaylist();
    void onOpenPlaylist();
    void onSavePlaylist();
    void onDeletePlaylist();
    void onDuplicatePlaylist();
    void onAutoSort(int mode);
    void onFillGaps();
    void onExport(const juce::String& format);

    void onSmartPlaylistBuild();
    void onIAAutoBuild();

private:
    struct TrackInfo;
    class SmartRuleEditor;
    class IABuildDialog;
    void setupUI();
    void updatePlaylistScore();
    void updateCompatibilityIndicators();
    void updateVisualization();
    void handleLibraryDrop(const juce::String& trackIdsStr);
    void addTrackToPlaylist(const Models::Track& track);

    enum class SortColumn { Number = 0, Title, Artist, BPM, Key, Energy, Duration, Score, Count };
    SortColumn m_sortColumn = SortColumn::Title;
    bool m_sortAscending = true;
    void sortByColumn(SortColumn col);
    void handleColumnHeaderClick(int columnIndex);
    void refreshPlaylistList();
    void reloadPlaylistsFromDb();
    void selectPlaylistByDbId(int64_t dbId);
    void persistCurrentOrder();
    Models::Track toModelTrack(const TrackInfo& ti) const;
    std::vector<int64_t> currentTrackIds() const;
    void onSuggestAI();
    void onSendToDJ();
    void onFillPlaylist();
    void loadPlaylistTracks(int index);
    void writeM3U(const juce::File& file);
    void writePLS(const juce::File& file);
    void writeJSON(const juce::File& file);
    void writePDF(const juce::File& file);

    class PlaylistListModel : public juce::ListBoxModel
    {
    public:
        juce::StringArray playlistNames;
        std::vector<bool> isSmartFlags;
        int selectedIndex = -1;
        std::function<void(int)> onSelectionChanged;
        int getNumRows() override { return playlistNames.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override {
            if (row >= 0 && row < playlistNames.size()) {
                selectedIndex = row;
                if (onSelectionChanged) onSelectionChanged(row);
            }
        }
    };

    struct TrackInfo;
    class SmartRuleEditor;
    class IABuildDialog;

    class TrackListModel : public juce::ListBoxModel
    {
    public:
        std::vector<TrackInfo>* tracks = nullptr;
        PlaylistPreparationView* owner = nullptr;
        int getNumRows() override { return tracks ? static_cast<int>(tracks->size()) : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
        juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    };

    class DragDropTrackListBox : public juce::ListBox,
                                  public juce::DragAndDropTarget
    {
    public:
        DragDropTrackListBox(const juce::String& name, juce::ListBoxModel* model)
            : juce::ListBox(name, model) {}

        void mouseDrag(const juce::MouseEvent& e) override;
        bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override { return true; }
        void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;
        void itemDragEnter(const juce::DragAndDropTarget::SourceDetails&) override {}
        void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override;
        void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override;
        bool shouldDrawDragImageWhenOver() override { return true; }
        void paint(juce::Graphics& g) override;

        std::function<void(int from, int to)> onReorder;
        std::function<void(const juce::String& trackIdsStr)> onExternalDrop;
        int dragInsertIndex = -1;
        int dragSourceRow = -1;
    };

    class CompatibilityBadge : public juce::Component
    {
    public:
        enum Level { Good, Warning, Bad };
        Level level = Good;
        int percent = 100;
        juce::String hint;
        void paint(juce::Graphics& g) override;
    };

    class PlaylistScoreDisplay : public juce::Component
    {
    public:
        int score = 0;
        int bpmScore = 0, keyScore = 0, energyScore = 0, diversityScore = 0;
        juce::String suggestion;
        void paint(juce::Graphics& g) override;
    };

    class PlaylistVisualizationToggle : public juce::Component
    {
    public:
        enum Mode { ListView, GraphView, ScatterView };
        Mode currentMode = ListView;
        std::function<void(Mode)> onModeChanged;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
    };

    class AdvancedSmartRuleEditor : public juce::Component
    {
    public:
        struct RuleRow {
            std::unique_ptr<juce::ComboBox> fieldCombo;
            std::unique_ptr<juce::ComboBox> opCombo;
            std::unique_ptr<juce::TextEditor> valueEdit;
            std::unique_ptr<juce::TextEditor> value2Edit;
            std::unique_ptr<juce::TextButton> removeBtn;
        };
        juce::OwnedArray<juce::Component> allChildren;
        std::vector<std::unique_ptr<RuleRow>> rules;
        std::unique_ptr<juce::TextButton> addRuleBtn;
        std::unique_ptr<juce::TextButton> applyBtn;
        std::unique_ptr<juce::Label> titleLabel;
        std::unique_ptr<juce::Label> matchCountLabel;
        std::unique_ptr<juce::ToggleButton> conjunctionToggle;
        std::function<void(std::vector<Models::SmartPlaylistRule>)> onApply;

        AdvancedSmartRuleEditor();
        void resized() override;
        void paint(juce::Graphics& g) override;
        void addRule();
        void removeRule(int index);
        void applyRules();
        void updateMatchCount();

    private:
        void populateFieldCombo(juce::ComboBox& combo);
        void populateOpCombo(juce::ComboBox& combo);
    };

    class AdvancedIABuildDialog : public juce::Component
    {
    public:
        std::unique_ptr<juce::Label> titleLabel;

        std::unique_ptr<juce::Slider> bpmWeight;
        std::unique_ptr<juce::Slider> keyWeight;
        std::unique_ptr<juce::Slider> energyWeight;
        std::unique_ptr<juce::Slider> genreWeight;

        std::unique_ptr<juce::Slider> startEnergy;
        std::unique_ptr<juce::Slider> peakEnergy;
        std::unique_ptr<juce::Slider> endEnergy;

        std::unique_ptr<juce::Label> bpmWeightLabel;
        std::unique_ptr<juce::Label> keyWeightLabel;
        std::unique_ptr<juce::Label> energyWeightLabel;
        std::unique_ptr<juce::Label> genreWeightLabel;
        std::unique_ptr<juce::Label> startEnergyLabel;
        std::unique_ptr<juce::Label> peakEnergyLabel;
        std::unique_ptr<juce::Label> endEnergyLabel;

        std::unique_ptr<juce::TextButton> buildBtn;
        std::unique_ptr<juce::TextButton> cancelBtn;

        struct BuildSettings {
            float bpmWeight = 0.5f;
            float keyWeight = 0.5f;
            float energyWeight = 0.5f;
            float genreWeight = 0.5f;
            float startEnergy = 0.5f;
            float peakEnergy = 0.8f;
            float endEnergy = 0.4f;
        };
        std::function<void(BuildSettings)> onBuild;

        AdvancedIABuildDialog();
        void resized() override;
        void paint(juce::Graphics& g) override;
    };


    std::vector<Models::Playlist> m_dbPlaylists;
    int64_t m_currentPlaylistId = -1;
    int m_currentPlaylistIndex = -1;

    std::vector<Models::Track> m_currentTracks;

    bool m_exporting = false;

    mutable Services::Preparation::SetCompatibilityScorer m_scorer;


    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_newBtn, m_openBtn, m_saveBtn;
    std::unique_ptr<juce::TextButton> m_deleteBtn, m_duplicateBtn;
    std::unique_ptr<juce::TextButton> m_iaBuildBtn, m_smartPlaylistBtn;
    std::unique_ptr<juce::TextButton> m_addTracksBtn, m_removeTracksBtn;
    std::unique_ptr<juce::TextButton> m_suggestBtn, m_fillBtn, m_sendDJBtn;
    std::unique_ptr<juce::TextButton> m_exportXSPFBtn;

    std::unique_ptr<juce::ListBox> m_playlistList;
    std::unique_ptr<PlaylistListModel> m_playlistListModel;

    std::unique_ptr<DragDropTrackListBox> m_trackList;
    std::unique_ptr<TrackListModel> m_trackListModel;
    juce::OwnedArray<CompatibilityBadge> m_compatBadges;

    std::unique_ptr<LibraryBrowserPanel> m_libraryBrowser;

    std::unique_ptr<PlaylistVisualizationToggle> m_vizToggle;

    std::unique_ptr<PlaylistScoreDisplay> m_scoreDisplay;

    std::unique_ptr<juce::TextButton> m_exportM3UBtn, m_exportPLSBtn, m_exportPDFBtn, m_exportJSONBtn;

    std::unique_ptr<juce::ComboBox> m_autoSortCombo;
    std::unique_ptr<juce::TextButton> m_autoSortBtn, m_fillGapsBtn;

    std::unique_ptr<SmartRuleEditor> m_smartRuleEditor;
    std::unique_ptr<IABuildDialog> m_iaBuildDialog;

    std::unique_ptr<Widgets::NestedSmartRuleEditor> m_nestedSmartEditor;

    static constexpr int kFilterBarHeight = 40;

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    std::unique_ptr<LibraryBrowserPanel::Listener> m_libListener;

    struct TrackInfo
    {
        juce::String title, artist, key, genre, filePath;
        float bpm = 0.0f;
        int energy = 0;
        double durationSec = 0.0;
        int rating = 0;
        int64_t trackId = 0;
    };

    struct Playlist
    {
        juce::String name;
        std::vector<TrackInfo> tracks;
        int64_t dbId = -1;
        bool isSmart = false;
    };

    struct SmartRule
    {
        juce::String field, op, value, value2;
    };

    struct IABuildSettings
    {
        int mood = 0;
        int durationMin = 60;
    };

    int camelotNumber(const juce::String& key) const;
    bool camelotLetter(const juce::String& key) const;
    bool areKeysCompatible(const juce::String& k1, const juce::String& k2) const;
    float computeTrackCompatibility(int idxA, int idxB) const;
    juce::String getSuggestionText() const;

    std::vector<Playlist> m_playlists;
    std::vector<SmartRule> m_smartRules;
    std::vector<TrackInfo> m_allTracksCache;
    std::vector<TrackInfo> m_filteredTracks;
    bool m_isFilterActive = false;
    void updateTrackListPointer();

    void setupFilters();
    void applyFilters();
    std::unique_ptr<juce::Label> m_filterLabel;
    std::unique_ptr<juce::TextEditor> m_filterSearchEdit;
    std::unique_ptr<juce::Slider> m_filterBpmMin, m_filterBpmMax;
    std::unique_ptr<juce::Label> m_filterBpmLabel;
    std::unique_ptr<juce::ComboBox> m_filterKeyCombo;
    std::unique_ptr<juce::ComboBox> m_filterGenreCombo;
    std::unique_ptr<juce::Slider> m_filterEnergyMin, m_filterEnergyMax;
    std::unique_ptr<juce::Label> m_filterEnergyLabel;
    std::unique_ptr<juce::ComboBox> m_filterRatingCombo;
    std::unique_ptr<juce::TextButton> m_filterResetBtn;

    class SmartRuleEditor : public juce::Component
    {
    public:
        struct RuleRow {
            std::unique_ptr<juce::ComboBox> fieldCombo;
            std::unique_ptr<juce::ComboBox> opCombo;
            std::unique_ptr<juce::TextEditor> valueEdit;
            std::unique_ptr<juce::TextEditor> value2Edit;
            std::unique_ptr<juce::TextButton> removeBtn;
        };
        juce::OwnedArray<juce::Component> allChildren;
        std::vector<std::unique_ptr<RuleRow>> rules;
        std::unique_ptr<juce::TextButton> addRuleBtn;
        std::unique_ptr<juce::TextButton> applyBtn;
        std::unique_ptr<juce::TextButton> closeBtn;
        std::unique_ptr<juce::Label> titleLabel;
        std::function<void(std::vector<SmartRule>)> onApply;

        SmartRuleEditor();
        void resized() override;
        void paint(juce::Graphics& g) override;
        void addRule();
        void removeRule(int index);
        void applyRules();
    };

    class IABuildDialog : public juce::Component
    {
    public:
        std::unique_ptr<juce::Label> titleLabel;
        std::unique_ptr<juce::ComboBox> moodCombo;
        std::unique_ptr<juce::ComboBox> durationCombo;
        std::unique_ptr<juce::TextButton> buildBtn;
        std::unique_ptr<juce::TextButton> cancelBtn;
        std::unique_ptr<juce::TextButton> closeBtn;
        std::function<void(IABuildSettings)> onBuild;

        IABuildDialog();
        void resized() override;
        void paint(juce::Graphics& g) override;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPreparationView)
};

}
