#pragma once
#include <memory>
#include <vector>
#include <map>
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../models/Track.h"
#include "../../services/preparation/SoireePreparationService.h"
#include "../../services/preparation/EventCoordinatorService.h"
#include "../../services/preparation/EventValidationService.h"
#include "../../services/preparation/StorylinePlannerService.h"
#include "../../services/preparation/SetCompatibilityScorer.h"
#include "../widgets/browser/LibraryBrowserPanel.h"
#include "../widgets/timeline/PhaseTimelineWidget.h"
#include "../widgets/handover/HandoverListWidget.h"
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI { class LibraryBrowserPanel; }
namespace BeatMate::UI {


class SoireePreparationView : public juce::Component,
                              public juce::DragAndDropContainer,
                              public BeatMate::UI::IRetranslatable
{
public:
    SoireePreparationView();
    explicit SoireePreparationView(Services::Library::TrackDataProvider* provider);
    ~SoireePreparationView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void soireeExportRequested(const juce::String& format) {}
        virtual void soireeSaved() {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onNewSoiree();
    void onLoadSoiree();
    void onAutoFillAI();
    void onExportPDF();
    void onExportJSON();
    void onExportM3U();
    void onBrowseLibrary();
    void refreshValidation();
    void onStorylineArc(int arcType);

    struct Phase
    {
        juce::String name;
        juce::String genre;
        float targetBPM = 120.0f;
        int energyTarget = 5;
        double durationMin = 30.0;
        juce::Colour color;
        std::vector<Models::Track> assignedTracks;
    };

    struct Soiree
    {
        juce::String name;
        juce::String venue;
        juce::String date;
        int typeIndex = 0;
        double totalDurationHours = 5.0;
        std::vector<Phase> phases;
        bool sonoOK = false, lumieresOK = false, riderOK = false;
        bool backupUSB = false, setlistPrinted = false;
        bool headphonesReady = false, controllerReady = false;
        bool powerAdaptersReady = false, contactOK = false, paymentOK = false;
    };

    class StorylineArcVisualization : public juce::Component
    {
    public:
        Services::Preparation::StoryArc storyArc;

        void paint(juce::Graphics& g) override;
        void resized() override;
    };

    class VenueInfoPanel : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override;
        void resized() override;

        std::unique_ptr<juce::TextEditor> address;
        std::unique_ptr<juce::TextEditor> city;
        std::unique_ptr<juce::TextEditor> country;
        std::unique_ptr<juce::TextEditor> capacity;
        std::unique_ptr<juce::TextEditor> soundSystem;
        std::unique_ptr<juce::TextEditor> djBooth;
        std::unique_ptr<juce::TextEditor> contactName;
        std::unique_ptr<juce::TextEditor> contactEmail;
        std::unique_ptr<juce::TextEditor> feeAmount;
        std::unique_ptr<juce::ComboBox> currency;
        std::unique_ptr<juce::ComboBox> paymentStatus;
    };

    class EventValidationBar : public juce::Component
    {
    public:
        Services::Preparation::EventValidationReport report;

        void setReport(const Services::Preparation::EventValidationReport& r) { report = r; repaint(); }
        void paint(juce::Graphics& g) override;
    };

private:
    void setupUI();
    void rebuildTimeline();
    void selectPhase(int phaseIndex);
    void updatePhaseEditor();
    void updatePhaseTrackList();
    void applyEnergyProfile(int profileIndex);
    void createDefaultPhases(int typeIndex);
    void syncEditorToPhase();

    std::vector<Models::Track> findMatchingTracks(int phaseIndex);
    std::shared_ptr<std::atomic<bool>> m_autoFillBusy = std::make_shared<std::atomic<bool>>(false);

    struct EnergyProfile
    {
        juce::String name;
        std::vector<int> energyLevels;
    };

    class TimelineComponent : public juce::Component
    {
    public:
        std::vector<Phase>* phases = nullptr;
        int hoveredPhase = -1;
        int selectedPhase = -1;
        int dragBorderPhase = -1;
        float dragStartX = 0;
        double dragStartDuration = 0;
        double dragNextStartDuration = 0;
        std::function<void(int)> onPhaseSelected;
        std::function<void()> onPhasesChanged;
        void paint(juce::Graphics& g) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
    private:
        int getPhaseAtX(float x);
        bool isNearBorder(float x, int& borderPhaseIndex);
    };

    class PhaseTrackListModel : public juce::TableListBoxModel
    {
    public:
        std::vector<Models::Track>* tracks = nullptr;
        int getNumRows() override { return tracks ? (int)tracks->size() : 0; }
        void paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected) override;
        void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool selected) override;
        juce::String getCellText(int row, int col);
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

