#pragma once
#include <memory>
#include <atomic>
#include <array>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../models/Track.h"
#include "../../services/preparation/SetCompatibilityScorer.h"
#include "../../services/preparation/SetStatisticsService.h"
#include "../../services/preparation/SetValidationService.h"
#include "../../services/preparation/SetExportServiceComplete.h"
#include "../../services/preparation/MatchUpService.h"
#include "../../services/preparation/QuickSetPlannerService.h"
#include "../../services/preparation/SetPlannerEngine.h"
#include "../widgets/compatibility/HarmonicCompatibilityChip.h"
#include "../widgets/energy/EnergyCurveEditor.h"
#include "../widgets/energy/BPMProgressionGraph.h"
#include "../widgets/related/RelatedTracksPanel.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {

class LibraryBrowserPanel;


class SetPreparationView : public juce::Component,
                           public juce::DragAndDropContainer,
                           public BeatMate::UI::IRetranslatable,
                           private juce::Timer
{
public:
    SetPreparationView();
    explicit SetPreparationView(Services::Library::TrackDataProvider* provider);
    ~SetPreparationView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void retranslateUi() override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void setlistExportRequested() {}
        virtual void trackPreviewRequested(const juce::String& filePath,
                                           const juce::String& title,
                                           const juce::String& artist) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onAddTrack();
    void addTracksFromLibrary(const std::vector<int64_t>& trackIds);
    void onRemoveTrack();
    void onClearSet();
    void onMoveUp();
    void onMoveDown();
    void onAutoSort();
    void onExportSetlist();
    void cloneToSoiree();
    void onFillGaps();
    void onOptimizeEnergy();
    void onRandomize();

    void onIAAutoArrange();
    void showAutoFillMenu();
    void onAutoFillAI(const juce::String& styleFilter);
    void onSortByBPM();
    void onSortByKey();
    void onSortByEnergy();
    void onCheckCompatibility();
    void onExportM3U();
    void onExportPDF();
    void onExportJSON();
    void onExportCSV();
    void onExportHTML();

    void onArchiveBmset();
    void onSendToDJSoftware();
    void onExportUsbPlaylist();
    void onPrintShare();

    void applyEnergyProfile(int profileIndex);

    std::function<void(const juce::String& filePath, const juce::String& title, const juce::String& artist)> onTrackPreview;

private:
    void setupUI();
    void updateCompatibility();
    void updateTimer();
    void updateSetScore();
    void refreshStatistics();
    void refreshValidation();
    void updateHeaderSubtitle();
    void updateSteps();
    void updateProgressiveDisclosure();
    void runStepAction(int step);
    void showExportMenu(juce::Component* anchor);
    void refreshStepsBarTexts();
    void buildChecklistItems();
    void syncSetInfoFromEditors();
    void runExportToFormat(Services::Preparation::ExportFormat fmt,
                           const juce::String& wildcard,
                           const juce::String& formatLabel,
                           bool withTransitionNotes = false);
    void saveChecklistState();
    void loadChecklistState();

    enum class SortColumn { Number = 0, Title, Artist, BPM, Key, Energy, Duration, Count };
    SortColumn m_sortColumn = SortColumn::Title;
    bool m_sortAscending = true;
    void sortByColumn(SortColumn col);
    void handleColumnHeaderClick(int columnIndex);

    struct TransitionInfo
    {
        int scorePercent = 0;
        float bpmDiff = 0.0f;
        bool keyCompat = true;
        juce::String transitionType;
        juce::Colour color;
        Services::Preparation::SetCompatibilityScorer::CompatibilityResult fullResult;
    };

    class DragDropSetlistModel : public juce::ListBoxModel,
                                  public juce::DragAndDropTarget
    {
    public:
        std::vector<Models::Track>* tracks = nullptr;
        std::vector<TransitionInfo>* transitions = nullptr;
        std::function<void(int row)> onDoubleClick;
        std::function<void(int from, int to)> onReorder;
        std::function<void(int transitionIndex)> onTransitionClicked;

        int getNumRows() override { return tracks ? static_cast<int>(tracks->size()) : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override
        {
            if (e.y > 36 && onTransitionClicked)
                onTransitionClicked(row);
        }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            if (onDoubleClick) onDoubleClick(row);
        }

        juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows);
        bool isInterestedInDragSource(const SourceDetails&) override { return true; }
        void itemDropped(const SourceDetails& details) override;
        void itemDragEnter(const SourceDetails&) override {}
        void itemDragMove(const SourceDetails&) override {}
        void itemDragExit(const SourceDetails&) override {}
        bool shouldDrawDragImageWhenOver() override { return true; }

        int dragInsertIndex = -1;
    };

    class DragDropListBox : public juce::ListBox,
                            public juce::DragAndDropTarget
    {
    public:
        DragDropListBox(const juce::String& name, juce::ListBoxModel* model)
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
        std::function<void(const std::vector<int>& fromRows, int to)> onReorderMulti;
        std::function<void(const juce::String& trackIds)> onExternalDrop;
        int dragInsertIndex = -1;
        int dragSourceRow = -1;
        int externalDropInsertIndex = -1;
    };

    class SetScoreCircle : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        int score = 0;
        void paint(juce::Graphics& g) override;
    };

    class PrepStepsBar : public juce::Component
    {
    public:
        PrepStepsBar();
        std::array<juce::String, 5> stepLabels;
        std::array<juce::String, 5> ctaLabels;
        juce::String doneLabel;
        std::function<void(int step)> onStepAction;

        void setStates(const std::array<bool, 5>& done);
        juce::TextButton* ctaButton() { return m_ctaBtn.get(); }
        int currentStep() const { return m_current; }

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }

    private:
        juce::Rectangle<int> chipArea(int i) const;
        std::array<bool, 5> m_done { false, false, false, false, false };
        int m_current = 1;
        int m_hover = -1;
        std::unique_ptr<juce::TextButton> m_ctaBtn;
    };

    class EnergyCurveGraph : public juce::Component
    {
    public:
        std::vector<int> energyValues;
        std::vector<juce::String> trackNames;
        std::vector<std::vector<int>> intraCurves;
        void paint(juce::Graphics& g) override;
    private:
        juce::Colour getEnergyColor(float normalized) const;
    };

    class StatisticsDashboard : public juce::Component
    {
    public:
        Services::Preparation::SetStatistics stats;
        void paint(juce::Graphics& g) override;
    };

    class TransitionDetailEditor : public juce::Component
    {
    public:
        Services::Preparation::MatchUpResult matchResult;
        std::unique_ptr<juce::ComboBox> transitionTypeCombo;
        std::unique_ptr<juce::Slider> mixDurationSlider;
        std::unique_ptr<juce::Label> scoreLabel, verdictLabel, bpmLabel, keyLabel, energyLabel;
        std::unique_ptr<juce::Label> adviceLabel, mixPointLabel;
        bool visible = false;
        int transitionIndex = -1;

        TransitionDetailEditor();
        void paint(juce::Graphics& g) override;
        void resized() override;
        void updateFromResult(const Services::Preparation::MatchUpResult& result, int idx,
                              int rowScorePercent = -1);
    };

    class AlgorithmSelector : public juce::Component
    {
    public:
        std::unique_ptr<juce::ComboBox> algorithmCombo;
        std::unique_ptr<juce::Slider> bpmWeight, keyWeight, energyWeight, genreWeight;
        std::unique_ptr<juce::Label> bpmWLabel, keyWLabel, energyWLabel, genreWLabel;
        std::unique_ptr<juce::Label> descriptionLabel;
        std::unique_ptr<juce::ComboBox> quickModeCombo;
        std::unique_ptr<juce::TextButton> applyBtn, cancelBtn, closeBtn;
        std::function<void(int algo, float bW, float kW, float eW, float gW, int quickMode)> onApply;
        std::function<void()> onClose;

        AlgorithmSelector();
        void paint(juce::Graphics& g) override;
        void resized() override;

        void updateDescriptionForSelection();
        void setWeightsEnabled(bool enabled);
    };

    class ValidationWarningsPanel : public juce::Component
    {
    public:
        Services::Preparation::ValidationReport report;
        int trackCount = 0;
        void paint(juce::Graphics& g) override;
        void setReport(const Services::Preparation::ValidationReport& r) { report = r; repaint(); }
    };

    class SetTimelineComponent : public juce::Component
    {
    public:
        std::vector<Models::Track>* tracks = nullptr;
        double targetDurationMin = 120.0;
        int hovered = -1;
        std::function<void(int)> onTrackSelected;
        void paint(juce::Graphics& g) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent&) override { hovered = -1; repaint(); }
    private:
        int trackAtX(float x) const;
    };

    class ChecklistComponent : public juce::Component
    {
    public:
        struct Item { juce::String label; bool* value = nullptr; };
        std::vector<Item> items;
        std::function<void()> onChecklistChanged;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
    };

    struct EnergyProfile
    {
        juce::String name;
        std::vector<int> energyLevels;
    };

    struct PreSetChecklist
    {
        bool cuePointsOK = false;
        bool transitionsOK = false;
        bool energyFlowOK = false;
        bool backupReady = false;
        bool headphonesOK = false;
        bool controllerOK = false;
        bool gainStagingOK = false;
        bool setlistPrinted = false;
    };

    std::vector<Models::Track> m_tracks;
    std::vector<TransitionInfo> m_transitionInfos;
    std::vector<EnergyProfile> m_energyPresets;
    PreSetChecklist m_checklistState;

    Services::Preparation::SetCompatibilityScorer m_scorer;
    Services::Preparation::SetStatisticsService m_statsService;
    Services::Preparation::SetValidationService m_validator;
    Services::Preparation::SetExportServiceComplete m_exportService;
    Services::Preparation::MatchUpService m_matchUp;
    Services::Preparation::QuickSetPlannerService m_quickPlanner;
    Services::Preparation::SetPlannerEngine m_planner;

    std::unique_ptr<juce::Label> m_titleLabel, m_subtitleLabel, m_trackCountLabel, m_totalDurationLabel;
    std::unique_ptr<juce::Label> m_avgBPMLabel, m_compatibilityLabel, m_timerLabel;

    std::unique_ptr<juce::Slider> m_setDuration;

    std::unique_ptr<juce::TextButton> m_iaAutoArrangeBtn;
    std::unique_ptr<juce::TextButton> m_sortBPMBtn, m_sortKeyBtn, m_sortEnergyBtn;
    std::unique_ptr<juce::TextButton> m_sortMenuBtn;
    std::unique_ptr<juce::TextButton> m_addBtn, m_removeBtn, m_clearBtn;
    std::unique_ptr<juce::TextButton> m_checkCompatBtn;
    std::unique_ptr<juce::TextButton> m_exportBtn;
    std::unique_ptr<juce::TextButton> m_emptyBrowseBtn, m_emptyAutoFillBtn;
    std::unique_ptr<PrepStepsBar> m_stepsBar;
    static constexpr int kStepsBarH = 44;
    bool m_hasArranged = false;
    bool m_validationRun = false;
    bool m_lastValidationOk = false;
    bool m_hasExported = false;
    std::unique_ptr<juce::TextButton> m_moveUpBtn, m_moveDownBtn;
    std::unique_ptr<juce::TextButton> m_autoSortBtn, m_fillGapsBtn, m_optimizeEnergyBtn, m_randomizeBtn;

    std::unique_ptr<DragDropListBox> m_setlistTable;
    std::unique_ptr<DragDropSetlistModel> m_setlistModel;

    std::unique_ptr<EnergyCurveGraph> m_energyCurve;

    std::unique_ptr<SetScoreCircle> m_setScore;


    std::unique_ptr<LibraryBrowserPanel> m_libraryBrowser;

    std::unique_ptr<StatisticsDashboard> m_statsDashboard;

    std::unique_ptr<TransitionDetailEditor> m_transitionEditor;

    std::unique_ptr<AlgorithmSelector> m_algoSelector;

    std::unique_ptr<ValidationWarningsPanel> m_validationPanel;

    std::unique_ptr<Widgets::EnergyCurveEditor>   m_energyCurveEditor;
    std::unique_ptr<Widgets::BPMProgressionGraph> m_bpmGraph;
    std::unique_ptr<Widgets::RelatedTracksPanel>  m_relatedTracks;
    juce::OwnedArray<Widgets::HarmonicCompatibilityChip> m_harmonicChips;

    std::unique_ptr<juce::Label>      m_setNameLabel, m_djLabel, m_venueLabel, m_dateLabel, m_durationLabel;
    std::unique_ptr<juce::TextEditor> m_setNameEditor, m_djEditor, m_venueEditor, m_dateEditor;
    std::unique_ptr<juce::Label>      m_profileLabel;
    std::unique_ptr<juce::ComboBox>   m_profileCombo;
    std::unique_ptr<SetTimelineComponent> m_setTimeline;

    std::unique_ptr<juce::TextButton> m_autoFillBtn;
    std::unique_ptr<juce::TextButton> m_browseLibraryBtn;
    void onBrowseLibrary();

    std::unique_ptr<ChecklistComponent> m_checklist;
    std::unique_ptr<juce::TextButton>   m_exportArchiveBtn;
    std::unique_ptr<juce::TextButton>   m_exportDJBtn;
    std::unique_ptr<juce::TextButton>   m_exportUsbBtn;
    std::unique_ptr<juce::TextButton>   m_exportPrintBtn;

    juce::String m_djName, m_eventName, m_eventDate, m_eventVenue;

    void runExportAsync(const Services::Preparation::ExportConfig& cfg,
                        const juce::String& destPath,
                        const juce::String& formatLabel);
    std::shared_ptr<std::atomic<bool>> m_exportBusy { std::make_shared<std::atomic<bool>>(false) };
    std::shared_ptr<std::atomic<bool>> m_autoFillBusy { std::make_shared<std::atomic<bool>>(false) };
    std::shared_ptr<std::atomic<bool>> m_autoArrangeBusy { std::make_shared<std::atomic<bool>>(false) };

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    void saveSetToJSON();
    void loadSetFromJSON();
    void timerCallback() override;
    bool m_persistDirty = false;

    void handleLibraryDrop(const juce::String& trackIdsStr, int insertIndex);
    void addTrackToSet(const Models::Track& track);

    void syncProWidgets();

    float computeAverageCompatibility(const std::vector<Models::Track>& tracks);
    void applyArrangementResult(const std::vector<Models::Track>& before,
                                const std::vector<Models::Track>& after,
                                const juce::String& algoName);
    void runAutoArrange(int algo, float bW, float kW, float eW, float gW, int quickMode);

    struct LibBrowserListener;
    std::unique_ptr<LibBrowserListener> m_libBrowserListener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetPreparationView)
};

}
