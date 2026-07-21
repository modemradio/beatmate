#include "FirstTimeSetupDialog.h"
#include "../styles/ColorPalette.h"
#include "../../services/config/I18n.h"
#include "../../services/config/LocalizationService.h"
#include "../../app/ServiceLocator.h"

namespace BeatMate::UI {

void FirstTimeSetupDialog::FoldersListModel::paintListBoxItem(
    int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(Colors::primary().withAlpha(0.2f));
    else if (row % 2 == 0)
        g.fillAll(Colors::bgDark());
    else
        g.fillAll(Colors::bgSurface());

    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(13.0f));
    if (folders && row < folders->size())
        g.drawText("  " + (*folders)[row], 0, 0, w, h, juce::Justification::centredLeft);
}

FirstTimeSetupDialog::FirstTimeSetupDialog()
{
    // NOTE: setSize() must come AFTER every child widget is constructed.

    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("dialog.setup.title"));
    m_titleLabel->setFont(juce::Font(22.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_subtitleLabel = std::make_unique<juce::Label>("st", "");
    m_subtitleLabel->setFont(juce::Font(13.0f));
    m_subtitleLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_subtitleLabel);

    m_prevBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.prev"));
    m_prevBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_prevBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_prevBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_prevBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_prevBtn->onClick = [this] {
        if (m_currentPage > 0)
            showPage(m_currentPage - 1);
    };
    addAndMakeVisible(*m_prevBtn);

    m_nextBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.next"));
    m_nextBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_nextBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_nextBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_nextBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_nextBtn->onClick = [this] {
        if (m_currentPage < (int)m_pages.size() - 1)
        {
            if (m_currentPage == (int)m_pages.size() - 2)
                updateSummary();
            showPage(m_currentPage + 1);
        }
        else
        {
            saveWizardSettings();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(1);
        }
    };
    addAndMakeVisible(*m_nextBtn);

    createPages();
    showPage(0);

    setSize(980, 720);
}

juce::Component* FirstTimeSetupDialog::createWelcomePage()
{
    auto* page = new juce::Component();

    struct LogoComp : public juce::Component, private juce::Timer {
        LogoComp() { startTimerHz(30); phase = 0.f; }
        ~LogoComp() override { stopTimer(); }
        void timerCallback() override { phase += 0.08f; repaint(); }
        void paint(juce::Graphics& g) override {
            auto b = getLocalBounds().toFloat();
            const float cx = b.getCentreX();
            const float cy = b.getCentreY();
            const float r  = juce::jmin(b.getWidth(), b.getHeight()) * 0.34f;
            const float pulse = 0.5f + 0.5f * std::sin(phase);

            for (int i = 3; i >= 1; --i) {
                float rr = r + 6.f + i * 4.f + pulse * 4.f;
                g.setColour(juce::Colour(0xFF6366F1)
                    .withAlpha((0.18f - i * 0.04f) * (0.5f + pulse * 0.5f)));
                g.fillEllipse(cx - rr, cy - rr, rr * 2, rr * 2);
            }

            juce::Path hex;
            for (int i = 0; i < 6; ++i) {
                float a = (i * 60.f - 90.f) * juce::MathConstants<float>::pi / 180.f;
                float hx = cx + std::cos(a) * r;
                float hy = cy + std::sin(a) * r;
                if (i == 0) hex.startNewSubPath(hx, hy);
                else        hex.lineTo(hx, hy);
            }
            hex.closeSubPath();

            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xFF3B82F6), cx - r, cy - r,
                juce::Colour(0xFF8B5CF6), cx + r, cy + r, false));
            g.fillPath(hex);

            g.saveState();
            g.reduceClipRegion(hex);
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::white.withAlpha(0.30f), cx, cy - r,
                juce::Colours::transparentWhite,        cx, cy, false));
            g.fillRect(b);
            g.restoreState();

            g.setColour(juce::Colour(0xFFA5B4FC));
            g.strokePath(hex, juce::PathStrokeType(2.0f));

            g.setFont(juce::Font(juce::FontOptions{}.withHeight(r * 0.95f).withStyle("Bold")));
            auto txtRect = juce::Rectangle<float>(cx - r, cy - r, r * 2, r * 2);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawText("BM", txtRect.translated(0.f, 2.f), juce::Justification::centred);
            g.setColour(juce::Colours::white);
            g.drawText("BM", txtRect, juce::Justification::centred);
        }
        float phase = 0.f;
    };
    auto* logo = new LogoComp();
    logo->setBounds(440, 30, 110, 110);
    page->addAndMakeVisible(logo);

    auto* welcomeTitle = new juce::Label("", BM_TJ("dialog.setup.welcomeTitle"));
    welcomeTitle->setFont(juce::Font(juce::FontOptions{}.withHeight(32.0f).withStyle("Bold")));
    welcomeTitle->setColour(juce::Label::textColourId, juce::Colour(0xFFF8FAFC));
    welcomeTitle->setJustificationType(juce::Justification::centred);
    welcomeTitle->setBounds(30, 160, 920, 44);
    page->addAndMakeVisible(welcomeTitle);
    m_welcomeTitleLbl = welcomeTitle;

    auto* byLbl = new juce::Label("", BM_TJ("dialog.setup.byAuthor"));
    byLbl->setFont(juce::Font(juce::FontOptions{}.withHeight(16.0f).withStyle("Italic")));
    byLbl->setColour(juce::Label::textColourId, juce::Colour(0xFF93A3FF));
    byLbl->setJustificationType(juce::Justification::centred);
    byLbl->setBounds(30, 208, 920, 26);
    page->addAndMakeVisible(byLbl);
    m_welcomeByLbl = byLbl;

    auto* taglineLbl = new juce::Label("", BM_TJ("dialog.setup.tagline"));
    taglineLbl->setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f)));
    taglineLbl->setColour(juce::Label::textColourId, juce::Colour(0xFF94A3B8));
    taglineLbl->setJustificationType(juce::Justification::centred);
    taglineLbl->setBounds(30, 242, 920, 24);
    page->addAndMakeVisible(taglineLbl);
    m_welcomeTagLbl = taglineLbl;

    auto* featLbl = new juce::Label("", BM_TJ("dialog.setup.features"));
    featLbl->setFont(juce::Font(juce::FontOptions{}.withHeight(14.5f)));
    featLbl->setColour(juce::Label::textColourId, juce::Colour(0xFFCBD5E1));
    featLbl->setJustificationType(juce::Justification::centred);
    featLbl->setBounds(60, 288, 860, 80);
    page->addAndMakeVisible(featLbl);
    m_welcomeFeatLbl = featLbl;

    auto* ctaLbl = new juce::Label("", BM_TJ("dialog.setup.cta"));
    ctaLbl->setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    ctaLbl->setColour(juce::Label::textColourId, juce::Colour(0xFF64748B));
    ctaLbl->setJustificationType(juce::Justification::centred);
    ctaLbl->setBounds(30, 380, 920, 22);
    page->addAndMakeVisible(ctaLbl);
    m_welcomeCtaLbl = ctaLbl;

    auto* langLabel = new juce::Label("", BM_TJ("dialog.setup.language"));
    langLabel->setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    langLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFCBD5E1));
    langLabel->setJustificationType(juce::Justification::centredRight);
    langLabel->setBounds(300, 440, 200, 30);
    page->addAndMakeVisible(langLabel);
    m_welcomeLangLbl = langLabel;

    m_languageCombo = std::make_unique<juce::ComboBox>();
    m_languageCombo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF12121A));
    m_languageCombo->setColour(juce::ComboBox::textColourId,       juce::Colour(0xFFF1F5F9));
    m_languageCombo->setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xFF3B82F6));
    m_languageCombo->addItem(BM_TJ("dialog.setup.langFr"), 1);
    m_languageCombo->addItem(BM_TJ("dialog.setup.langEn"), 2);
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        int selId = 1;
        if (g_serviceLocator) {
            if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>()) {
                selId = (svc->getCurrentLanguage() == "en") ? 2 : 1;
            }
        }
        m_languageCombo->setSelectedId(selId, juce::dontSendNotification);
    }
    m_languageCombo->setBounds(510, 440, 180, 30);
    m_languageCombo->onChange = [this]() { applyLanguageChange(); };
    page->addAndMakeVisible(*m_languageCombo);

    return page;
}

