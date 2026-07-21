#include "LicenseDialogShared.h"
#include "SharedLicense.h"
#include "services/security/LicenseService.h"

namespace BeatMate::Shared {

using Services::Security::LicenseState;

static juce::String formatExpiry(int64_t epoch)
{
    if (epoch <= 0) return "sans expiration";
    juce::Time t(epoch * 1000LL);
    return t.formatted("%d/%m/%Y");
}

LicenseDialogShared::LicenseDialogShared()
{
    auto setupLabel = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, juce::Colour(0xffB8C0CC));
        l.setFont(juce::Font(13.0f));
        addAndMakeVisible(l);
    };
    auto setupEdit = [this](juce::TextEditor& e, const juce::String& placeholder, bool password = false) {
        e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1A1F2A));
        e.setColour(juce::TextEditor::textColourId,       juce::Colours::white);
        e.setColour(juce::TextEditor::outlineColourId,    juce::Colour(0xff2A3240));
        e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff6C4CFF));
        e.setTextToShowWhenEmpty(placeholder, juce::Colour(0xff5A6473));
        if (password) e.setPasswordCharacter(0x2022);
        addAndMakeVisible(e);
    };

    titleLabel_.setText("Licence BeatMate", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel_);

    stateLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    stateLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(stateLabel_);

    setupLabel(keyLabel_,    "Cle de licence");
    setupLabel(emailLabel_,  "Email d'achat");
    setupLabel(prenomLabel_, "Prenom");
    setupLabel(nomLabel_,    "Nom");
    setupEdit(keyEdit_,    "XXXXX-XXXXX-XXXXX-XXXXX");
    setupEdit(emailEdit_,  "vous@exemple.com");
    setupEdit(prenomEdit_, "Prenom");
    setupEdit(nomEdit_,    "Nom");
    keyEdit_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));

    for (auto* b : { &activateBtn_, &changeBtn_, &removeBtn_, &closeBtn_, &saveServerBtn_ })
        b->addListener(this);

    activateBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6C4CFF));
    changeBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2A3240));
    removeBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff8A2A3A));
    closeBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff2A3240));
    saveServerBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2A3240));

    addAndMakeVisible(activateBtn_);
    addAndMakeVisible(changeBtn_);
    addAndMakeVisible(removeBtn_);
    addAndMakeVisible(closeBtn_);

    serverToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff8A93A0));
    serverToggle_.onClick = [this] { resized(); repaint(); };
    addAndMakeVisible(serverToggle_);

    setupLabel(baseUrlLabel_, "URL serveur");
    setupLabel(apiKeyLabel_,  "Cle API");
    setupEdit(baseUrlEdit_, "https://beatmate.fr");
    setupEdit(apiKeyEdit_,  "cle API du site", true);
    addAndMakeVisible(saveServerBtn_);

    statusLabel_.setFont(juce::Font(12.5f));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    auto creds = SharedLicense::instance().credentials();
    baseUrlEdit_.setText(creds.baseUrl, false);
    apiKeyEdit_.setText(creds.apiKey, false);

    setSize(520, 600);
    refreshState();
}

LicenseDialogShared::~LicenseDialogShared() = default;

void LicenseDialogShared::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0E1118));
    g.setColour(juce::Colour(0xff1A1F2A));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 10.0f);
}

