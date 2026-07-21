#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class BPMTapDetector : public juce::Component {
public:
    BPMTapDetector(); ~BPMTapDetector() override=default;
    double detectedBPM() const{return m_bpm;}
    class Listener{public:virtual ~Listener()=default;virtual void bpmDetected(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
    void onTap(); void onReset(); void resized() override;
private:
    void setupUI(); void calculateBPM();
    std::unique_ptr<juce::TextButton> m_tapBtn,m_resetBtn;
    std::unique_ptr<juce::Label> m_bpmLabel,m_tapCountLabel;
    std::vector<int64_t> m_tapTimes; double m_bpm=0.0;
    static constexpr int kMaxTaps=16;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BPMTapDetector)
};
} // namespace BeatMate::UI
