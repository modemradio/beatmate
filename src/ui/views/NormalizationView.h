#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../IRetranslatable.h"
#include "normalization/AudioEditorPanel.h"
namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {
class NormalizationView : public juce::Component, public BeatMate::UI::IRetranslatable, private juce::Timer {
public:
    NormalizationView();
    explicit NormalizationView(Services::Library::TrackDataProvider* provider);
    ~NormalizationView() override=default;
    void paint(juce::Graphics& g) override; void resized() override;
    void retranslateUi() override;
    class Listener{public:virtual ~Listener()=default;virtual void normalizeRequested(){}virtual void normalizeSelectedRequested(const std::vector<int>& /*indices*/){}virtual void cancelRequested(){}virtual void previewRequested(int){}virtual void normalizationComplete(){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
    void onNormalize(); void onPreview(); void onCancel(); void updateProgress(int,int);
    float getTargetLUFS() const { return m_targetLUFS ? static_cast<float>(m_targetLUFS->getValue()) : -14.0f; }
    int getTrackCount() const { return m_trackModel ? static_cast<int>(m_trackModel->entries.size()) : 0; }
    std::vector<juce::String> getTrackTitles() const;
    // Appele depuis le thread listener.
    void setCurrentMeasurement(const juce::String& trackTitle,
                               float measuredLUFSBefore,
                               float finalLUFSAfter);
    juce::String getTitleAtRow(int row) const;
    void queueTracks(const std::vector<int64_t>& trackIds);
private:
    void setupUI();
    void applyFilters();
    void populateGenreFilter();
    void addAllFilteredTracks();
    void removeAllTracks();

    std::unique_ptr<juce::Label> m_titleLabel,m_descLabel,m_lufsValueLabel,m_statusLabel;
    std::unique_ptr<juce::Slider> m_targetLUFS;
    std::unique_ptr<juce::TextButton> m_addTracksBtn,m_removeBtn,m_previewBeforeBtn,m_previewAfterBtn,m_normalizeBtn,m_cancelBtn;
    std::unique_ptr<juce::TextButton> m_presetSpotify,m_presetApple,m_presetYT,m_presetClub;
    std::unique_ptr<juce::TextButton> m_addAllBtn, m_removeAllBtn, m_selectAllBtn;

    std::unique_ptr<juce::TextEditor> m_searchEditor;
    std::unique_ptr<juce::ComboBox> m_genreFilter;
    std::unique_ptr<juce::Label> m_searchLabel, m_genreLabel;

    std::unique_ptr<juce::ListBox> m_trackTable;
    class TrackModel:public juce::ListBoxModel{
    public:
        struct E{
            juce::String title, artist, currentLUFS, targetLUFS, status;
            bool selected = false;   // décoché par défaut (l'utilisateur choisit)
            juce::String filePath;
        };
        std::vector<E> entries;
        juce::ListBox* ownerList = nullptr;
        int getNumRows()override{return(int)entries.size();}
        void paintListBoxItem(int,juce::Graphics&,int,int,bool)override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        void selectAll(bool select);
        int getSelectedCount() const;
    };
    std::unique_ptr<TrackModel> m_trackModel;

    std::unique_ptr<AudioEditorPanel> m_audioEditor;
    std::unique_ptr<juce::TextButton> m_editFileBtn;
    bool m_editorVisible = false;
    void openAudioEditor();

    double m_progress=0.0;
    int m_completedCount = 0;
    int m_totalCount = 0;
    juce::String m_currentTrackName;
    juce::String m_lufsBefore = "-";
    juce::String m_lufsAfter = "-";
    double m_normStartTime = 0.0;
    bool m_normalizing = false;
    float m_pulsePhase = 0.0f;
    float m_spinnerAngle = 0.0f;
    void timerCallback() override;

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    struct FullTrackEntry {
        juce::String title, artist, genre;
        juce::String filePath;
        double bpm = 0.0;
        bool analyzed = false;
    };
    std::vector<FullTrackEntry> m_allDbTracks;
    void loadTracksFromDatabase();

    std::vector<float> m_wfBars;
    float m_wfGain = 1.0f;
    void loadComparisonWaveform(const juce::String& filePath);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NormalizationView)
};
} // namespace BeatMate::UI
