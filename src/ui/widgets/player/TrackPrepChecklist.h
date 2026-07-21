#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
namespace BeatMate::UI {


struct PrepCheckItem {
    juce::String label;
    bool checked = false;
    bool autoCheckable = true;
    juce::String tooltip;
};

class TrackPrepChecklist : public juce::Component {
public:
    TrackPrepChecklist();
    ~TrackPrepChecklist() override = default;

    void resetChecklist();
    void setItem(int index, bool checked);
    void setItemLabel(int index, const juce::String& label);
    bool isItemChecked(int index) const;
    int getCheckedCount() const;
    int getTotalCount() const { return (int)m_items.size(); }
    float getCompletionPercent() const;
    bool isComplete() const;

    void setHasBPM(bool has);
    void setHasKey(bool has);
    void setHasCuePoints(bool has);
    void setHasBeatGrid(bool has);
    void setHasWaveform(bool has);
    void setHasGainAnalysis(bool has);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::vector<PrepCheckItem> m_items;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackPrepChecklist)
};

}
