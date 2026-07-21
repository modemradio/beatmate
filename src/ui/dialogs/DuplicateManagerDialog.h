#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../services/library/DuplicateScanner.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class DuplicateManagerDialog : public juce::Component,
                               public juce::ListBoxModel,
                               private juce::Timer
{
public:
    using Scanner = Services::Library::DuplicateScanner;

    static void show(Services::Library::TrackDataProvider* provider,
                     std::function<void()> onFinished = {});

    DuplicateManagerDialog(Services::Library::TrackDataProvider* provider,
                           std::function<void()> onFinished);
    ~DuplicateManagerDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    // Une ligne = un en-tete de groupe, ou un fichier du groupe.
    struct Row {
        int group = -1;
        int entry = -1;   // -1 = en-tete
    };

    void startScan();
    void cancelScan();
    void rebuildRows();
    void applyKeepRuleToAll();
    void removeSelected(bool deleteFiles);
    void exportCsv();
    void updateButtons();
    void timerCallback() override;
    int checkedCount() const;
    int64_t reclaimableBytes() const;

    Services::Library::TrackDataProvider* m_provider = nullptr;
    std::function<void()> m_onFinished;

    std::vector<Scanner::Group> m_groups;
    std::vector<Row> m_rows;
    std::vector<bool> m_collapsed;

    // Groupes remontes par le thread de scan, en attente d'affichage. Le timer les
    // transfere vers m_groups : la liste se remplit pendant le balayage.
    std::vector<Scanner::Group> m_pending;
    std::mutex m_pendingMutex;

    Scanner::Options m_opts;
    std::thread m_worker;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_cancel { false };
    std::atomic<int> m_done { 0 }, m_total { 0 };
    juce::String m_phase;

    std::unique_ptr<juce::ListBox> m_list;
    std::unique_ptr<juce::TextButton> m_scanBtn, m_removeBtn, m_deleteBtn, m_csvBtn,
                                      m_checkAllBtn, m_uncheckAllBtn, m_revealBtn, m_closeBtn;
    std::unique_ptr<juce::ComboBox> m_keepCombo;
    std::unique_ptr<juce::ToggleButton> m_cFile, m_cAudio, m_cMeta, m_cName, m_cIgnoreTags;
    std::unique_ptr<juce::FileChooser> m_chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicateManagerDialog)
};

} // namespace BeatMate::UI
