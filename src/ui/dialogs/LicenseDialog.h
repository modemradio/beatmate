#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
struct LicenseActivationRequest {
    juce::String key;
    juce::String email;
    juce::String prenom;
    juce::String nom;
};

class LicenseDialog : public juce::Component {
public:
    LicenseDialog();
    ~LicenseDialog() override = default;
    juce::String licenseKey() const;
    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(const LicenseActivationRequest&)> onActivationRequestedFull;

    // Legacy callback (key-only). Still fired for backward compat with old call sites.
    std::function<void(const juce::String&)> onActivationRequested;

    void setStatus(const juce::String& msg, juce::Colour colour);
    void setBusy(bool busy);

private:
    void onActivate();
    std::unique_ptr<juce::Label>      m_titleLabel;
    std::unique_ptr<juce::TextEditor> m_keyEdit;
    std::unique_ptr<juce::Label>      m_emailLbl,  m_prenomLbl, m_nomLbl;
    std::unique_ptr<juce::TextEditor> m_emailEdit, m_prenomEdit, m_nomEdit;
    std::unique_ptr<juce::Label>      m_statusLabel;
    std::unique_ptr<juce::TextButton> m_activateBtn, m_cancelBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseDialog)
};
} // namespace BeatMate::UI
