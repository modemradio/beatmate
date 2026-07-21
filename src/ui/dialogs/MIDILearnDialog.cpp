#include "MIDILearnDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
#include <spdlog/spdlog.h>

namespace BeatMate::UI {

juce::File MIDILearnDialog::defaultMappingFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("BeatMate").getChildFile("midi_mappings.json");
}

MIDILearnDialog::MIDILearnDialog(Core::MIDIMapping* mapping, juce::String actionId)
    : m_mapping(mapping), m_actionId(std::move(actionId))
{
    setOpaque(false);
    setSize(420, 280);

    auto makeLabel = [this](const juce::String& text, juce::Colour colour, float size, bool bold) {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
        l->setColour(juce::Label::textColourId, colour);
        addAndMakeVisible(*l);
        return l;
    };

    m_title       = makeLabel(BM_TJ("dialog.midi.title"),
                               Colors::textPrimary(), 16.0f, true);
    m_actionLabel = makeLabel(BM_TJ("dialog.midi.actionPrefix") + m_actionId,
                               Colors::accent(), 12.0f, false);
    m_channelLabel = makeLabel(BM_TJ("dialog.midi.channel"),
                               Colors::textSecondary(), 11.0f, false);
    m_numberLabel  = makeLabel(BM_TJ("dialog.midi.number"),
                               Colors::textSecondary(), 11.0f, false);
    m_statusLabel  = makeLabel(BM_TJ("dialog.midi.waiting"),
                               Colors::textMuted(), 10.0f, false);

    auto makeEdit = [this](const juce::String& def) {
        auto e = std::make_unique<juce::TextEditor>();
        e->setText(def);
        e->setJustification(juce::Justification::centred);
        e->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold));
        e->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
        e->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        e->setColour(juce::TextEditor::outlineColourId, Colors::border());
        addAndMakeVisible(*e);
        return e;
    };
    m_channelEdit = makeEdit("1");
    m_numberEdit  = makeEdit("0");
    m_channelEdit->setInputRestrictions(2, "0123456789");
    m_numberEdit ->setInputRestrictions(3, "0123456789");

    m_isCcToggle = std::make_unique<juce::ToggleButton>(BM_TJ("dialog.midi.isCc"));
    m_isCcToggle->setToggleState(true, juce::dontSendNotification);
    m_isCcToggle->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_isCcToggle);

    auto makeBtn = [this](const juce::String& t, juce::Colour bg, bool primary) {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::buttonOnColourId, primary ? bg.brighter(0.2f) : Colors::primary());
        b->setColour(juce::TextButton::textColourOffId, primary ? juce::Colours::white : Colors::textPrimary());
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(*b);
        return b;
    };
    m_okButton     = makeBtn(BM_TJ("dialog.midi.assign"), Colors::primary(), true);
    m_cancelButton = makeBtn(BM_TJ("dialog.midi.cancel"),  Colors::bgLighter(), false);
    m_okButton->onClick     = [this]() { commit(); };
    m_cancelButton->onClick = [this]() { cancel(); };
}

void MIDILearnDialog::setLastReceived(int channel, int ccOrNote, bool isCC)
{
    if (m_channelEdit) m_channelEdit->setText(juce::String(channel), juce::dontSendNotification);
    if (m_numberEdit)  m_numberEdit ->setText(juce::String(ccOrNote), juce::dontSendNotification);
    if (m_isCcToggle)  m_isCcToggle ->setToggleState(isCC, juce::dontSendNotification);
    if (m_statusLabel)
        m_statusLabel->setText(BM_TJ("dialog.midi.receivedPrefix") + juce::String(channel)
                                + (isCC ? " CC " : " Note ") + juce::String(ccOrNote),
                               juce::dontSendNotification);
}

void MIDILearnDialog::commit()
{
    const int channel  = juce::jlimit(1, 16, m_channelEdit->getText().getIntValue());
    const int number   = juce::jlimit(0, 127, m_numberEdit ->getText().getIntValue());
    const bool isCC    = m_isCcToggle->getToggleState();

    Core::MIDIMappingEntry entry;
    entry.channel   = channel;
    entry.ccOrNote  = number;
    entry.isCC      = isCC;
    entry.action    = m_actionId.toStdString();
    entry.minValue  = 0.0f;
    entry.maxValue  = 1.0f;

    if (m_mapping) {
        m_mapping->addMapping(entry);
        const auto file = defaultMappingFile();
        file.getParentDirectory().createDirectory();
        if (m_mapping->save(file.getFullPathName().toStdString()))
            spdlog::info("[MIDILearn] Saved mapping '{}' → ch={} {}={}",
                          entry.action, channel, isCC ? "CC" : "Note", number);
        else
            spdlog::warn("[MIDILearn] Could not persist mapping file");
    }

    if (onCompleted) onCompleted(&entry);
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
}

void MIDILearnDialog::cancel()
{
    if (onCompleted) onCompleted(nullptr);
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}

void MIDILearnDialog::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDarker());
    g.setColour(Colors::border());
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void MIDILearnDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    m_title       ->setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    m_actionLabel ->setBounds(area.removeFromTop(20));
    area.removeFromTop(12);

    auto row = area.removeFromTop(26);
    m_channelLabel->setBounds(row.removeFromLeft(160));
    row.removeFromLeft(8);
    m_channelEdit ->setBounds(row.removeFromLeft(80));
    area.removeFromTop(10);

    row = area.removeFromTop(26);
    m_numberLabel ->setBounds(row.removeFromLeft(160));
    row.removeFromLeft(8);
    m_numberEdit  ->setBounds(row.removeFromLeft(80));
    area.removeFromTop(10);

    m_isCcToggle  ->setBounds(area.removeFromTop(24));
    area.removeFromTop(12);
    m_statusLabel ->setBounds(area.removeFromTop(22));

    area.removeFromTop(12);
    auto btnRow = area.removeFromTop(32);
    m_cancelButton->setBounds(btnRow.removeFromRight(110));
    btnRow.removeFromRight(8);
    m_okButton    ->setBounds(btnRow.removeFromRight(110));
}

void MIDILearnDialog::showAsync(Core::MIDIMapping* mapping,
                                  const juce::String& actionId,
                                  std::function<void(const Core::MIDIMappingEntry*)> done)
{
    auto* dlg = new MIDILearnDialog(mapping, actionId);
    dlg->onCompleted = std::move(done);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(dlg); // DialogWindow takes ownership.
    opts.dialogTitle = BM_TJ("dialog.midi.title");
    opts.dialogBackgroundColour = Colors::bgDarker();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = false;
    opts.resizable = false;
    opts.launchAsync();
}

} // namespace BeatMate::UI
