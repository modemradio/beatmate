#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class QuickActionBar : public juce::Component {
public:
    QuickActionBar(); ~QuickActionBar() override=default;
    void resized() override;void paint(juce::Graphics& g) override;
    class Listener{public:virtual ~Listener()=default;virtual void importClicked(){}virtual void analyzeClicked(){}virtual void exportClicked(){}virtual void normalizeClicked(){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    std::unique_ptr<juce::TextButton> m_importBtn,m_analyzeBtn,m_normalizeBtn,m_exportBtn;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickActionBar)
};
} // namespace BeatMate::UI
