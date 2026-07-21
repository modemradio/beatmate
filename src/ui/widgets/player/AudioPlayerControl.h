#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../controls/RotaryKnobWidget.h"
namespace BeatMate::UI {
class AudioPlayerControl : public juce::Component {
public:
    AudioPlayerControl(); ~AudioPlayerControl() override=default;
    void setTrackInfo(const juce::String& title,const juce::String& artist,double bpm,const juce::String& key);
    void setPosition(int posMs,int totalMs);void setPlaying(bool playing);
    void resized() override;
    class Listener{public:virtual ~Listener()=default;virtual void playPauseClicked(){}virtual void stopClicked(){}virtual void seekRequested(int){}virtual void volumeChanged(double){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    void setupUI();
    std::unique_ptr<juce::TextButton> m_playPauseBtn,m_stopBtn;
    std::unique_ptr<juce::Slider> m_seekSlider;
    std::unique_ptr<juce::Label> m_posLabel,m_durationLabel,m_titleLabel,m_bpmLabel,m_keyLabel;
    std::unique_ptr<RotaryKnobWidget> m_volumeKnob;
    bool m_playing=false;
    juce::ListenerList<Listener> m_listeners;
    struct VolumeListener : public RotaryKnobWidget::Listener {
        AudioPlayerControl& owner;
        VolumeListener(AudioPlayerControl& o) : owner(o) {}
        void valueChanged(double v) override { owner.m_listeners.call([v](AudioPlayerControl::Listener& l) { l.volumeChanged(v); }); }
    };
    std::unique_ptr<VolumeListener> m_volumeListener;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPlayerControl)
};
}
