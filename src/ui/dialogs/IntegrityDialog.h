#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../services/analysis/AudioIntegrityChecker.h"

namespace BeatMate::UI {

class IntegrityDialog : public juce::Component,
                        public juce::ListBoxModel,
                        private juce::Timer
{
public:
    using Status = Services::Analysis::AudioIntegrityChecker::Status;

    struct Entry {
        juce::String path;
        juce::String title;
        Status status = Status::Unknown;
        juce::String details;
        bool checked = true;
    };

    static void show(std::vector<Entry> entries, const juce::String& scopeLabel,
                     std::function<void()> onFinished = {});

    IntegrityDialog(std::vector<Entry> entries, const juce::String& scopeLabel,
                    std::function<void()> onFinished);
    ~IntegrityDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    void startCheck(bool onlyUnknown);
    void startRepair();
    void cancelWork();
    void exportCsv();
    void rebuildFiltered();
    void updateButtons();
    void timerCallback() override;
    std::vector<int> checkedIndices() const;

    std::vector<Entry> m_all;
    std::vector<int> m_filtered;
    juce::String m_scope;
    std::function<void()> m_onFinished;

    Services::Analysis::AudioIntegrityChecker m_checker;
    std::thread m_worker;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_cancel { false };
    std::atomic<int> m_done { 0 };
    std::atomic<int> m_total { 0 };
    juce::String m_currentFile;
    juce::String m_phase;

    void focusOnProblems();

    std::unique_ptr<juce::ListBox> m_list;
    std::unique_ptr<juce::TextButton> m_checkBtn, m_repairBtn, m_revealBtn,
                                      m_csvBtn, m_allBtn, m_noneBtn, m_badOnlyBtn, m_closeBtn;
    std::unique_ptr<juce::ToggleButton> m_fOk, m_fWarn, m_fBad, m_fUnknown;
    std::unique_ptr<juce::FileChooser> m_chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntegrityDialog)
};

} // namespace BeatMate::UI
