#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class BatchOperationsPanel : public juce::Component {
public:
    BatchOperationsPanel(); ~BatchOperationsPanel() override=default;
    void paint(juce::Graphics& g) override;void resized() override;
    void updateProgress(int current,int total);
    class Listener{public:virtual ~Listener()=default;virtual void startRequested(const juce::String&,const juce::StringArray&){}virtual void cancelled(){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    void setupUI();
    std::unique_ptr<juce::Label> m_titleLabel,m_statusLabel;
    std::unique_ptr<juce::ComboBox> m_operationCombo;
    std::unique_ptr<juce::TextButton> m_startBtn,m_cancelBtn;
    double m_progress=0.0;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchOperationsPanel)
};
} // namespace BeatMate::UI
