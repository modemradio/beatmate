#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class DocumentationDialog : public juce::Component {
public:
    DocumentationDialog();
    ~DocumentationDialog() override = default;
    void setContent(const juce::String& text);
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    std::unique_ptr<juce::TextEditor> m_browser;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DocumentationDialog)
};
} // namespace BeatMate::UI
