#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class DockablePanel : public juce::Component {
public:
    DockablePanel(const juce::String& title); ~DockablePanel() override=default;
    void setContentWidget(juce::Component* widget);void saveState();void restoreState();
    void paint(juce::Graphics& g) override;void resized() override;
private:
    juce::String m_title;juce::Component* m_content=nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DockablePanel)
};
} // namespace BeatMate::UI
