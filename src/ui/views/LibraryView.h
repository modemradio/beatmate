#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../models/Track.h"
#include "../../services/library/AdvancedSearchService.h"
#include "../../services/library/CollectionStats.h"
#include "../../services/library/PeakFileService.h"
#include "../../services/library/ImageCacheService.h"
#include "../../services/library/DuplicateDetector.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

#include "../../services/djsoftware/DJSoftwareManager.h"
#include "../widgets/browser/BrowserPanel.h"
#include "LibraryBatchEditDialog.h"
#include "analysis/TrackDetailPanel.h"
#include "library/TagEditorPanel.h"
#include "../IRetranslatable.h"

namespace BeatMate::UI {

class LibraryView : public juce::Component,
                    public juce::TextEditor::Listener,
                    public BrowserPanel::Listener,
                    public juce::DragAndDropContainer,
                    public juce::Timer,
                    public juce::KeyListener,
                    public juce::ScrollBar::Listener,
                    public BeatMate::UI::IRetranslatable
{
public:
    LibraryView();
    explicit LibraryView(Services::Library::TrackDataProvider* provider);
    ~LibraryView() override {
        if (m_aliveFlag) m_aliveFlag->store(false);
        if (m_trackList) m_trackList->getViewport()->getHorizontalScrollBar().removeListener(this);
        m_wavePool.removeAllJobs(true, 4000);
        m_artPool.removeAllJobs(true, 4000);
        // ordre de destruction : détacher la racine du TreeView avant m_rootItem
        if (m_collectionTree) m_collectionTree->setRootItem(nullptr);
        m_collectionTree.reset();
        m_rootItem.reset();
    }

    void refreshLibrary();
    void setSearchText(const juce::String& text);

    void retranslateUi() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void resized() override;
    void visibilityChanged() override;
    void textEditorTextChanged(juce::TextEditor&) override;
    void filterChanged(const juce::String& category, const juce::String& value) override;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;

    void timerCallback() override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void trackSelected(int64_t) {}
        virtual void trackDoubleClicked(int64_t) {}
        virtual void tracksDroppedOnPlaylist(const std::vector<int>& rowIndices, const juce::String& playlistName) {}
        virtual void addToSetRequested(const std::vector<int64_t>& trackIds) {}
        virtual void addToPlaylistRequested(const std::vector<int64_t>& trackIds, const juce::String& playlistName) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    std::function<void(const juce::String&, const juce::String&, double, const juce::String&, const juce::String&)> onTrackPreview;
    std::function<void(const juce::String& filePath,
                       const juce::String& title,
                       const juce::String& artist)> onHoverPreview;
    std::function<void()> onHoverPreviewStop;
    std::function<void(int64_t trackId)> onOpenInHotCues;
    std::function<void(std::vector<int64_t> trackIds)> onAnalyzeTracks;
    std::function<void(std::vector<int64_t> trackIds)> onNormalizeTracks;
    std::function<void(std::vector<int64_t> trackIds)> onAddToSet;

    void refreshVisibleRowsInPlace();

private:
    void setupUI();
    void setupFilters();
    void populateDemoData();
    void applyAllFilters();
    void loadFromDatabase();

    enum class SortColumn {
        Waveform = 0, Art, Title, Artist, Album, BPM, Key, Camelot, Energy, Duration,
        Genre, Rating, Color, Cues, Stems, Year, PlayCount,
        Lufs, Mood, Danceability, RecordLabel, Comment, Bitrate, LastPlayed, DateAdded,
        Count
    };
    SortColumn m_sortColumn = SortColumn::Title;
    bool m_sortAscending = true;
    void sortByColumn(SortColumn col);
    void drawSortArrow(juce::Graphics& g, int x, int y, int w, int h, bool ascending);

    int m_hoveredRow = -1;
    double m_hoverStartTime = 0.0;
    bool m_hoverPreviewActive = false;
    static constexpr double kHoverDelayMs = 1000.0;

    void showContextMenu(int row, const juce::MouseEvent& e);
    void showBatchEditDialog();

    using BatchEditDialog = LibraryBatchEditDialog;

    struct FilterPreset {
        juce::String name;
        float bpmMin = 60, bpmMax = 200;
        juce::String key, genre, artist;
        float energyMin = 1, energyMax = 10;
        int ratingMin = 0;
        bool isSystem = false;
    };
    std::vector<FilterPreset> m_filterPresets;
    std::unique_ptr<juce::ComboBox> m_presetCombo;
    std::unique_ptr<juce::TextButton> m_savePresetBtn;
    void loadFilterPresets();
    void saveFilterPresets();
    void applyFilterPreset(int presetIndex);

    class StatisticsPanel : public juce::Component
    {
    public:
        Services::Library::CollectionReport report;
        bool hasData = false;
        void paint(juce::Graphics& g) override;
    };
    std::unique_ptr<StatisticsPanel> m_statsPanel;
    std::unique_ptr<juce::TextButton> m_statsToggleBtn;
    bool m_statsVisible = false;
    void refreshStats();

    std::unique_ptr<Services::Library::AdvancedSearchService> m_advancedSearch;
    std::unique_ptr<Services::Library::CollectionStats> m_collectionStats;
    std::unique_ptr<Services::Library::PeakFileService> m_peakService;
    std::unique_ptr<Services::Library::ImageCacheService> m_imageCache;
    std::unique_ptr<Services::Library::DuplicateDetector> m_duplicateDetector;

    std::unique_ptr<BrowserPanel> m_browserPanel;
    std::unique_ptr<juce::TextEditor> m_searchEdit;
    std::unique_ptr<juce::TextButton> m_clearSearchBtn;

    std::unique_ptr<juce::Slider> m_bpmMinSlider, m_bpmMaxSlider;
    std::unique_ptr<juce::Label> m_bpmRangeLabel;
    std::unique_ptr<juce::ComboBox> m_keyFilterCombo;
    std::unique_ptr<juce::ComboBox> m_genreFilterCombo;
    std::unique_ptr<juce::ComboBox> m_artistFilterCombo;
    std::unique_ptr<juce::Slider> m_energyMinSlider, m_energyMaxSlider;
    std::unique_ptr<juce::Label> m_energyRangeLabel;
    std::unique_ptr<juce::ComboBox> m_ratingFilterCombo;
    std::unique_ptr<juce::TextButton> m_resetFiltersBtn;

    std::unique_ptr<juce::Label> m_titleLabel, m_trackCountLabel;
    std::unique_ptr<juce::Label> m_filterSectionLabel;

    std::unique_ptr<juce::TextButton> m_importRekordboxXmlBtn;
    std::unique_ptr<juce::FileChooser> m_importXmlChooser;
    void importRekordboxXmlFlow();

    std::unique_ptr<juce::TextButton> m_syncDjPlaylistsBtn;

    std::unique_ptr<juce::TextButton> m_relinkBtn;

    std::unique_ptr<juce::TextButton> m_browseFolderBtn;
    std::unique_ptr<juce::FileChooser> m_browseFolderChooser;
    void browseFoldersFlow();

    std::unique_ptr<BatchEditDialog> m_batchEditDialog;

public:
    class TrackTableModel : public juce::ListBoxModel {
    public:
        struct Track {
            juce::String title, artist, album, bpm, key, camelot, energy, duration, genre, rating, filePath;
            juce::String year, stems, color, label;
            juce::String lufs, mood, danceability, comment, dateAdded;
            int cueCount = 0;
            int playCount = 0;
            int64_t trackId = 0;
            bool analyzed = false;
            juce::String sourceBadge;
            juce::String bitrate;
            juce::String lastPlayed;
            std::vector<float> waveformLow, waveformMid, waveformHigh;
            bool waveformLoaded = false;
            juce::Image albumArtThumb;
            bool albumArtLoaded = false;
        };
        std::vector<Track> tracks, filteredTracks;
        juce::String filterText;

        std::set<int64_t> checkedIds;
        int lastCheckedRow = -1;
        std::function<void()> onCheckedChanged;
        bool isChecked(int64_t id) const { return checkedIds.find(id) != checkedIds.end(); }
        std::vector<int64_t> checkedOrSelected(juce::ListBox* lb) const;

        SortColumn sortCol = SortColumn::Title;
        bool sortAsc = true;

        int getNumRows() override { return static_cast<int>(filteredTracks.size()); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        void applyFilter();
        void sortTracks();

        int hoveredRow = -1;

        struct RowWave {
            std::vector<float> low, mid, high;
            bool exists = false;
            juce::uint32 stampMs = 0;
        };
        std::unordered_map<int64_t, RowWave> waveCache;

        struct RowArt {
            juce::Image img;
            bool exists = false;
            juce::uint32 stampMs = 0;
        };
        std::unordered_map<int64_t, RowArt> artCache;
        std::function<void(int64_t trackId, const juce::String& filePath)> onArtNeeded;

        void selectedRowsChanged(int lastRowSelected) override {
            if (onSelectionChanged) onSelectionChanged(lastRowSelected);
        }

        std::function<void(int row)> onDoubleClicked;
        std::function<void(int row, int columnIndex, const juce::String& value)> onCellClicked;
        std::function<void(int row, const juce::MouseEvent&)> onRightClick;
        std::function<void(int64_t trackId, int newRating)> onRatingChanged;
        std::function<void(int64_t trackId)> onWaveformNeeded;
        std::function<void(int row)> onSelectionChanged;

        juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    };
    std::unique_ptr<TrackTableModel> m_model;
    std::unique_ptr<juce::ListBox> m_trackList;

private:
    std::unique_ptr<TrackDetailPanel> m_detailPanel;
    std::unique_ptr<juce::TextButton> m_detailToggleBtn;
    bool m_detailVisible = true;
    juce::ThreadPool m_wavePool{1};
    std::set<int64_t> m_wavePending;
    bool m_refreshQueued = false;
    void requestRowWaveform(int64_t trackId);
    juce::ThreadPool m_artPool{1};
    std::set<int64_t> m_artPending;
    void requestRowArt(int64_t trackId, const juce::String& filePath);
    bool m_listRepaintPending = false;
    void scheduleListRepaint();
    void showDetailForRow(int row);

public:
    class FacetBar : public juce::Component {
    public:
        struct Chip { juce::String value; juce::String label; int count = 0; bool active = false; };
        std::vector<Chip> chips;
        int dim = 0;
        bool anyActive = false;
        bool groupGenres = true;
        float scrollX = 0.0f;
        std::function<void(int)> onDimChanged;
        std::function<void(const juce::String&)> onChipToggled;
        std::function<void()> onClearAll;
        std::function<void(bool)> onGroupToggled;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    private:
        struct Zone { juce::Rectangle<float> r; int kind = 0; int index = 0; };
        std::vector<Zone> m_zones;
        float m_pageWidth = 200.0f;
    };
private:
    std::unique_ptr<FacetBar> m_facetBar;
    std::map<int, std::set<juce::String>> m_activeFacets;
    std::vector<TrackTableModel::Track> m_facetBase;
    void applyFacetsOnly();
    void applyFacetsAndRebuildChips();
    static juce::String facetValueForTrack(int dim, const TrackTableModel::Track& t);

public:

public:
    enum class CollectionNodeType {
        Root,
        BeatMateLibrary,
        DJRoot,
        DJRootUnavailable,
        RekordboxPlaylists,
        RekordboxHistory,
        SeratoCrates,
        SeratoSmartCrates,
        TraktorPlaylists,
        VirtualDJFolders,
        EngineDJPlaylists,
        Playlist,
        Folder,
        BeatMateSmartlists,
        SmartPlaylist
    };

    class CollectionTreeItem : public juce::TreeViewItem {
    public:
        CollectionTreeItem(LibraryView& owner,
                           CollectionNodeType type,
                           juce::String name,
                           juce::String payload = {});
        ~CollectionTreeItem() override = default;

        bool mightContainSubItems() override;
        void paintItem(juce::Graphics& g, int width, int height) override;
        void itemSelectionChanged(bool nowSelected) override;
        void itemOpennessChanged(bool isNowOpen) override;
        void itemClicked(const juce::MouseEvent& e) override;
        juce::String getUniqueName() const override;
        int getItemHeight() const override { return 22; }

        CollectionNodeType type_;
        juce::String name_;
        juce::String payload_;
        bool lazyLoaded_ = false;

    private:
        LibraryView& owner_;
    };

private:
    std::unique_ptr<juce::TreeView> m_collectionTree;
    std::unique_ptr<CollectionTreeItem> m_rootItem;
    std::unique_ptr<Services::DJSoftware::DJSoftwareManager> m_djManager;

    CollectionNodeType m_activeNodeType = CollectionNodeType::BeatMateLibrary;
    juce::String m_activeNodePayload;

    void buildCollectionTree();
    void rebuildCollectionTree();
    void onTreeNodeSelected(CollectionNodeType type, const juce::String& payload);
    void showSmartlistMenu(CollectionNodeType type, const juce::String& payload);
    void openSmartlistEditor(int64_t playlistId);
    void populateDJSubtree(CollectionTreeItem* djRoot, Services::DJSoftware::DJSoftwareType dj);

    void loadRekordboxTracksForNode(const juce::String& payload);
    void loadSeratoTracksForNode(const juce::String& payload);
    void loadTraktorTracksForNode(const juce::String& payload);
    void loadVirtualDJTracksForNode(const juce::String& payload);
    void loadEngineDJTracksForNode(const juce::String& payload);

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    void handleColumnHeaderClick(int columnIndex);

    int m_resizingCol = -1;
    int m_resizeStartX = 0;
    int m_resizeStartW = 0;
    int columnEdgeAtX(int mouseX, int mouseY) const;
    void loadColWidths();
    void saveColWidths();
    void loadColVisibility();
    void saveColVisibility();
    void showColumnPicker();
    void updateTableContentWidth();
    void showColorMenuForRow(int row);

    std::unique_ptr<juce::TextButton> m_viewsBtn;
    void showViewsMenu();
    void saveCurrentViewAs(const juce::String& name);
    void applySavedView(const juce::String& name);

    std::unique_ptr<TagEditorPanel> m_tagEditor;
    std::unique_ptr<juce::TextButton> m_tagEditorBtn;
    bool m_tagEditorVisible = false;
    void toggleTagEditor(bool show);

    std::unique_ptr<juce::TextButton> m_selectAllTableBtn;
    std::unique_ptr<juce::TextButton> m_duplicatesBtn;
    bool m_needsInitialLoad = false;
    std::unique_ptr<juce::TextButton> m_repairCorruptBtn;
    std::unique_ptr<juce::TextButton> m_integrityBtn;
    void updateContextActionButtons();
    void repairCorruptShown();
    void toggleAllChecked();
    void updateCheckedUi();
    void showIntegrityMenu();
    void openIntegrityDialog(int scope);
    void quickIntegrity(int row, bool repair);

    bool m_searchFocused = false;

    static constexpr int kSearchBarHeight = 48;
    static constexpr int kFilterBarHeight = 44;
    static constexpr int kFacetBarHeight = 34;
    static constexpr int kColumnHeaderHeight = 26;

    static constexpr int kCheckGutter = 26;
    static constexpr int kNumColumns = 25;
    static int kColWidths[kNumColumns];
    static bool kColVisible[kNumColumns];
    static constexpr const char* kColNames[kNumColumns] = {
        "WAVE", "ART", "TITRE", "ARTISTE", "ALBUM", "BPM", "KEY", "CAMELOT", "ENERGY",
        "DUREE", "GENRE", "NOTE", "COL", "CUES", "STEMS", "ANNEE", "PLAYS",
        "LUFS", "MOOD", "DANSE", "LABEL", "COMMENT", "KBPS", "JOUE LE", "AJOUTE"
    };

    std::shared_ptr<std::atomic<bool>> m_aliveFlag;
    std::shared_ptr<std::atomic<bool>> m_syncBusy { std::make_shared<std::atomic<bool>>(false) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryView)
};

}
