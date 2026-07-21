#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../../services/library/TrackRelinkService.h"

namespace BeatMate::Services::Library { class TrackDataProvider; }

namespace BeatMate::UI {

class RelinkDialog : public juce::Component, private juce::ListBoxModel {
public:
    explicit RelinkDialog(Services::Library::TrackDataProvider* provider);

    static void show(Services::Library::TrackDataProvider* provider);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

    void startScan();
    void applySelected();

    Services::Library::TrackDataProvider* provider_ = nullptr;
    Services::Library::TrackRelinkService service_;
    std::vector<Services::Library::RelinkCandidate> candidates_;
    std::vector<std::string> roots_;
    int missingCount_ = 0;
    bool scanning_ = false;

    juce::Label status_;
    juce::ListBox list_;
    juce::TextButton scanBtn_;
    juce::TextButton addRootBtn_;
    juce::TextButton applyBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RelinkDialog)
};

} // namespace BeatMate::UI
