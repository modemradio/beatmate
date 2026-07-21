#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class ExportDialog : public juce::Component {
public:
    ExportDialog();
    ~ExportDialog() override = default;
    juce::String format() const;
    int bitrate() const;
    juce::String destination() const;
    bool exportMetadata() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    static int showDialog(juce::Component* parent = nullptr);
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_formatLabel, m_bitrateLabel, m_sampleRateLabel;
    std::unique_ptr<juce::ComboBox> m_formatCombo, m_bitrateCombo, m_sampleRateCombo;
    std::unique_ptr<juce::TextEditor> m_destEdit;
    std::unique_ptr<juce::TextButton> m_browseBtn;
    std::unique_ptr<juce::ToggleButton> m_metadataCheck, m_cuePointsCheck, m_normalizeCheck;
    std::unique_ptr<juce::TextButton> m_cancelBtn, m_okBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportDialog)
};
}
