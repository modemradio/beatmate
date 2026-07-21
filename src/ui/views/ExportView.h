#pragma once
#include <memory>
#include <vector>
#include <set>
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../IRetranslatable.h"
#include "../../services/export/BatchExportService.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }
namespace BeatMate::UI {

class ExportView : public juce::Component,
                   public juce::Timer,
                   public juce::FileDragAndDropTarget,
                   public BeatMate::UI::IRetranslatable
{
public:
    ExportView();
    explicit ExportView(Services::Library::TrackDataProvider* provider);
    ~ExportView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseUp(const juce::MouseEvent& e) override;
    void retranslateUi() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void exportRequested() {}
        virtual void exportCompleted(int filesExported) {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void onBrowseDestination();
    void onStartExport();
    void onCancel();
    void onAddTracks();
    void onAddPlaylist();
    void onSelectAll();
    void onDeselectAll();
    void onRemoveSelected();
    void onExportPlaylistM3U();
    void onExportPlaylistPLS();
    void onExportPlaylistPDF();

    void updateProgress(int current, int total, const juce::String& fileName);

private:
    void setupUI();
    void updateEstimatedSize();
    void addDroppedFiles(const juce::StringArray& files);
    Services::Export::BatchExportSettings collectSettings() const;
    void applySettings(const Services::Export::BatchExportSettings& settings);
    void onExportFinished(const Services::Export::BatchExportReport& report);
    void refreshPresetCombo();
    void onSavePreset();
    void onDeletePreset();
    void setExportingState(bool exporting);

    struct ExportTrackInfo
    {
        int64_t trackId = 0;
        juce::String title;
        juce::String artist;
        juce::String filePath;
        juce::String key;
        double duration = 0.0;
        float bpm = 0.0f;
        int energy = 0;
        bool selected = true;
        int64_t fileSize = 0;
    };

    class TrackModel : public juce::ListBoxModel
    {
    public:
        std::vector<ExportTrackInfo>* tracks = nullptr;
        ExportView* owner = nullptr;
        int getNumRows() override { return tracks ? static_cast<int>(tracks->size()) : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    };

    std::vector<ExportTrackInfo> m_exportTracks;
    bool m_isExporting = false;
    bool m_dragHover = false;
    double m_progress = 0.0;
    double m_estimatedSizeMB = 0.0;
    juce::int64 m_exportStartTime = 0;
    int m_lastDone = 0;
    int m_lastTotal = 0;

    std::unique_ptr<Services::Export::BatchExportService> m_exportService;
    bool m_djRekordbox = false, m_djSerato = false, m_djTraktor = false, m_djVirtualDJ = false;
    juce::Rectangle<int> m_djCardRects[4];

    std::unique_ptr<juce::Label> m_titleLabel, m_statusLabel, m_sizeLabel, m_timeLabel;
    std::unique_ptr<juce::Label> m_formatLabel, m_qualityLabel, m_sampleRateLabel;
    std::unique_ptr<juce::Label> m_bitDepthLabel, m_destLabel, m_structLabel;

    std::unique_ptr<juce::ComboBox> m_formatCombo, m_bitrateCombo, m_sampleRateCombo;
    std::unique_ptr<juce::ComboBox> m_bitDepthCombo, m_structureCombo;

    std::unique_ptr<juce::TextEditor> m_destEdit;

    std::unique_ptr<juce::TextButton> m_browseBtn, m_exportBtn, m_cancelBtn;
    std::unique_ptr<juce::TextButton> m_addTracksBtn, m_addPlaylistBtn;
    std::unique_ptr<juce::TextButton> m_selectAllBtn, m_deselectAllBtn, m_removeSelBtn;
    std::unique_ptr<juce::TextButton> m_exportM3UBtn, m_exportPLSBtn, m_exportPDFBtn;

    std::unique_ptr<juce::ToggleButton> m_normalizeCheck, m_writeTagsCheck, m_writeM3UCheck;

    std::unique_ptr<juce::Slider> m_lufsSlider;
    std::unique_ptr<juce::Label> m_lufsLabel;
    std::unique_ptr<juce::ComboBox> m_presetCombo;
    std::unique_ptr<juce::TextButton> m_presetSaveBtn, m_presetDeleteBtn;
    std::unique_ptr<juce::Label> m_presetLabel;
    // LUFS presets : Spotify -14, Apple -16, YouTube -14, Club -8.
    std::unique_ptr<juce::TextButton> m_presetSpotifyBtn, m_presetAppleBtn,
                                      m_presetYouTubeBtn, m_presetClubBtn;

    std::unique_ptr<juce::ListBox> m_trackTable;
    std::unique_ptr<TrackModel> m_trackModel;

    juce::ListenerList<Listener> m_listeners;
    Services::Library::TrackDataProvider* m_provider = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportView)
};

} // namespace BeatMate::UI
