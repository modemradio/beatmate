#include "LicenseDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"

namespace BeatMate::UI {

static std::unique_ptr<juce::TextEditor> makeEditor(const juce::String& placeholder, int maxChars = 0)
{
    auto e = std::make_unique<juce::TextEditor>();
    e->setColour(juce::TextEditor::backgroundColourId,    Colors::bgLight());
    e->setColour(juce::TextEditor::textColourId,          Colors::textPrimary());
    e->setColour(juce::TextEditor::outlineColourId,       Colors::border());
    e->setColour(juce::TextEditor::focusedOutlineColourId,Colors::borderFocus());
    e->setTextToShowWhenEmpty(placeholder, Colors::textDim());
    if (maxChars > 0) e->setInputRestrictions(maxChars);
    return e;
}

static std::unique_ptr<juce::Label> makeLabel(const juce::String& text)
{
    auto l = std::make_unique<juce::Label>(juce::String(), text);
    l->setFont(juce::Font(11.0f));
    l->setColour(juce::Label::textColourId, Colors::textDim());
    return l;
}

LicenseDialog::LicenseDialog()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("dialog.license.title"));
    m_titleLabel->setFont(juce::Font(18.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_keyEdit = makeEditor("XXXXX-XXXXX-XXXXX-XXXXX", 23);
    m_keyEdit->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
    m_keyEdit->setJustification(juce::Justification::centred);
    addAndMakeVisible(*m_keyEdit);

    m_prenomLbl = makeLabel(juce::String::fromUTF8(u8"Prénom")); addAndMakeVisible(*m_prenomLbl);
    m_prenomEdit = makeEditor(juce::String::fromUTF8(u8"Prénom")); addAndMakeVisible(*m_prenomEdit);

    m_nomLbl = makeLabel("Nom"); addAndMakeVisible(*m_nomLbl);
    m_nomEdit = makeEditor("Nom"); addAndMakeVisible(*m_nomEdit);

    m_emailLbl = makeLabel("Email"); addAndMakeVisible(*m_emailLbl);
    m_emailEdit = makeEditor("vous@exemple.com"); addAndMakeVisible(*m_emailEdit);

    m_statusLabel = std::make_unique<juce::Label>("st", BM_TJ("dialog.license.statusTrial"));
    m_statusLabel->setFont(juce::Font(12.0f));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::warning());
    addAndMakeVisible(*m_statusLabel);

    m_cancelBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.license.cancel"));
    m_cancelBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_cancelBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_cancelBtn->onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(*m_cancelBtn);

    m_activateBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.license.activate"));
    m_activateBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_activateBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_activateBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_activateBtn->onClick = [this] { onActivate(); };
    addAndMakeVisible(*m_activateBtn);

    setSize(480, 420);
}

static bool isValidEmail(const juce::String& e)
{
    auto s = e.trim();
    if (s.isEmpty() || s.length() > 254) return false;
    int at = s.indexOfChar('@');
    if (at <= 0 || at >= s.length() - 1) return false;
    return s.substring(at + 1).contains(".");
}

void LicenseDialog::onActivate()
{
    auto key    = m_keyEdit->getText().trim();
    auto email  = m_emailEdit->getText().trim();
    auto prenom = m_prenomEdit->getText().trim();
    auto nom    = m_nomEdit->getText().trim();

    if (key.length() != 23) {
        setStatus(BM_TJ("dialog.license.invalidShort"), Colors::error());
        return;
    }
    if (prenom.isEmpty() || nom.isEmpty() || !isValidEmail(email)) {
        setStatus(juce::String::fromUTF8(u8"Renseignez prénom, nom et un email valide."), Colors::error());
        return;
    }

    setBusy(true);
    setStatus(BM_TJ("dialog.license.activating"), Colors::primary());

    if (onActivationRequestedFull) {
        onActivationRequestedFull({key, email, prenom, nom});
    }
    if (onActivationRequested) {
        onActivationRequested(key);
    }
}

void LicenseDialog::setStatus(const juce::String& msg, juce::Colour colour)
{
    m_statusLabel->setText(msg, juce::dontSendNotification);
    m_statusLabel->setColour(juce::Label::textColourId, colour);
}

void LicenseDialog::setBusy(bool busy)
{
    m_activateBtn->setEnabled(!busy);
    m_keyEdit   ->setReadOnly(busy);
    m_emailEdit ->setReadOnly(busy);
    m_prenomEdit->setReadOnly(busy);
    m_nomEdit   ->setReadOnly(busy);
}

juce::String LicenseDialog::licenseKey() const { return m_keyEdit->getText().trim(); }

void LicenseDialog::paint(juce::Graphics& g) { ProDraw::viewBackground(g, getWidth(), getHeight()); }

void LicenseDialog::resized()
{
    auto area = getLocalBounds().reduced(24);
    m_titleLabel->setBounds(area.removeFromTop(26)); area.removeFromTop(14);
    m_keyEdit   ->setBounds(area.removeFromTop(40)); area.removeFromTop(14);

    auto rowFor = [&](juce::Label& lbl, juce::TextEditor& edit) {
        auto row = area.removeFromTop(48);
        lbl.setBounds(row.removeFromTop(16));
        edit.setBounds(row.removeFromTop(28));
        area.removeFromTop(6);
    };
    rowFor(*m_prenomLbl, *m_prenomEdit);
    rowFor(*m_nomLbl,    *m_nomEdit);
    rowFor(*m_emailLbl,  *m_emailEdit);

    m_statusLabel->setBounds(area.removeFromTop(20));
    auto btnRow = area.removeFromBottom(30);
    m_activateBtn->setBounds(btnRow.removeFromRight(100)); btnRow.removeFromRight(8);
    m_cancelBtn  ->setBounds(btnRow.removeFromRight(100));
}

} // namespace BeatMate::UI