    Soiree m_soiree;
    std::vector<EnergyProfile> m_presets;
    int m_selectedPhaseIndex = -1;

    class SoireeContentComponent : public juce::Component
    {
    public:
        SoireePreparationView* owner = nullptr;
        void paint(juce::Graphics& g) override;
    };

    Services::Preparation::SoireePreparationService m_soireeService;
    Services::Preparation::EventValidationService m_eventValidator;
    Services::Preparation::StorylinePlannerService m_storylinePlanner;
    Services::Preparation::SetCompatibilityScorer m_scorer;

    std::unique_ptr<juce::Viewport> m_viewport;
    std::unique_ptr<SoireeContentComponent> m_content;

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_newBtn, m_mySoireesBtn;

    std::unique_ptr<juce::Label> m_nameLabel, m_venueLabel, m_dateLabel, m_typeLabel, m_durationLabel;
    std::unique_ptr<juce::TextEditor> m_nameEditor, m_venueEditor, m_dateEditor;
    std::unique_ptr<juce::ComboBox> m_typeCombo;
    std::unique_ptr<juce::Slider> m_durationSlider;

    std::unique_ptr<juce::Label> m_profileLabel;
    std::unique_ptr<juce::ComboBox> m_profileCombo;

    std::unique_ptr<TimelineComponent> m_timeline;

    std::unique_ptr<juce::Label> m_phaseNameLabel, m_phaseGenreLabel, m_phaseBpmLabel;
    std::unique_ptr<juce::Label> m_phaseEnergyLabel, m_phaseDurLabel, m_phaseColorLabel;
    std::unique_ptr<juce::TextEditor> m_phaseNameEditor;
    std::unique_ptr<juce::ComboBox> m_phaseGenreCombo;
    std::unique_ptr<juce::Slider> m_phaseBpmSlider, m_phaseEnergySlider, m_phaseDurSlider;
    std::unique_ptr<juce::TextButton> m_phaseColorBtn;

    std::unique_ptr<juce::TextButton> m_autoFillBtn;

    std::unique_ptr<juce::TextButton> m_addPhaseBtn;
    std::unique_ptr<juce::TextButton> m_removePhaseBtn;

    std::unique_ptr<juce::TableListBox> m_phaseTrackList;
    std::unique_ptr<PhaseTrackListModel> m_phaseTrackModel;
    std::unique_ptr<juce::TextButton> m_addTrackBtn, m_removeTrackBtn, m_clearTracksBtn;

    std::unique_ptr<ChecklistComponent> m_checklist;

    std::unique_ptr<juce::TextButton> m_exportPDFBtn, m_exportJSONBtn, m_exportM3UBtn;

    std::unique_ptr<LibraryBrowserPanel> m_libraryBrowser;
    std::unique_ptr<juce::TextButton> m_browseLibraryBtn;
    std::unique_ptr<juce::TabbedComponent> m_rightTabs;

    std::unique_ptr<LibraryBrowserPanel::Listener> m_libListener;

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    std::unique_ptr<juce::FileChooser> m_fileChooser;

    std::unique_ptr<Widgets::PhaseTimelineWidget> m_phaseTimeline;
    std::unique_ptr<Widgets::HandoverListWidget>  m_handoverList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoireePreparationView)
};

}
