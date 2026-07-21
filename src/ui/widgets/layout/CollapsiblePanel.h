#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class CollapsiblePanel : public juce::Component {
public:
    CollapsiblePanel(const juce::String& title); ~CollapsiblePanel() override=default;
    void setContentWidget(juce::Component* widget);void setExpanded(bool expanded);bool isExpanded() const{return m_expanded;}
    void resized() override;void paint(juce::Graphics& g) override;
    class Listener{public:virtual ~Listener()=default;virtual void toggled(bool){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    std::unique_ptr<juce::TextButton> m_headerBtn;juce::Component* m_contentWidget=nullptr;bool m_expanded=true;juce::String m_title;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CollapsiblePanel)
};
}
