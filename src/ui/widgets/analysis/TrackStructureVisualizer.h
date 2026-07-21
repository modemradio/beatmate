#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
struct StructureSection{juce::String label;double start;double end;juce::Colour color;};
class TrackStructureVisualizer : public juce::Component {
public:
    TrackStructureVisualizer(); ~TrackStructureVisualizer() override=default;
    void setSections(const std::vector<StructureSection>& sections);
    void paint(juce::Graphics& g) override;void mouseDown(const juce::MouseEvent& e) override;
    class Listener{public:virtual ~Listener()=default;virtual void sectionClicked(int){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    std::vector<StructureSection> m_sections;juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStructureVisualizer)
};
} // namespace BeatMate::UI