void FirstTimeSetupDialog::applyLanguageChange()
{
    // CRITICAL: don't call LocalizationService::setLanguage() here.
    if (!m_languageCombo) return;
    const bool en = (m_languageCombo->getSelectedId() == 2);

    struct Tx { const char* fr; const char* en; };
    auto tx = [en](const Tx& t) -> juce::String {
        return juce::String::fromUTF8(en ? t.en : t.fr);
    };

    const Tx txTitle     { "Bienvenue dans BeatMate V12",          "Welcome to BeatMate V12" };
    const Tx txBy        { "par Sebastien Sainte-Foi",             "by Sebastien Sainte-Foi" };
    const Tx txTag       { "La suite DJ professionnelle pour DJs exigeants",
                           "The professional DJ suite for demanding DJs" };
    const Tx txFeat      {
        "Cet assistant va configurer :\n"
        "    \xE2\x80\xA2  Vos dossiers de musique          \xE2\x80\xA2  Vos logiciels DJ (Rekordbox, Serato...)\n"
        "    \xE2\x80\xA2  Votre peripherique audio         \xE2\x80\xA2  Votre licence",
        "This wizard will configure:\n"
        "    \xE2\x80\xA2  Your music folders              \xE2\x80\xA2  Your DJ software (Rekordbox, Serato...)\n"
        "    \xE2\x80\xA2  Your audio device               \xE2\x80\xA2  Your license"
    };
    const Tx txCta       { "Cliquez sur \xC2\xAB Suivant \xC2\xBB pour commencer.",
                           "Click \xE2\x80\x9CNext\xE2\x80\x9D to begin." };
    const Tx txLangLbl   { "Langue / Language :", "Langue / Language :" };
    const Tx txLangFr    { "Fran\xC3\xA7""ais", "French" };
    const Tx txLangEn    { "Anglais",   "English" };
    const Tx txNext      { "Suivant",   "Next" };
    const Tx txPrev      { "Precedent", "Previous" };
    const Tx txStart     { "Commencer", "Start" };

    if (m_welcomeTitleLbl) m_welcomeTitleLbl->setText(tx(txTitle),   juce::dontSendNotification);
    if (m_welcomeByLbl)    m_welcomeByLbl   ->setText(tx(txBy),      juce::dontSendNotification);
    if (m_welcomeTagLbl)   m_welcomeTagLbl  ->setText(tx(txTag),     juce::dontSendNotification);
    if (m_welcomeFeatLbl)  m_welcomeFeatLbl ->setText(tx(txFeat),    juce::dontSendNotification);
    if (m_welcomeCtaLbl)   m_welcomeCtaLbl  ->setText(tx(txCta),     juce::dontSendNotification);
    if (m_welcomeLangLbl)  m_welcomeLangLbl ->setText(tx(txLangLbl), juce::dontSendNotification);

    {
        const int keepId = m_languageCombo->getSelectedId();
        m_languageCombo->clear(juce::dontSendNotification);
        m_languageCombo->addItem(tx(txLangFr), 1);
        m_languageCombo->addItem(tx(txLangEn), 2);
        m_languageCombo->setSelectedId(keepId > 0 ? keepId : 1, juce::dontSendNotification);
    }

    if (m_nextBtn) m_nextBtn->setButtonText(
        (m_currentPage == (int) m_pages.size() - 1) ? tx(txStart) : tx(txNext));
    if (m_prevBtn) m_prevBtn->setButtonText(tx(txPrev));
    repaint();
}

