#include "ImportView.h"
#include "import/FileImportPanel.h"
#include "import/DJImportPanel.h"
#include "../styles/ColorPalette.h"
#include "../utils/ViewPrefs.h"
#include "../../services/config/I18n.h"
#include <spdlog/spdlog.h>

namespace BeatMate::UI {

ImportView::ImportView() : m_provider(nullptr)
{
    setupUI();
    retranslateUi();
}

ImportView::ImportView(Services::Library::TrackDataProvider* provider) : m_provider(provider)
{
    setupUI();
    retranslateUi();
}

ImportView::~ImportView() = default;

void ImportView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("import.title"));
    m_titleLabel->setFont(Type::display());
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeTab = [this](const juce::String& t) {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
        b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(*b);
        return b;
    };
    m_filesTabBtn = makeTab(BM_TJ("import.tab.files"));
    m_filesTabBtn->onClick = [this] { switchTab(false); };
    m_djTabBtn = makeTab(BM_TJ("import.tab.djSoftware"));
    m_djTabBtn->onClick = [this] { switchTab(true); };

    m_filePanel = std::make_unique<FileImportPanel>();
    m_filePanel->onImportRequested = [this](const std::vector<Services::Library::StagedImportEntry>& entries,
                                            const Services::Library::FileImportOptions& options) {
        m_listeners.call([&entries, &options](Listener& l) { l.importRequested(entries, options); });
    };
    m_filePanel->onCancelRequested = [this] {
        m_listeners.call([](Listener& l) { l.importCancelRequested(); });
    };
    m_filePanel->onAnalyzeImported = [this] {
        m_listeners.call([](Listener& l) { l.analyzeImportedRequested(); });
    };
    m_filePanel->onNavigateToLibrary = [this] {
        if (onNavigateToLibrary) onNavigateToLibrary();
    };
    addAndMakeVisible(*m_filePanel);

    m_djPanel = std::make_unique<DJImportPanel>();
    m_djPanel->onNavigateToLibrary = [this] {
        if (onNavigateToLibrary) onNavigateToLibrary();
    };
    addChildComponent(*m_djPanel);

    m_showDjTab = Prefs::getInt("import.activeTab", 0) == 1;
    switchTab(m_showDjTab);
}

void ImportView::switchTab(bool showDj)
{
    m_showDjTab = showDj;
    Prefs::setInt("import.activeTab", showDj ? 1 : 0);

    m_filesTabBtn->setColour(juce::TextButton::buttonColourId,
                             showDj ? Colors::bgLighter() : Colors::primary().withAlpha(0.4f));
    m_filesTabBtn->setColour(juce::TextButton::textColourOffId,
                             showDj ? Colors::textSecondary() : Colors::textPrimary());
    m_djTabBtn->setColour(juce::TextButton::buttonColourId,
                          showDj ? Colors::primary().withAlpha(0.4f) : Colors::bgLighter());
    m_djTabBtn->setColour(juce::TextButton::textColourOffId,
                          showDj ? Colors::textPrimary() : Colors::textSecondary());

    m_filePanel->setVisible(!showDj);
    m_djPanel->setVisible(showDj);
    if (showDj)
        m_djPanel->refreshSources();
    repaint();
}

void ImportView::filesDropped(const juce::StringArray& files, int, int)
{
    if (m_showDjTab)
        switchTab(false);
    m_filePanel->addPaths(files);
}

void ImportView::onImportProgress(int current, int total, const juce::String& fileName)
{
    m_filePanel->setImportProgress(current, total, fileName);
}

void ImportView::onImportFinished(const Services::Library::FileImportReport& report)
{
    m_filePanel->showImportReport(report);
}

void ImportView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    const int margin = 24;
    g.setColour(Colors::primary());
    g.fillRoundedRectangle(static_cast<float>(margin - 12), 24.0f, 3.0f, 24.0f, 1.5f);

    const int tabY = 62;
    const int tabW = 170;
    g.setColour(Colors::primary());
    if (!m_showDjTab)
        g.fillRoundedRectangle(static_cast<float>(margin), static_cast<float>(tabY + 28),
                               static_cast<float>(tabW), 2.0f, 1.0f);
    else
        g.fillRoundedRectangle(static_cast<float>(margin + tabW + 8), static_cast<float>(tabY + 28),
                               static_cast<float>(tabW), 2.0f, 1.0f);

    ProDraw::vignette(g, static_cast<float>(getWidth()), static_cast<float>(getHeight()));
}

void ImportView::resized()
{
    const int margin = 24;
    m_titleLabel->setBounds(margin, 20, 500, 32);

    const int tabY = 62;
    const int tabW = 170;
    m_filesTabBtn->setBounds(margin, tabY, tabW, 26);
    m_djTabBtn->setBounds(margin + tabW + 8, tabY, tabW, 26);

    const int contentY = tabY + 38;
    auto content = juce::Rectangle<int>(margin, contentY,
                                        getWidth() - margin * 2, getHeight() - contentY - 12);
    m_filePanel->setBounds(content);
    m_djPanel->setBounds(content);
}

void ImportView::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("import.title"), juce::dontSendNotification);
    if (m_filesTabBtn)
        m_filesTabBtn->setButtonText(BM_TJ("import.tab.files"));
    if (m_djTabBtn)
        m_djTabBtn->setButtonText(BM_TJ("import.tab.djSoftware"));
    if (m_filePanel)
        m_filePanel->retranslateUi();
    if (m_djPanel)
        m_djPanel->retranslateUi();
    repaint();
}

} // namespace BeatMate::UI