void LicenseDialogShared::resized()
{
    auto r = getLocalBounds().reduced(24);
    titleLabel_.setBounds(r.removeFromTop(32));
    stateLabel_.setBounds(r.removeFromTop(40));
    r.removeFromTop(6);

    const bool full = SharedLicense::instance().isFullLicense();

    auto field = [&r](juce::Label& l, juce::TextEditor& e) {
        l.setBounds(r.removeFromTop(18));
        e.setBounds(r.removeFromTop(30));
        r.removeFromTop(8);
    };

    const bool showFields = ! full;
    keyLabel_.setVisible(showFields);    keyEdit_.setVisible(showFields);
    emailLabel_.setVisible(showFields);  emailEdit_.setVisible(showFields);
    prenomLabel_.setVisible(showFields); prenomEdit_.setVisible(showFields);
    nomLabel_.setVisible(showFields);    nomEdit_.setVisible(showFields);

    if (showFields)
    {
        field(keyLabel_, keyEdit_);
        field(emailLabel_, emailEdit_);
        auto rowTop = r.removeFromTop(48);
        auto left = rowTop.removeFromLeft(rowTop.getWidth() / 2 - 6);
        prenomLabel_.setBounds(left.removeFromTop(18));
        prenomEdit_.setBounds(left.removeFromTop(30));
        rowTop.removeFromLeft(12);
        nomLabel_.setBounds(rowTop.removeFromTop(18));
        nomEdit_.setBounds(rowTop.removeFromTop(30));
        r.removeFromTop(10);
    }

    auto btnRow = r.removeFromTop(38);
    if (full)
    {
        changeBtn_.setVisible(true);
        removeBtn_.setVisible(true);
        activateBtn_.setVisible(false);
        changeBtn_.setBounds(btnRow.removeFromLeft(btnRow.getWidth() / 2 - 6));
        btnRow.removeFromLeft(12);
        removeBtn_.setBounds(btnRow);
    }
    else
    {
        activateBtn_.setVisible(true);
        changeBtn_.setVisible(false);
        removeBtn_.setVisible(false);
        activateBtn_.setBounds(btnRow);
    }
    r.removeFromTop(10);

    serverToggle_.setBounds(r.removeFromTop(26));
    const bool showServer = serverToggle_.getToggleState();
    baseUrlLabel_.setVisible(showServer); baseUrlEdit_.setVisible(showServer);
    apiKeyLabel_.setVisible(showServer);  apiKeyEdit_.setVisible(showServer);
    saveServerBtn_.setVisible(showServer);
    if (showServer)
    {
        field(baseUrlLabel_, baseUrlEdit_);
        field(apiKeyLabel_, apiKeyEdit_);
        saveServerBtn_.setBounds(r.removeFromTop(32).removeFromRight(200));
        r.removeFromTop(8);
    }

    statusLabel_.setBounds(r.removeFromTop(40));
    closeBtn_.setBounds(getLocalBounds().reduced(24).removeFromBottom(34).removeFromRight(120));
}

void LicenseDialogShared::refreshState()
{
    auto& lic = SharedLicense::instance();
    switch (lic.state())
    {
        case LicenseState::Licensed:
            stateLabel_.setText("Licence active : " + juce::String(lic.licenseType())
                                + "  (" + juce::String(lic.licenseKeyMasked()) + ")  expire le "
                                + formatExpiry(lic.expiresAtEpoch()),
                                juce::dontSendNotification);
            stateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff3DD68C));
            break;
        case LicenseState::Trial:
            stateLabel_.setText("Periode d'essai : " + juce::String(lic.trialDaysRemaining())
                                + " jour(s) restant(s). Activez votre licence BeatMate.",
                                juce::dontSendNotification);
            stateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffF5A623));
            break;
        case LicenseState::TrialExpired:
            stateLabel_.setText("Essai expire. Activez votre licence BeatMate pour continuer.",
                                juce::dontSendNotification);
            stateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffFF5470));
            break;
        case LicenseState::Unlicensed:
        default:
            stateLabel_.setText("Aucune licence. Activez votre licence BeatMate.",
                                juce::dontSendNotification);
            stateLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffF5A623));
            break;
    }
    resized();
    repaint();
}

void LicenseDialogShared::setBusy(bool busy)
{
    busy_ = busy;
    activateBtn_.setEnabled(! busy);
    changeBtn_.setEnabled(! busy);
    removeBtn_.setEnabled(! busy);
    saveServerBtn_.setEnabled(! busy);
}