juce::Component* FirstTimeSetupDialog::createMusicFoldersPage()
{
    auto* page = new juce::Component();

    auto* desc = new juce::Label("", BM_TJ("dialog.setup.foldersDesc"));
    desc->setFont(juce::Font(14.0f));
    desc->setColour(juce::Label::textColourId, Colors::textSecondary());
    desc->setJustificationType(juce::Justification::topLeft);
    desc->setBounds(24, 10, 600, 50);
    page->addAndMakeVisible(desc);

    m_foldersModel.folders = &m_musicFolders;
    m_foldersList = std::make_unique<juce::ListBox>("folders", &m_foldersModel);
    m_foldersList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_foldersList->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_foldersList->setRowHeight(26);
    m_foldersList->setBounds(24, 70, 520, 240);
    page->addAndMakeVisible(*m_foldersList);

    m_addFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.addFolder"));
    m_addFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_addFolderBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_addFolderBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_addFolderBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_addFolderBtn->setBounds(560, 70, 150, 28);
    m_addFolderBtn->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(BM_TJ("dialog.setup.chooseFolder"),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    m_musicFolders.addIfNotAlreadyThere(result.getFullPathName());
                    m_foldersList->updateContent();
                }
            });
    };
    page->addAndMakeVisible(*m_addFolderBtn);

    m_removeFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.removeFolder"));
    m_removeFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::error());
    m_removeFolderBtn->setColour(juce::TextButton::buttonOnColourId, Colors::error().brighter(0.2f));
    m_removeFolderBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_removeFolderBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_removeFolderBtn->setBounds(560, 104, 150, 28);
    m_removeFolderBtn->onClick = [this] {
        int sel = m_foldersList->getSelectedRow();
        if (sel >= 0 && sel < m_musicFolders.size()) {
            m_musicFolders.remove(sel);
            m_foldersList->updateContent();
        }
    };
    page->addAndMakeVisible(*m_removeFolderBtn);

    m_addDefaultBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.addMusic"));
    m_addDefaultBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_addDefaultBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_addDefaultBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_addDefaultBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_addDefaultBtn->setBounds(560, 138, 150, 28);
    m_addDefaultBtn->onClick = [this] {
        auto musicDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
        if (musicDir.exists()) {
            m_musicFolders.addIfNotAlreadyThere(musicDir.getFullPathName());
            m_foldersList->updateContent();
        }
    };
    page->addAndMakeVisible(*m_addDefaultBtn);

    auto* infoLbl = new juce::Label("", BM_TJ("dialog.setup.formatsInfo"));
    infoLbl->setFont(juce::Font(12.0f));
    infoLbl->setColour(juce::Label::textColourId, Colors::primary());
    infoLbl->setBounds(24, 320, 500, 20);
    page->addAndMakeVisible(infoLbl);

    return page;
}

juce::Component* FirstTimeSetupDialog::createDJSoftwarePage()
{
    auto* page = new juce::Component();

    auto* desc = new juce::Label("", BM_TJ("dialog.setup.djDesc"));
    desc->setFont(juce::Font(14.0f));
    desc->setColour(juce::Label::textColourId, Colors::textSecondary());
    desc->setJustificationType(juce::Justification::topLeft);
    desc->setBounds(24, 10, 600, 50);
    page->addAndMakeVisible(desc);

    auto* headerName = new juce::Label("", BM_TJ("dialog.setup.software"));
    headerName->setFont(juce::Font(13.0f, juce::Font::bold));
    headerName->setColour(juce::Label::textColourId, Colors::textPrimary());
    headerName->setBounds(24, 70, 300, 22);
    page->addAndMakeVisible(headerName);

    auto* headerStatus = new juce::Label("", BM_TJ("dialog.setup.status"));
    headerStatus->setFont(juce::Font(13.0f, juce::Font::bold));
    headerStatus->setColour(juce::Label::textColourId, Colors::textPrimary());
    headerStatus->setBounds(400, 70, 200, 22);
    page->addAndMakeVisible(headerStatus);

    int y = 98;
    juce::String names[] = { "Rekordbox", "Serato DJ", "Traktor Pro", "VirtualDJ", "Engine DJ", "djay Pro" };

    m_djSoftwareEntries.clear();
    for (auto& name : names)
    {
        DJSoftwareEntry entry;
        entry.name = name;
        entry.detected = false;

        entry.nameLbl = std::make_unique<juce::Label>("", name);
        entry.nameLbl->setFont(juce::Font(14.0f));
        entry.nameLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
        entry.nameLbl->setBounds(24, y, 350, 26);
        page->addAndMakeVisible(*entry.nameLbl);

        entry.statusLbl = std::make_unique<juce::Label>("", BM_TJ("dialog.setup.notScanned"));
        entry.statusLbl->setFont(juce::Font(12.0f, juce::Font::bold));
        entry.statusLbl->setColour(juce::Label::textColourId, Colors::textMuted());
        entry.statusLbl->setBounds(400, y, 200, 26);
        page->addAndMakeVisible(*entry.statusLbl);

        m_djSoftwareEntries.push_back(std::move(entry));
        y += 32;
    }

    y += 16;

    m_scanBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.scanBtn"));
    m_scanBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_scanBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_scanBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_scanBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_scanBtn->setBounds(24, y, 220, 32);
    m_scanBtn->onClick = [this] { scanDJSoftware(); };
    page->addAndMakeVisible(*m_scanBtn);

    return page;
}

