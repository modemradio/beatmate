#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class EulaDialog : public juce::Component {
public:
    EulaDialog();
    ~EulaDialog() override = default;
    bool isAccepted() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    static bool showDialog(juce::Component* parent = nullptr);
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextEditor> m_textBrowser;
    std::unique_ptr<juce::ToggleButton> m_acceptCheck;
    std::unique_ptr<juce::TextButton> m_acceptBtn, m_refuseBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EulaDialog)
};
} // namespace BeatMate::UI
