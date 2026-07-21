#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class CamelotWheelWidget : public juce::Component {
public:
    CamelotWheelWidget(); ~CamelotWheelWidget() override=default;
    void setHighlightedKey(const juce::String& key);void setCompatibleKeys(const juce::StringArray& keys);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    class Listener{public:virtual ~Listener()=default;virtual void keyClicked(const juce::String&){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    juce::String keyAtPosition(juce::Point<int> pos) const;
    juce::String m_highlightedKey;juce::StringArray m_compatibleKeys;juce::String m_hoveredKey;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CamelotWheelWidget)
};
} // namespace BeatMate::UI