juce::Component* FirstTimeSetupDialog::createAudioPage()
{
    auto* page = new juce::Component();

    auto* desc = new juce::Label("", BM_TJ("dialog.setup.audioDesc"));
    desc->setFont(juce::Font(14.0f));
    desc->setColour(juce::Label::textColourId, Colors::textSecondary());
    desc->setJustificationType(juce::Justification::topLeft);
    desc->setBounds(24, 10, 600, 50);
    page->addAndMakeVisible(desc);

    int y = 80;

    auto* devLabel = new juce::Label("", BM_TJ("dialog.setup.outputDevice"));
    devLabel->setFont(juce::Font(14.0f));
    devLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    devLabel->setBounds(24, y, 220, 26);
    page->addAndMakeVisible(devLabel);

    m_audioDeviceCombo = std::make_unique<juce::ComboBox>();
    m_audioDeviceCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_audioDeviceCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_audioDeviceCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());

    {
        juce::AudioDeviceManager tempManager;
        tempManager.initialiseWithDefaultDevices(0, 2);
        auto* currentType = tempManager.getCurrentDeviceTypeObject();
        if (currentType != nullptr)
        {
            auto deviceNames = currentType->getDeviceNames();
            for (int i = 0; i < deviceNames.size(); ++i)
                m_audioDeviceCombo->addItem(deviceNames[i], i + 1);
        }
    }
    if (m_audioDeviceCombo->getNumItems() == 0)
        m_audioDeviceCombo->addItem(BM_TJ("dialog.setup.defaultDevice"), 1);
    m_audioDeviceCombo->setSelectedId(1);
    m_audioDeviceCombo->setBounds(260, y, 350, 26);
    page->addAndMakeVisible(*m_audioDeviceCombo);
    y += 40;

    auto* srLabel = new juce::Label("", BM_TJ("dialog.setup.sampleRate"));
    srLabel->setFont(juce::Font(14.0f));
    srLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    srLabel->setBounds(24, y, 220, 26);
    page->addAndMakeVisible(srLabel);

    m_sampleRateCombo = std::make_unique<juce::ComboBox>();
    m_sampleRateCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_sampleRateCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_sampleRateCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_sampleRateCombo->addItem("44100 Hz", 1);
    m_sampleRateCombo->addItem("48000 Hz", 2);
    m_sampleRateCombo->addItem("96000 Hz", 3);
    m_sampleRateCombo->setSelectedId(1);
    m_sampleRateCombo->setBounds(260, y, 200, 26);
    page->addAndMakeVisible(*m_sampleRateCombo);
    y += 40;

    auto* bufLabel = new juce::Label("", BM_TJ("dialog.setup.bufferSize"));
    bufLabel->setFont(juce::Font(14.0f));
    bufLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    bufLabel->setBounds(24, y, 220, 26);
    page->addAndMakeVisible(bufLabel);

    m_bufferSizeCombo = std::make_unique<juce::ComboBox>();
    m_bufferSizeCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_bufferSizeCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_bufferSizeCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_bufferSizeCombo->addItem("128 samples", 1);
    m_bufferSizeCombo->addItem("256 samples", 2);
    m_bufferSizeCombo->addItem("512 samples", 3);
    m_bufferSizeCombo->addItem("1024 samples", 4);
    m_bufferSizeCombo->setSelectedId(3);
    m_bufferSizeCombo->setBounds(260, y, 200, 26);
    page->addAndMakeVisible(*m_bufferSizeCombo);

    m_bufferSizeCombo->onChange = [this] {
        int bufferSizes[] = { 128, 256, 512, 1024 };
        int sampleRates[] = { 44100, 48000, 96000 };
        int bufIdx = m_bufferSizeCombo->getSelectedId() - 1;
        int srIdx  = m_sampleRateCombo->getSelectedId() - 1;
        if (bufIdx >= 0 && bufIdx < 4 && srIdx >= 0 && srIdx < 3 && m_latencyLabel) {
            double latencyMs = (double)bufferSizes[bufIdx] / (double)sampleRates[srIdx] * 1000.0;
            m_latencyLabel->setText(BM_TJ("dialog.setup.latencyPrefix") + juce::String(latencyMs, 1) + " ms",
                                     juce::dontSendNotification);
        }
    };
    m_sampleRateCombo->onChange = [this] {
        if (m_bufferSizeCombo && m_bufferSizeCombo->onChange)
            m_bufferSizeCombo->onChange();
    };
    y += 40;

    m_latencyLabel = std::make_unique<juce::Label>("", BM_TJ("dialog.setup.latencyDefault"));
    m_latencyLabel->setFont(juce::Font(14.0f, juce::Font::bold));
    m_latencyLabel->setColour(juce::Label::textColourId, Colors::primary());
    m_latencyLabel->setBounds(24, y, 400, 26);
    page->addAndMakeVisible(*m_latencyLabel);

    return page;
}

