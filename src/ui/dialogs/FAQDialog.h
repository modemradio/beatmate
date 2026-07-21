#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class FAQDialog : public juce::Component {
public:
    FAQDialog();
    ~FAQDialog() override = default;
    void addFAQ(const juce::String& question, const juce::String& answer);
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    struct FAQItem { juce::String question, answer; bool expanded = false; };
    std::vector<FAQItem> m_faqs;
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextEditor> m_faqDisplay;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    void refreshDisplay();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FAQDialog)
};
} // namespace BeatMate::UI
