#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DJSourceGrid.h"
#include "DJPlaylistPicker.h"
#include "ImportSummaryCard.h"
#include "../../IRetranslatable.h"
#include "../../../services/djsoftware/DJImportService.h"

namespace BeatMate::UI {

class DJImportPanel : public juce::Component, public IRetranslatable {
public:
    DJImportPanel();
    ~DJImportPanel() override;

    void refreshSources();

    std::function<void()> onNavigateToLibrary;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    enum class Step { PickSource, PickPlaylists, Importing, Done };

    void showStep(Step step);
    void chooseSource(Services::DJSoftware::DJSoftwareType type);
    void startImport();
    void applyProgress(int processed, int total, const juce::String& item);
    void finishImport(const Services::DJSoftware::DJImportReport& report);

    Step m_step = Step::PickSource;
    Services::DJSoftware::DJSoftwareType m_sourceType = Services::DJSoftware::DJSoftwareType::Rekordbox;
    juce::String m_sourceName;

    std::unique_ptr<DJSourceGrid> m_sourceGrid;
    std::unique_ptr<DJPlaylistPicker> m_playlistPicker;
    std::unique_ptr<ImportSummaryCard> m_summary;
    std::unique_ptr<juce::TextButton> m_backBtn, m_importBtn, m_cancelBtn;
    std::unique_ptr<juce::Label> m_statusLabel;

    double m_progress = 0.0;
    bool m_loadingPlaylists = false;
    int m_syncRetries = 0;
    bool m_retryWaiting = false;

    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::thread m_worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DJImportPanel)
};

}