juce::Component* FirstTimeSetupDialog::createLicencePage()
{
    auto* page = new juce::Component();

    auto* desc = new juce::Label("", BM_TJ("dialog.setup.licDesc"));
    desc->setFont(juce::Font(14.0f));
    desc->setColour(juce::Label::textColourId, Colors::textSecondary());
    desc->setJustificationType(juce::Justification::topLeft);
    desc->setBounds(24, 10, 600, 50);
    page->addAndMakeVisible(desc);

    int y = 80;

    auto* keyLabel = new juce::Label("", BM_TJ("dialog.setup.licKey"));
    keyLabel->setFont(juce::Font(14.0f));
    keyLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    keyLabel->setBounds(24, y, 160, 26);
    page->addAndMakeVisible(keyLabel);

    m_licenseKeyEdit = std::make_unique<juce::TextEditor>();
    m_licenseKeyEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_licenseKeyEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_licenseKeyEdit->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_licenseKeyEdit->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    m_licenseKeyEdit->setTextToShowWhenEmpty("XXXXX-XXXXX-XXXXX-XXXXX-XXXXX", juce::Colours::grey);
    m_licenseKeyEdit->setBounds(190, y, 360, 28);
    page->addAndMakeVisible(*m_licenseKeyEdit);
    y += 40;

    m_activateLicBtn = std::make_unique<juce::TextButton>(BM_TJ("dialog.setup.activate"));
    m_activateLicBtn->setColour(juce::TextButton::buttonColourId, Colors::primary());
    m_activateLicBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryHover());
    m_activateLicBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_activateLicBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_activateLicBtn->setBounds(190, y, 120, 30);
    m_activateLicBtn->onClick = [this] {
        auto key = m_licenseKeyEdit->getText().trim();
        if (key.length() >= 20) {
            m_licStatusLabel->setText(BM_TJ("dialog.setup.licActive"), juce::dontSendNotification);
            m_licStatusLabel->setColour(juce::Label::textColourId, Colors::success());
            m_licenseKeyEdit->setReadOnly(true);
        } else if (key.isNotEmpty()) {
            m_licStatusLabel->setText(BM_TJ("dialog.setup.licInvalid"), juce::dontSendNotification);
            m_licStatusLabel->setColour(juce::Label::textColourId, Colors::error());
        }
    };
    page->addAndMakeVisible(*m_activateLicBtn);
    y += 44;

    m_licStatusLabel = std::make_unique<juce::Label>("", BM_TJ("dialog.setup.trialMode"));
    m_licStatusLabel->setFont(juce::Font(13.0f));
    m_licStatusLabel->setColour(juce::Label::textColourId, Colors::warning());
    m_licStatusLabel->setBounds(24, y, 500, 22);
    page->addAndMakeVisible(*m_licStatusLabel);
    y += 36;

    juce::String machineId = juce::SystemStats::getUniqueDeviceID();
    if (machineId.isEmpty())
        machineId = juce::String::toHexString(juce::SystemStats::getOperatingSystemName().hashCode64());
    auto* machineIdLbl = new juce::Label("", BM_TJ("dialog.setup.machineIdPrefix") + machineId);
    machineIdLbl->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    machineIdLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    machineIdLbl->setBounds(24, y, 500, 22);
    page->addAndMakeVisible(machineIdLbl);
    y += 36;

    auto* skipLbl = new juce::Label("", BM_TJ("dialog.setup.skipInfo"));
    skipLbl->setFont(juce::Font(12.0f));
    skipLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    skipLbl->setJustificationType(juce::Justification::topLeft);
    skipLbl->setBounds(24, y, 500, 40);
    page->addAndMakeVisible(skipLbl);

    auto* buyBtn = new juce::HyperlinkButton(BM_TJ("dialog.setup.buyLink"),
        juce::URL("https://beatmate.fr/tarifs/"));
    buyBtn->setFont(juce::Font(13.0f, juce::Font::underlined), false);
    buyBtn->setColour(juce::HyperlinkButton::textColourId, Colors::primary());
    buyBtn->setBounds(24, y + 46, 300, 22);
    page->addAndMakeVisible(buyBtn);

    return page;
}

juce::Component* FirstTimeSetupDialog::createSummaryPage()
{
    auto* page = new juce::Component();

    auto* summaryTitle = new juce::Label("", BM_TJ("dialog.setup.summaryTitle"));
    summaryTitle->setFont(juce::Font(16.0f, juce::Font::bold));
    summaryTitle->setColour(juce::Label::textColourId, Colors::textPrimary());
    summaryTitle->setBounds(24, 10, 500, 24);
    page->addAndMakeVisible(summaryTitle);

    m_summaryText = std::make_unique<juce::TextEditor>();
    m_summaryText->setMultiLine(true, true);
    m_summaryText->setReadOnly(true);
    m_summaryText->setScrollbarsShown(true);
    m_summaryText->setCaretVisible(false);
    m_summaryText->setColour(juce::TextEditor::backgroundColourId, Colors::bgDark());
    m_summaryText->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_summaryText->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_summaryText->setFont(juce::Font(13.0f));
    m_summaryText->setBounds(24, 44, 650, 300);
    page->addAndMakeVisible(*m_summaryText);

    auto* readyLbl = new juce::Label("", BM_TJ("dialog.setup.readyLbl"));
    readyLbl->setFont(juce::Font(14.0f, juce::Font::bold));
    readyLbl->setColour(juce::Label::textColourId, Colors::primary());
    readyLbl->setBounds(24, 360, 600, 24);
    page->addAndMakeVisible(readyLbl);

    return page;
}

