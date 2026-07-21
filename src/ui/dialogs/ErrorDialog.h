#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ErrorDialog : public juce::Component {
public:
    ErrorDialog(const juce::String& message, const juce::String& details = {});
    ~ErrorDialog() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    static void showError(const juce::String& message, const juce::String& details = {},
                          juce::Component* parent = nullptr);
private:
    juce::String m_message, m_details;
    std::unique_ptr<juce::TextEditor> m_detailsEdit;
    std::unique_ptr<juce::TextButton> m_copyBtn, m_okBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ErrorDialog)
};
} // namespace BeatMate::UI
