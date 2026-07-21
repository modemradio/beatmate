#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../../services/library/DuplicateDetector.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class DuplicatesDialog : public juce::Component, private juce::ListBoxModel {
public:
    explicit DuplicatesDialog(Services::Library::TrackDataProvider* provider);

    static void show(Services::Library::TrackDataProvider* provider);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct PairRow {
        Models::Track keep;
        Models::Track remove;
        float confidence = 0.0f;
        bool selected = true;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

    void startScan();
    void mergeSelected();

    Services::Library::TrackDataProvider* provider_ = nullptr;
    std::vector<PairRow> rows_;
    bool scanning_ = false;

    juce::ComboBox methodCombo_;
    juce::Label status_;
    juce::ListBox list_;
    juce::TextButton scanBtn_;
    juce::TextButton mergeBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuplicatesDialog)
};

} // namespace BeatMate::UI
