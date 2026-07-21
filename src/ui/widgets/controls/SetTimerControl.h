#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {

class SetTimerControl : public juce::Component, public juce::Timer {
public:
    SetTimerControl();
    ~SetTimerControl() override = default;

    void start();
    void stop();
    void reset();
    bool isRunning() const { return m_running; }

    void setTargetDuration(double seconds) { m_targetDuration = seconds; repaint(); }
    double getElapsedSeconds() const { return m_elapsed; }

    void setWarningThreshold(double secondsRemaining) { m_warningThreshold = secondsRemaining; }

    void paint(juce::Graphics& g) override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default;
        virtual void timerStarted() {} virtual void timerStopped() {}
        virtual void timerWarning() {} virtual void timerExpired() {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    juce::String formatDuration(double secs) const;

    bool m_running = false;
    double m_elapsed = 0.0;
    double m_targetDuration = 0.0;
    double m_warningThreshold = 300.0;   // 5 min warning
    bool m_warningFired = false;
    bool m_expiredFired = false;
    std::chrono::steady_clock::time_point m_startTime;
    double m_pausedElapsed = 0.0;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SetTimerControl)
};

}
