#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class CamelotWheelDialog : public juce::Component {
public:
    CamelotWheelDialog();
    ~CamelotWheelDialog() override = default;
    void setCurrentKey(const juce::String& key);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    std::function<void(const juce::String&)> onKeySelected;
    std::function<void(const juce::StringArray&)> onFilterByCompatible;
private:
    void drawCamelotWheel(juce::Graphics& g);
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_selectedKeyLabel;
    std::unique_ptr<juce::Label> m_compatibleLabel;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    juce::String m_highlightedKey;
    juce::Rectangle<float> m_wheelArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CamelotWheelDialog)
};
} // namespace BeatMate::UI
