#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../IRetranslatable.h"
#include "analysis/AnalysisColumns.h"
#include "analysis/AnalysisListHeader.h"
#include "analysis/AnalysisProgressCard.h"
#include "analysis/TrackDetailPanel.h"
#include "../widgets/controls/GradientButton.h"
#include "../../services/analysis/AnalysisRunner.h"
#include "../../services/analysis/AudioIntegrityChecker.h"
#include <atomic>
#include <thread>

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {
class AnalysisView : public juce::Component, public BeatMate::UI::IRetranslatable, private juce::Timer
{
public:
    AnalysisView();
    explicit AnalysisView(Services::Library::TrackDataProvider* provider);
    ~AnalysisView() override;
    void retranslateUi() override;

    void addTrackForAnalysis(const juce::String& title, const juce::String& artist, const juce::String& path);
    void clearTracks();

    void onTrackStarted(const juce::String& path);
    void onTrackFinished(const Services::Analysis::TrackRowResult& result);
    void onBatchProgress(int processed, int total, int skipped);
    void onBatchFinished(int processed, int total, int skipped, bool cancelled);

    void paint(juce::Graphics& g) override;
    void resized() override;

    class Listener { public: virtual ~Listener()=default; virtual void analyzeAllRequested(){} virtual void analyzeSelectedRequested(const juce::StringArray& selectedPaths){} virtual void analysisCancelled(){} };
    void addListener(Listener* l){m_listeners.add(l);}
    void removeListener(Listener* l){m_listeners.remove(l);}
    juce::StringArray getSelectedPaths() const;

    bool isBPMEnabled() const       { return m_optBPM && m_optBPM->getToggleState(); }
    bool isKeyEnabled() const       { return m_optKey && m_optKey->getToggleState(); }
    bool isEnergyEnabled() const    { return m_optEnergy && m_optEnergy->getToggleState(); }
    bool isStructureEnabled() const { return m_optStructure && m_optStructure->getToggleState(); }
    bool isWaveformEnabled() const  { return m_optWaveform && m_optWaveform->getToggleState(); }
    bool isMoodEnabled() const      { return m_optMood && m_optMood->getToggleState(); }
    bool isUltraStemsEnabled() const { return m_optUltraStems && m_optUltraStems->getToggleState(); }

private:
    enum class RowStatus { Pending, Running, Done, Error };

    struct FullTrackEntry {
        int64_t id = 0;
        juce::String title, artist, genre, path;
        RowStatus status = RowStatus::Pending;
        int progress = 0;
        double bpmValue = 0.0;
        float bpmConfidence = 0.0f;
        juce::String key, camelot;
        float energy = 0.0f;
        float lufs = 0.0f;
        bool analyzed = false;
        bool checked = false;
        juce::String energySegmentsJson;
        bool sparkParsed = false;
        std::vector<float> spark;
    };

    class TrackModel : public juce::ListBoxModel {
    public:
        explicit TrackModel(AnalysisView& owner) : view(owner) {}
        AnalysisView& view;
        std::vector<int> visible;
        int getNumRows() override { return static_cast<int>(visible.size()); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
        void selectedRowsChanged(int lastRowSelected) override;
    };

    void setupUI();
    void applyFilters();
    void populateGenreFilter();
    void loadAllFromDatabase();
    void refreshCountLabel();
    void beginRunUi();
    void showDetailForEntry(int allIndex);
    void sortByColumn(AnalysisColumns::Col col);
    int allIndexForPath(const juce::String& path) const;
    void updateSelectAllLabel();
    void setCardTarget(int target);
    void timerCallback() override;

    std::unique_ptr<juce::Label> m_titleLabel, m_trackCountLabel;
    std::unique_ptr<juce::ToggleButton> m_optBPM, m_optKey, m_optEnergy, m_optStructure, m_optWaveform, m_optMood, m_optUltraStems;
    std::unique_ptr<juce::TextButton> m_analyzeSelectedBtn, m_cancelBtn, m_addTracksBtn, m_clearBtn, m_selectAllBtn;
    std::unique_ptr<GradientButton> m_analyzeAllBtn;

    std::unique_ptr<juce::TextEditor> m_searchEditor;
    std::unique_ptr<juce::ComboBox> m_genreFilter;
    std::unique_ptr<juce::ComboBox> m_statusFilter;
    std::unique_ptr<juce::Slider> m_bpmMinSlider;
    std::unique_ptr<juce::Slider> m_bpmMaxSlider;
    std::unique_ptr<juce::Label> m_bpmRangeLabel;
    std::unique_ptr<juce::Label> m_searchLabel, m_genreLabel, m_statusFilterLabel;

    std::vector<FullTrackEntry> m_allTracks;
    std::unique_ptr<TrackModel> m_trackModel;
    std::unique_ptr<juce::ListBox> m_trackList;
    std::unique_ptr<AnalysisListHeader> m_listHeader;
    std::unique_ptr<AnalysisProgressCard> m_progressCard;
    std::unique_ptr<TrackDetailPanel> m_detailPanel;

    AnalysisColumns::Col m_sortColumn = AnalysisColumns::Col::Title;
    bool m_sortAscending = true;

    int m_statTotal = 0;
    int m_statAnalyzed = 0;
    bool m_analyzing = false;
    float m_cardHeight = 0.0f;
    int m_cardTarget = 0;

    // Contrôle d'intégrité (façon MP3val, ffmpeg embarqué)
    struct IntegrityIssue {
        juce::String path;
        Services::Analysis::AudioIntegrityChecker::Status status;
        juce::String details;
    };
    std::unique_ptr<Services::Analysis::AudioIntegrityChecker> m_integrity;
    std::unique_ptr<juce::TextButton> m_integrityBtn;
    std::vector<std::thread> m_integrityWorkers;
    std::atomic<bool> m_integrityRunning{false};
    std::atomic<bool> m_integrityCancel{false};
    std::atomic<int> m_integrityNext{0};
    std::atomic<int> m_integrityDone{0};
    juce::StringArray m_integrityPaths;
    std::vector<IntegrityIssue> m_integrityIssues;   // message thread only
    void showIntegrityMenu();
    void startIntegrityScan(juce::StringArray paths);
    void joinIntegrityWorkers();
    void updateIntegrityButton();
    void showIntegrityReportDialog();
    void repairCorruptFiles();

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisView)
};
} // namespace BeatMate::UI