void FirstTimeSetupDialog::createPages()
{
    m_pages.add(createWelcomePage());
    m_pages.add(createMusicFoldersPage());
    m_pages.add(createDJSoftwarePage());
    m_pages.add(createAudioPage());
    m_pages.add(createLicencePage());
    m_pages.add(createSummaryPage());

    for (auto* p : m_pages)
        addChildComponent(p);
}

void FirstTimeSetupDialog::showPage(int index)
{
    for (int i = 0; i < m_pages.size(); ++i)
        m_pages[i]->setVisible(i == index);

    m_currentPage = index;

    juce::StringArray titles = {
        BM_TJ("dialog.setup.stepWelcome"),
        BM_TJ("dialog.setup.stepFolders"),
        BM_TJ("dialog.setup.stepDj"),
        BM_TJ("dialog.setup.stepAudio"),
        BM_TJ("dialog.setup.stepLic"),
        BM_TJ("dialog.setup.stepSummary")
    };

    m_subtitleLabel->setText(BM_TJ("dialog.setup.stepPrefix") + juce::String(index + 1) + " / 6 : " + titles[index],
                              juce::dontSendNotification);
    m_prevBtn->setEnabled(index > 0);

    if (index == (int)m_pages.size() - 1)
        m_nextBtn->setButtonText(BM_TJ("dialog.setup.start"));
    else
        m_nextBtn->setButtonText(BM_TJ("dialog.setup.next"));

    resized();
}

void FirstTimeSetupDialog::scanDJSoftware()
{
    for (auto& entry : m_djSoftwareEntries) {
        entry.statusLbl->setText(BM_TJ("dialog.setup.scanning"), juce::dontSendNotification);
        entry.statusLbl->setColour(juce::Label::textColourId, Colors::warning());
    }

    juce::Timer::callAfterDelay(400, [this] {
        struct Check { juce::String path; };
        std::vector<Check> checks = {
            { juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("Pioneer/rekordbox").getFullPathName() },
            { juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("_Serato_").getFullPathName() },
            { juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Native Instruments/Traktor").getFullPathName() },
            { juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("VirtualDJ").getFullPathName() },
            { juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("Engine Library").getFullPathName() },
            { juce::String() } // djay Pro - no standard path
        };

        for (size_t i = 0; i < m_djSoftwareEntries.size() && i < checks.size(); ++i) {
            bool found = checks[i].path.isNotEmpty() && juce::File(checks[i].path).exists();
            m_djSoftwareEntries[i].detected = found;

            if (found) {
                m_djSoftwareEntries[i].statusLbl->setText(BM_TJ("dialog.setup.detected"), juce::dontSendNotification);
                m_djSoftwareEntries[i].statusLbl->setColour(juce::Label::textColourId, Colors::success());
            } else {
                m_djSoftwareEntries[i].statusLbl->setText(BM_TJ("dialog.setup.notDetected"), juce::dontSendNotification);
                m_djSoftwareEntries[i].statusLbl->setColour(juce::Label::textColourId, Colors::textMuted());
            }
        }
    });
}

void FirstTimeSetupDialog::updateSummary()
{
    if (!m_summaryText) return;

    juce::String text;

    text << "=== CONFIGURATION BEATMATE V11 ===\n\n";

    text << "LANGUE\n";
    text << "  " << (m_languageCombo ? m_languageCombo->getText() : "Francais") << "\n\n";

    text << "DOSSIERS DE MUSIQUE (" << juce::String(m_musicFolders.size()) << ")\n";
    if (m_musicFolders.isEmpty())
        text << "  (aucun dossier configure)\n";
    else
        for (const auto& f : m_musicFolders)
            text << "  " << f << "\n";
    text << "\n";

    text << "LOGICIELS DJ\n";
    bool anyDetected = false;
    for (const auto& entry : m_djSoftwareEntries)
    {
        if (entry.detected)
        {
            text << "  " << entry.name << " : Detecte\n";
            anyDetected = true;
        }
    }
    if (!anyDetected)
        text << "  (aucun logiciel DJ detecte)\n";
    text << "\n";

    text << "AUDIO\n";
    text << "  Peripherique : " << (m_audioDeviceCombo ? m_audioDeviceCombo->getText() : "Par defaut") << "\n";
    text << "  Sample rate  : " << (m_sampleRateCombo ? m_sampleRateCombo->getText() : "44100 Hz") << "\n";
    text << "  Buffer       : " << (m_bufferSizeCombo ? m_bufferSizeCombo->getText() : "512 samples") << "\n";
    if (m_latencyLabel)
        text << "  " << m_latencyLabel->getText() << "\n";
    text << "\n";

    text << "LICENCE\n";
    auto licKey = m_licenseKeyEdit ? m_licenseKeyEdit->getText().trim() : juce::String();
    if (licKey.length() >= 20)
        text << "  Cle : " << licKey.substring(0, 5) << "-****-****-****-" << licKey.substring(licKey.length() - 5) << "\n";
    else
        text << "  Mode essai (30 jours)\n";

    m_summaryText->setText(text, false);
}

void FirstTimeSetupDialog::saveWizardSettings()
{
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator && m_languageCombo) {
            if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>()) {
                const std::string code = (m_languageCombo->getSelectedId() == 2) ? "en" : "fr";
                svc->setLanguage(code);
            }
        }
    }

    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate");
    appData.createDirectory();
    auto settingsFile = appData.getChildFile("appsettings.json");

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    if (settingsFile.existsAsFile())
    {
        auto existing = juce::JSON::parse(settingsFile.loadFileAsString());
        if (auto* obj = existing.getDynamicObject())
            root = obj;
    }

    {
        juce::DynamicObject::Ptr general = new juce::DynamicObject();
        general->setProperty("language", m_languageCombo ? m_languageCombo->getSelectedId() : 1);
        general->setProperty("theme", 1); // Dark by default
        general->setProperty("autoSave", true);
        general->setProperty("autoSaveInterval", 2);
        general->setProperty("checkUpdates", true);
        general->setProperty("dataFolder", appData.getFullPathName());
        root->setProperty("general", juce::var(general.get()));
    }

    {
        juce::DynamicObject::Ptr audio = new juce::DynamicObject();
        audio->setProperty("deviceId", m_audioDeviceCombo ? m_audioDeviceCombo->getSelectedId() : 1);
        audio->setProperty("deviceName", m_audioDeviceCombo ? m_audioDeviceCombo->getText() : juce::String());
        audio->setProperty("sampleRate", m_sampleRateCombo ? m_sampleRateCombo->getSelectedId() : 1);
        audio->setProperty("bufferSize", m_bufferSizeCombo ? m_bufferSizeCombo->getSelectedId() : 3);
        root->setProperty("audio", juce::var(audio.get()));
    }

    {
        juce::DynamicObject::Ptr library = new juce::DynamicObject();
        juce::Array<juce::var> foldersArr;
        for (const auto& f : m_musicFolders)
            foldersArr.add(juce::var(f));
        library->setProperty("watchFolders", juce::var(foldersArr));
        library->setProperty("autoImport", true);
        library->setProperty("analyzeOnImport", true);
        library->setProperty("detectDuplicates", true);
        root->setProperty("library", juce::var(library.get()));
    }

    {
        juce::DynamicObject::Ptr licence = new juce::DynamicObject();
        auto key = m_licenseKeyEdit ? m_licenseKeyEdit->getText().trim() : juce::String();
        licence->setProperty("key", key);
        if (key.length() >= 20)
        {
            licence->setProperty("status", "Active");
            licence->setProperty("type", "Type : Professional");
        }
        else
        {
            licence->setProperty("status", "Essai");
            licence->setProperty("type", "Type : Essai gratuit (Trial)");
        }
        root->setProperty("licence", juce::var(licence.get()));
    }

    root->setProperty("firstTimeSetupDone", true);

    juce::String jsonText = juce::JSON::toString(juce::var(root.get()), true);
    settingsFile.replaceWithText(jsonText);
}

