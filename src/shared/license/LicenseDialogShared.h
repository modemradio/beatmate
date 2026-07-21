#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::Shared {

class LicenseDialogShared : public juce::Component,
                            private juce::Button::Listener
{
public:
    LicenseDialogShared();
    ~LicenseDialogShared() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static void show(juce::Component* parentForCentering, const juce::String& appName);

private:
    void buttonClicked(juce::Button*) override;
    void refreshState();
    void setBusy(bool busy);
    void setStatus(const juce::String& msg, juce::Colour colour);
    void doActivate();
    void doChange();
    void doRemove();
    void doSaveServer();

    juce::Label   titleLabel_;
    juce::Label   stateLabel_;

    juce::Label   keyLabel_, emailLabel_, prenomLabel_, nomLabel_;
    juce::TextEditor keyEdit_, emailEdit_, prenomEdit_, nomEdit_;

    juce::TextButton activateBtn_ { "Activer la licence" };
    juce::TextButton changeBtn_   { "Changer de licence" };
    juce::TextButton removeBtn_   { "Supprimer la licence" };
    juce::TextButton closeBtn_    { "Fermer" };

    juce::ToggleButton serverToggle_ { "Configuration serveur (avance)" };
    juce::Label   baseUrlLabel_, apiKeyLabel_;
    juce::TextEditor baseUrlEdit_, apiKeyEdit_;
    juce::TextButton saveServerBtn_ { "Enregistrer le serveur" };

    juce::Label   statusLabel_;

    bool busy_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseDialogShared)
};

} // namespace BeatMate::Shared
