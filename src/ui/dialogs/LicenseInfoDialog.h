#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class LicenseInfoDialog : public juce::Component {
public:
    LicenseInfoDialog();
    ~LicenseInfoDialog() override = default;
    void setLicenseType(const juce::String& type);
    void setActivationDate(const juce::String& date);
    void setExpirationDate(const juce::String& date);
    void setMachineId(const juce::String& id);
    void paint(juce::Graphics& g) override;
    void resized() override;
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_typeLbl, m_typeVal;
    std::unique_ptr<juce::Label> m_activationLbl, m_activationVal;
    std::unique_ptr<juce::Label> m_expirationLbl, m_expirationVal;
    std::unique_ptr<juce::Label> m_machineIdLbl, m_machineIdVal;
    std::unique_ptr<juce::Label> m_featuresLabel;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseInfoDialog)
};
}