void LicenseDialogShared::setStatus(const juce::String& msg, juce::Colour colour)
{
    statusLabel_.setText(msg, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, colour);
}

void LicenseDialogShared::buttonClicked(juce::Button* b)
{
    if (b == &activateBtn_)        doActivate();
    else if (b == &changeBtn_)     doChange();
    else if (b == &removeBtn_)     doRemove();
    else if (b == &saveServerBtn_) doSaveServer();
    else if (b == &closeBtn_)
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    }
}

void LicenseDialogShared::doActivate()
{
    const auto key    = keyEdit_.getText().trim();
    const auto email  = emailEdit_.getText().trim();
    const auto prenom = prenomEdit_.getText().trim();
    const auto nom    = nomEdit_.getText().trim();

    if (key.length() != 23)
    {
        setStatus("Cle invalide (format XXXXX-XXXXX-XXXXX-XXXXX).", juce::Colour(0xffFF5470));
        return;
    }
    if (! email.containsChar('@') || email.length() < 5)
    {
        setStatus("Email invalide.", juce::Colour(0xffFF5470));
        return;
    }
    if (prenom.isEmpty() || nom.isEmpty())
    {
        setStatus("Prenom et nom requis.", juce::Colour(0xffFF5470));
        return;
    }

    setBusy(true);
    setStatus("Activation en cours...", juce::Colour(0xff8A93A0));

    juce::Component::SafePointer<LicenseDialogShared> self(this);
    SharedLicense::instance().activate(
        key.toStdString(), email.toStdString(), prenom.toStdString(), nom.toStdString(),
        [self](ActivationOutcome out)
        {
            if (self == nullptr) return;
            self->setBusy(false);
            self->setStatus(out.message, out.success ? juce::Colour(0xff3DD68C) : juce::Colour(0xffFF5470));
            if (out.success) self->refreshState();
        });
}

void LicenseDialogShared::doChange()
{
    setBusy(true);
    setStatus("Desactivation de la licence courante...", juce::Colour(0xff8A93A0));
    juce::Component::SafePointer<LicenseDialogShared> self(this);
    SharedLicense::instance().deactivate(
        [self](ActivationOutcome out)
        {
            if (self == nullptr) return;
            self->setBusy(false);
            self->setStatus(out.message + " Saisissez la nouvelle cle.", juce::Colour(0xff8A93A0));
            self->keyEdit_.clear();
            self->refreshState();
        });
}

void LicenseDialogShared::doRemove()
{
    setBusy(true);
    setStatus("Suppression de la licence...", juce::Colour(0xff8A93A0));
    juce::Component::SafePointer<LicenseDialogShared> self(this);
    SharedLicense::instance().deactivate(
        [self](ActivationOutcome out)
        {
            if (self == nullptr) return;
            self->setBusy(false);
            self->setStatus(out.message, juce::Colour(0xff3DD68C));
            self->refreshState();
        });
}

void LicenseDialogShared::doSaveServer()
{
    WpCredentials c;
    c.baseUrl = baseUrlEdit_.getText().trim().toStdString();
    c.apiKey  = apiKeyEdit_.getText().trim().toStdString();
    SharedLicense::instance().saveCredentials(c);
    setStatus("Configuration serveur enregistree.", juce::Colour(0xff3DD68C));
}

void LicenseDialogShared::show(juce::Component* parentForCentering, const juce::String& appName)
{
    auto content = std::make_unique<LicenseDialogShared>();
    if (appName.isNotEmpty())
        content->titleLabel_.setText("Licence BeatMate — " + appName, juce::dontSendNotification);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content.release());
    opts.dialogTitle = "Licence BeatMate";
    opts.dialogBackgroundColour = juce::Colour(0xff0E1118);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    if (parentForCentering != nullptr)
        opts.componentToCentreAround = parentForCentering;
    opts.launchAsync();
}

} // namespace BeatMate::Shared
