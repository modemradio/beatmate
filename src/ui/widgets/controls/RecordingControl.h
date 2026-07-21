#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {

class RecordingControl : public juce::Component, public juce::Timer {
public:
    RecordingControl();
    ~RecordingControl() override = default;

    void setRecording(bool recording);
    bool isRecording() const { return m_recording; }
    void setElapsedSeconds(double seconds);
    void setFileSize(int64_t bytes);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default; virtual void recordToggled(bool recording) {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    juce::String formatTime(double seconds) const;
    juce::String formatSize(int64_t bytes) const;

    bool m_recording = false;
    double m_elapsed = 0.0;
    int64_t m_fileSize = 0;
    bool m_flashPhase = false;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingControl)
};

} // namespace BeatMate::UI