juce::String FirstTimeSetupDialog::selectedLanguage() const
{
    return m_languageCombo ? m_languageCombo->getText() : "Francais";
}

juce::StringArray FirstTimeSetupDialog::musicFolders() const
{
    return m_musicFolders;
}

juce::StringArray FirstTimeSetupDialog::detectedDJSoftware() const
{
    juce::StringArray result;
    for (const auto& entry : m_djSoftwareEntries)
        if (entry.detected)
            result.add(entry.name);
    return result;
}

bool FirstTimeSetupDialog::showDialog(juce::Component* /*parent*/)
{
    auto* content = new FirstTimeSetupDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle = BM_TJ("dialog.setup.title");
    opts.dialogBackgroundColour = Colors::bgDarker();
    opts.resizable = false;
    opts.useNativeTitleBar = true;
    return opts.runModal() == 1;
}

void FirstTimeSetupDialog::paint(juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();

    {
        juce::ColourGradient bg(juce::Colour(0xFF161432), 0.f, 0.f,
                                 juce::Colour(0xFF07070E), 0.f, H, false);
        bg.addColour(0.5, juce::Colour(0xFF0C0C18));
        g.setGradientFill(bg);
        g.fillRect(getLocalBounds());
    }

    auto fillHalo = [&](float cx, float cy, float r, juce::Colour c) {
        juce::ColourGradient h(c, cx, cy,
                               juce::Colours::transparentBlack, cx + r, cy, true);
        g.setGradientFill(h);
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
    };
    fillHalo(W * 0.1f,  H * 0.15f, 260.f, juce::Colour(0x553B82F6));
    fillHalo(W * 0.9f,  H * 0.85f, 260.f, juce::Colour(0x558B5CF6));
    fillHalo(W * 0.5f,  -40.f,      420.f, juce::Colour(0x306366F1));

    g.setColour(juce::Colour(0x206366F1));
    for (int y = 24; y < getHeight(); y += 28)
        for (int x = 24; x < getWidth(); x += 28)
            g.fillRect(x, y, 1, 1);

    const float headerH = 120.f;
    {
        juce::ColourGradient h(juce::Colour(0x33FFFFFF), 0.f, 0.f,
                                juce::Colour(0x00000000), 0.f, headerH, false);
        g.setGradientFill(h);
        g.fillRect(0.f, 0.f, W, headerH);
    }

    g.setColour(juce::Colour(0xFF6366F1));
    g.fillRect(24.f, 20.f, 4.f, 36.f);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(24.f).withStyle("Bold")));
    g.setColour(juce::Colour(0xFFF8FAFC));
    g.drawText(juce::String::fromUTF8("BeatMate V12"),
               juce::Rectangle<int>(40, 18, (int)W - 80, 30),
               juce::Justification::centredLeft);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.f)));
    g.setColour(juce::Colour(0xFF93A3FF));
    g.drawText(juce::String::fromUTF8("Configuration initiale"),
               juce::Rectangle<int>(40, 46, (int)W - 80, 20),
               juce::Justification::centredLeft);

    {
        juce::Rectangle<float> badge((float)getWidth() - 150.f, 22.f, 120.f, 26.f);
        g.setColour(juce::Colour(0xFF3B82F6).withAlpha(0.18f));
        g.fillRoundedRectangle(badge, 13.f);
        g.setColour(juce::Colour(0xFF93A3FF));
        g.drawRoundedRectangle(badge, 13.f, 1.f);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle("Bold")));
        g.setColour(juce::Colour(0xFFDBEAFE));
        g.drawText("PROFESSIONNEL", badge, juce::Justification::centred);
    }

    const char* const stepLabels[6] = {
        "Bienvenue", "Musique", "Logiciels DJ", "Audio", "Licence", juce::String::fromUTF8("Resume").toRawUTF8()
    };
    const int totalSteps = 6;
    const float stepCy = 90.f;
    const float lineY  = stepCy;
    const float availW = W - 120.f;
    const float stepDx = availW / (float)(totalSteps - 1);

    for (int i = 0; i < totalSteps; ++i) {
        const float cx = 60.f + i * stepDx;
        const bool done    = i < m_currentPage;
        const bool current = i == m_currentPage;

        if (i < totalSteps - 1) {
            float x1 = cx + 14.f;
            float x2 = cx + stepDx - 14.f;
            if (done) g.setColour(juce::Colour(0xFF22C55E).withAlpha(0.65f));
            else      g.setColour(juce::Colour(0xFF1E293B));
            g.fillRect(x1, lineY - 1.f, x2 - x1, 2.f);
        }

        if (current) {
            for (int k = 3; k >= 1; --k) {
                float r = 14.f + k * 5.f;
                g.setColour(juce::Colour(0xFF6366F1).withAlpha(0.22f - k * 0.05f));
                g.fillEllipse(cx - r, stepCy - r, r * 2, r * 2);
            }
        }

        juce::Colour fill = done   ? juce::Colour(0xFF22C55E)
                          : current ? juce::Colour(0xFF3B82F6)
                                    : juce::Colour(0xFF1E293B);
        g.setColour(fill);
        g.fillEllipse(cx - 14.f, stepCy - 14.f, 28.f, 28.f);

        g.setColour(current ? juce::Colour(0xFFA5B4FC)
                            : (done ? juce::Colour(0xFF86EFAC) : juce::Colour(0xFF334155)));
        g.drawEllipse(cx - 14.f, stepCy - 14.f, 28.f, 28.f, 1.5f);

        g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.f).withStyle("Bold")));
        g.setColour(done ? juce::Colours::white
                         : (current ? juce::Colours::white : juce::Colour(0xFF64748B)));
        juce::Rectangle<float> numRect(cx - 14.f, stepCy - 14.f, 28.f, 28.f);
        if (done) g.drawText(juce::String::fromUTF8("\xE2\x9C\x93"), numRect, juce::Justification::centred);
        else      g.drawText(juce::String(i + 1), numRect, juce::Justification::centred);

        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.f).withStyle(current ? "Bold" : "Regular")));
        g.setColour(current ? juce::Colour(0xFFF1F5F9)
                            : (done ? juce::Colour(0xFFCBD5E1) : juce::Colour(0xFF64748B)));
        juce::Rectangle<float> labRect(cx - 50.f, stepCy + 18.f, 100.f, 16.f);
        g.drawText(stepLabels[i], labRect, juce::Justification::centred);
    }

    auto content = juce::Rectangle<float>(16.f, 138.f, W - 32.f, H - 138.f - 74.f);
    {
        for (int s = 5; s >= 1; --s) {
            g.setColour(juce::Colour(0x00000000).withAlpha((float)s * 0.015f));
            g.fillRoundedRectangle(content.translated((float)s, (float)s), 10.f);
        }
        g.setColour(juce::Colour(0xCC0B0B17));
        g.fillRoundedRectangle(content, 10.f);
        g.setColour(juce::Colour(0xFF1E293B));
        g.drawRoundedRectangle(content, 10.f, 1.f);
    }

    const float footerY = H - 64.f;
    {
        juce::ColourGradient fg(juce::Colour(0x1AFFFFFF), 0.f, footerY,
                                 juce::Colour(0x00000000), 0.f, H, false);
        g.setGradientFill(fg);
        g.fillRect(0.f, footerY, W, 64.f);
    }
    g.setColour(juce::Colour(0xFF1E293B));
    g.fillRect(0.f, footerY, W, 1.f);

    g.setColour(juce::Colour(0xFF6366F1));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.f, 1.f);

}

void FirstTimeSetupDialog::resized()
{
    // Titre/sous-titre rendus dans paint() : labels parqués hors écran.
    if (m_titleLabel)    m_titleLabel->setBounds(-2000, -2000, 10, 10);
    if (m_subtitleLabel) m_subtitleLabel->setBounds(-2000, -2000, 10, 10);

    const int pageX = 32;
    const int pageY = 150;
    const int pageW = getWidth() - pageX * 2;
    const int pageH = getHeight() - pageY - 86;
    for (auto* p : m_pages)
        p->setBounds(pageX, pageY, pageW, pageH);

    const int btnW = 140, btnH = 40;
    m_prevBtn->setBounds(getWidth() - 2 * (btnW + 16), getHeight() - btnH - 18, btnW, btnH);
    m_nextBtn->setBounds(getWidth() - (btnW + 32),     getHeight() - btnH - 18, btnW, btnH);
}

} // namespace BeatMate::UI
