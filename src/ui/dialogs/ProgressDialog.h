#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ProgressDialog : public juce::Component, public juce::Timer {
public:
    explicit ProgressDialog(const juce::String& title);
    ~ProgressDialog() override = default;
    void setProgress(int value);
    void setStatus(const juce::String& text);
    void setRange(int min, int max);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    std::function<void()> onCancelled;
private:
    void updateETA();
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_statusLabel;
    std::unique_ptr<juce::Label> m_etaLabel;
    std::unique_ptr<juce::TextButton> m_cancelBtn;
    int m_min = 0, m_max = 100, m_value = 0;
    juce::int64 m_startTime = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgressDialog)
};
} // namespace BeatMate::UI
