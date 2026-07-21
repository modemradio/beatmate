#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "../IRetranslatable.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class ComparisonView : public juce::Component,
                       public BeatMate::UI::IRetranslatable,
                       public juce::ListBoxModel
{
public:
    ComparisonView();
    explicit ComparisonView(Services::Library::TrackDataProvider* provider);
    ~ComparisonView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

private:
    enum class State { OnlyA = 0, OnlyB, Different, Identical };

    struct Row {
        juce::String rel;            // chemin relatif
        State state = State::Identical;
        int64_t sizeA = -1, sizeB = -1;
        int64_t timeA = 0, timeB = 0; // unix ms
        bool checked = true;
    };

    std::vector<int> checkedRows() const;
    void setAllChecked(bool on);
    void updateCopyButtons();
    int m_lastToggledRow = -1;

    void setupUI();
    void pickFolder(bool isA);
    void startCompare();
    void cancelCompare();
    void rebuildFiltered();
    void copyFiles(bool aToB);
    void exportCsv();
    void updateChips();
    static juce::String fmtSize(int64_t bytes);
    static juce::String fmtDate(int64_t unixMs);

    juce::File m_dirA, m_dirB;

    std::unique_ptr<juce::Label> m_dirALabel, m_dirBLabel;
    std::unique_ptr<juce::TextButton> m_pickABtn, m_pickBBtn, m_compareBtn;
    std::unique_ptr<juce::TextButton> m_openABtn, m_openBBtn;
    std::unique_ptr<juce::ToggleButton> m_recurseCheck, m_audioOnlyCheck;
    std::unique_ptr<juce::ComboBox> m_criteriaCombo;   // 1=nom 2=nom+taille 3=nom+taille+date
    std::unique_ptr<juce::TextButton> m_stateChips[4];
    std::unique_ptr<juce::TextButton> m_copyABBtn, m_copyBABtn, m_exportBtn;
    std::unique_ptr<juce::TextButton> m_checkAllBtn, m_uncheckAllBtn;
    std::unique_ptr<juce::ListBox> m_list;
    std::unique_ptr<juce::FileChooser> m_chooser;

    std::vector<Row> m_rows;       // résultat complet (message thread après scan)
    std::vector<int> m_filtered;   // indices filtrés par état
    bool m_show[4] = { true, true, true, false };

    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_cancel{false};
    std::thread m_worker;

    Services::Library::TrackDataProvider* m_provider = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComparisonView)
};

} // namespace BeatMate::UI
