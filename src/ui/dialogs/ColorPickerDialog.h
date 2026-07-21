#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ColorPickerDialog : public juce::Component {
public:
    explicit ColorPickerDialog(juce::Colour initial=juce::Colours::white);
    ~ColorPickerDialog() override = default;
    juce::Colour selectedColor() const { return m_color; }
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    std::function<void(juce::Colour)> onColorChanged;
    static int showDialog(juce::Component* parent, juce::Colour initial, std::function<void(juce::Colour)> callback);
private:
    void updatePreview();
    juce::Colour m_color;
    std::unique_ptr<juce::TextEditor> m_hexEdit;
    juce::Rectangle<float> m_wheelArea;
    std::unique_ptr<juce::TextButton> m_okBtn, m_cancelBtn;
    juce::Array<juce::Colour> m_swatches;
    juce::OwnedArray<juce::TextButton> m_swatchBtns;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColorPickerDialog)
};
} // namespace BeatMate::UI
