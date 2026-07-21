#include "FileImportPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../widgets/ToastNotifier.h"
#include "../../utils/ViewPrefs.h"
#include "../../../services/config/I18n.h"
#include "../../../services/library/TrackDatabase.h"
#include "../../../services/library/TrackMetadata.h"
#include "../../../services/library/DuplicateDetector.h"
#include "../../../app/ServiceLocator.h"
#include <spdlog/spdlog.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI {

namespace {
const juce::StringArray kAudioExtensions = {
    "*.mp3", "*.wav", "*.flac", "*.aac", "*.ogg",
    "*.m4a", "*.aiff", "*.aif", "*.wma", "*.opus"
};

bool isAudioFile(const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    for (auto& e : kAudioExtensions)
        if (ext == e.substring(1)) return true;
    return false;
}

juce::String audioFilter() { return kAudioExtensions.joinIntoString(";"); }

constexpr int kDropZoneH = 92;
constexpr int kOptionsH = 26;
constexpr int kFolderRowH = 24;
constexpr int kInspectorW = 300;
constexpr int kFooterH = 76;
}

FileImportPanel::FileImportPanel()
{
    m_list = std::make_unique<ImportStagingList>(m_entries);
    m_list->onToggleRow = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_entries.size())) return;
        m_entries[static_cast<size_t>(row)].selected = !m_entries[static_cast<size_t>(row)].selected;
        m_list->refreshRow(row);
        updateCountLabel();
    };
    m_list->onRemoveRow = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_entries.size())) return;
        m_entries.erase(m_entries.begin() + row);
        m_list->refresh();
        m_inspector->clearEntry();
        updateCountLabel();
    };
    m_list->onRowSelected = [this](int row) {
        if (row < 0 || row >= static_cast<int>(m_entries.size()))
        {
            m_inspector->clearEntry();
            return;
        }
        m_inspector->setEntry(&m_entries[static_cast<size_t>(row)], row);
    };
    addAndMakeVisible(*m_list);

    m_inspector = std::make_unique<ImportInspectorPanel>();
    m_inspector->onFieldEdited = [this](int index, int field, const juce::String& value) {
        if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
        auto& e = m_entries[static_cast<size_t>(index)];
        switch (field)
        {
            case 0: e.titleOverride = value; break;
            case 1: e.artistOverride = value; break;
            case 2: e.albumOverride = value; break;
            case 3: e.genreOverride = value; break;
            default: break;
        }
        m_list->refreshRow(index);
    };
    m_inspector->clearEntry();
    addAndMakeVisible(*m_inspector);

    m_summary = std::make_unique<ImportSummaryCard>();
    m_summary->onAnalyzeNow = [this] { if (onAnalyzeImported) onAnalyzeImported(); };
    m_summary->onViewLibrary = [this] { if (onNavigateToLibrary) onNavigateToLibrary(); };
    addChildComponent(*m_summary);

    auto makeBtn = [this](const juce::String& t, juce::Colour bg) {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
        return b;
    };

    m_browseFilesBtn = makeBtn(BM_TJ("import.btn.browseFiles"), Colors::primary().withAlpha(0.4f));
    m_browseFilesBtn->onClick = [this] { browseFiles(); };
    m_browseFolderBtn = makeBtn(BM_TJ("import.btn.browseFolder"), Colors::bgLighter());
    m_browseFolderBtn->onClick = [this] { browseFolder(); };

    auto makeOpt = [this](const char* key, const char* tipKey) {
        auto b = std::make_unique<juce::ToggleButton>(BM_TJ(key));
        b->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
        b->setTooltip(BM_TJ(tipKey));
        addAndMakeVisible(*b);
        return b;
    };
    m_autoAnalyzeCheck = makeOpt("import.settings.autoAnalyze", "import.settings.autoAnalyzeTip");
    m_detectDuplicatesCheck = makeOpt("import.settings.detectDuplicates", "import.settings.detectDuplicatesTip");
    m_copyToLibraryCheck = makeOpt("import.settings.copyToLibrary", "import.settings.copyToLibraryTip");
    m_readTagsCheck = makeOpt("import.settings.readTags", "import.settings.readTagsTip");

    auto bindToggle = [](juce::ToggleButton* b, const std::string& key, bool defaultVal,
                         std::function<void()> extra = nullptr) {
        b->setToggleState(Prefs::getBool(key, defaultVal), juce::dontSendNotification);
        b->onClick = [b, key, extra] {
            Prefs::setBool(key, b->getToggleState());
            if (extra) extra();
        };
    };
    bindToggle(m_autoAnalyzeCheck.get(), "import.autoAnalyze", true);
    bindToggle(m_detectDuplicatesCheck.get(), "import.detectDuplicates", true);
    bindToggle(m_readTagsCheck.get(), "import.readTags", true);

    m_libraryFolderLabel = std::make_unique<juce::Label>("lf", "");
    m_libraryFolderLabel->setFont(juce::Font(10.5f));
    m_libraryFolderLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_libraryFolderLabel);

    m_libraryFolderBtn = makeBtn("...", Colors::bgLighter());
    m_libraryFolderBtn->onClick = [this] { browseLibraryFolder(); };

    bindToggle(m_copyToLibraryCheck.get(), "import.copyToLibrary", false, [this] {
        const bool on = m_copyToLibraryCheck->getToggleState();
        m_libraryFolderLabel->setEnabled(on);
        m_libraryFolderBtn->setEnabled(on);
        m_libraryFolderLabel->setAlpha(on ? 1.0f : 0.45f);
    });

    {
        const juce::String defaultFolder = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                               .getChildFile("BeatMate Library").getFullPathName();
        const juce::String folder = juce::String::fromUTF8(
            Prefs::getString("import.libraryFolder", defaultFolder.toStdString()).c_str());
        m_libraryFolderLabel->setText(folder, juce::dontSendNotification);
        const bool on = m_copyToLibraryCheck->getToggleState();
        m_libraryFolderLabel->setEnabled(on);
        m_libraryFolderBtn->setEnabled(on);
        m_libraryFolderLabel->setAlpha(on ? 1.0f : 0.45f);
    }

    m_countLabel = std::make_unique<juce::Label>("cnt", "");
    m_countLabel->setFont(juce::Font(11.0f));
    m_countLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_countLabel);

    m_toggleAllBtn = makeBtn(BM_TJ("import.btn.selectAll"), Colors::bgLighter());
    m_toggleAllBtn->onClick = [this] { toggleAll(); };
    m_clearBtn = makeBtn(BM_TJ("import.btn.clear"), Colors::bgLighter());
    m_clearBtn->onClick = [this] { clearStaging(); };
    m_cancelBtn = makeBtn(BM_TJ("import.btn.cancel"), Colors::error().withAlpha(0.3f));
    m_cancelBtn->onClick = [this] { if (onCancelRequested) onCancelRequested(); };
    m_cancelBtn->setVisible(false);
    m_importBtn = makeBtn(BM_TJ("import.btn.import"), Colors::primary());
    m_importBtn->onClick = [this] { startImport(); };

    m_statusLabel = std::make_unique<juce::Label>("st", BM_TJ("import.status.ready"));
    m_statusLabel->setFont(juce::Font(11.5f));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_statusLabel);

    updateCountLabel();
}

FileImportPanel::~FileImportPanel()
{
    m_metaPool.removeAllJobs(true, 2000);
}

juce::Rectangle<int> FileImportPanel::dropZoneBounds() const
{
    return { 0, 0, getWidth(), kDropZoneH };
}

int FileImportPanel::indexOfPath(const juce::String& path) const
{
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        if (m_entries[static_cast<size_t>(i)].path == path)
            return i;
    return -1;
}

void FileImportPanel::addPaths(const juce::StringArray& absolutePaths)
{
    if (m_importing)
        return;

    juce::StringArray audioPaths;
    for (const auto& p : absolutePaths)
    {
        juce::File f(p);
        if (f.isDirectory())
        {
            juce::Array<juce::File> found;
            f.findChildFiles(found, juce::File::findFiles, true);
            for (auto& child : found)
                if (isAudioFile(child))
                    audioPaths.add(child.getFullPathName());
        }
        else if (isAudioFile(f) && f.existsAsFile())
        {
            audioPaths.add(f.getFullPathName());
        }
    }

    int added = 0;
    for (const auto& p : audioPaths)
    {
        if (indexOfPath(p) >= 0)
            continue;
        StagedFile e;
        e.path = p;
        juce::File f(p);
        e.fileName = f.getFileNameWithoutExtension();
        e.ext = f.getFileExtension().substring(1);
        m_entries.push_back(std::move(e));
        scheduleMetaJob(p);
        ++added;
    }

    if (added > 0)
    {
        m_summary->clearSummary();
        m_summary->setVisible(false);
        m_list->refresh();
        updateCountLabel();
    }
    spdlog::info("[Import] Staged {} new files ({} candidates)", added, audioPaths.size());
}

void FileImportPanel::scheduleMetaJob(const juce::String& path)
{
    juce::Component::SafePointer<FileImportPanel> self(this);
    const int generation = m_generation;
    auto snapshot = m_snapshot;
    const bool wantFuzzy = m_detectDuplicatesCheck->getToggleState();

    m_metaPool.addJob([self, path, generation, snapshot, wantFuzzy] {
        Services::Library::TrackMetadata* metaSvc = nullptr;
        Services::Library::TrackDatabase* db = nullptr;
        Services::Library::DuplicateDetector* detector = nullptr;
        if (BeatMate::g_serviceLocator)
        {
            metaSvc = BeatMate::g_serviceLocator->tryGet<Services::Library::TrackMetadata>();
            db = BeatMate::g_serviceLocator->tryGet<Services::Library::TrackDatabase>();
            detector = BeatMate::g_serviceLocator->tryGet<Services::Library::DuplicateDetector>();
        }

        StagedFile result;
        result.path = path;
        result.metaState = StagedFile::MetaState::Failed;

        const std::string pathStd = path.toStdString();
        bool exactDup = db && db->getTrackByPath(pathStd).has_value();

        if (metaSvc)
        {
            auto trackOpt = metaSvc->readMetadata(pathStd);
            if (trackOpt.has_value())
            {
                result.meta = *trackOpt;
                result.metaState = StagedFile::MetaState::Loaded;
                auto art = metaSvc->readAlbumArt(pathStd);
                if (!art.empty())
                {
                    auto img = juce::ImageFileFormat::loadFrom(art.data(), art.size());
                    if (img.isValid())
                    {
                        const int maxDim = 220;
                        if (img.getWidth() > maxDim || img.getHeight() > maxDim)
                            img = img.rescaled(maxDim, maxDim * img.getHeight() / juce::jmax(1, img.getWidth()));
                        result.cover = img;
                    }
                }
            }
        }

        if (exactDup)
        {
            result.dupState = StagedFile::DupState::ExactDuplicate;
            result.dupConfidence = 1.0f;
        }
        else if (wantFuzzy && detector && db
                 && result.metaState == StagedFile::MetaState::Loaded)
        {
            {
                std::lock_guard<std::mutex> lock(snapshot->mutex);
                if (snapshot->generation != generation)
                {
                    snapshot->tracks = db->getAllTracks();
                    snapshot->generation = generation;
                }
            }
            std::lock_guard<std::mutex> lock(snapshot->mutex);
            auto match = detector->findMatchForCandidate(result.meta, snapshot->tracks);
            if (match.has_value())
            {
                result.dupState = StagedFile::DupState::ProbableDuplicate;
                result.dupConfidence = match->confidence;
                result.dupMatchLabel = juce::String::fromUTF8(match->existing.artist.c_str())
                                       + " - " + juce::String::fromUTF8(match->existing.title.c_str());
            }
            else
            {
                result.dupState = StagedFile::DupState::Unique;
            }
        }
        else
        {
            result.dupState = StagedFile::DupState::Unique;
        }

        juce::MessageManager::callAsync([self, generation, result = std::move(result)]() {
            auto* panel = self.getComponent();
            if (!panel || panel->m_generation != generation)
                return;
            const int idx = panel->indexOfPath(result.path);
            if (idx < 0)
                return;
            auto& e = panel->m_entries[static_cast<size_t>(idx)];
            e.meta = result.meta;
            e.cover = result.cover;
            e.metaState = result.metaState;
            e.dupState = result.dupState;
            e.dupConfidence = result.dupConfidence;
            e.dupMatchLabel = result.dupMatchLabel;
            panel->m_list->refreshRow(idx);
            if (panel->m_list->selectedRow() == idx)
                panel->m_inspector->setEntry(&e, idx);
        });
    });
}

void FileImportPanel::browseFiles()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("import.chooseFiles"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        audioFilter());
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, chooser](const juce::FileChooser& fc) {
            juce::StringArray paths;
            for (auto& f : fc.getResults())
                paths.add(f.getFullPathName());
            if (!paths.isEmpty())
                addPaths(paths);
        });
}

void FileImportPanel::browseFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("import.chooseFolder"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (dir != juce::File{} && dir.isDirectory())
                addPaths({ dir.getFullPathName() });
        });
}

void FileImportPanel::browseLibraryFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("import.options.changeFolder"),
        juce::File(m_libraryFolderLabel->getText()));
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (dir == juce::File{} || !dir.isDirectory())
                return;
            m_libraryFolderLabel->setText(dir.getFullPathName(), juce::dontSendNotification);
            Prefs::setString("import.libraryFolder", dir.getFullPathName().toStdString());
        });
}

void FileImportPanel::startImport()
{
    if (m_importing || !onImportRequested)
        return;

    std::vector<Services::Library::StagedImportEntry> entries;
    for (const auto& e : m_entries)
    {
        if (!e.selected)
            continue;
        Services::Library::StagedImportEntry se;
        se.filePath = e.path.toStdString();
        se.titleOverride = e.titleOverride.toStdString();
        se.artistOverride = e.artistOverride.toStdString();
        se.albumOverride = e.albumOverride.toStdString();
        se.genreOverride = e.genreOverride.toStdString();
        entries.push_back(std::move(se));
    }
    if (entries.empty())
    {
        m_statusLabel->setText(BM_TJ("import.noSelection"), juce::dontSendNotification);
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("import.noSelection"), {}, Widgets::ToastNotifier::Kind::Warning, 5000);
        return;
    }

    Services::Library::FileImportOptions options;
    options.readTags = m_readTagsCheck->getToggleState();
    options.detectDuplicates = m_detectDuplicatesCheck->getToggleState();
    options.copyToLibrary = m_copyToLibraryCheck->getToggleState();
    options.autoAnalyze = m_autoAnalyzeCheck->getToggleState();
    options.libraryFolder = m_libraryFolderLabel->getText().toStdString();

    m_summary->clearSummary();
    m_summary->setVisible(false);
    updateImportingState(true);
    m_statusLabel->setText(BM_TJ("import.status.inProgress"), juce::dontSendNotification);
    onImportRequested(entries, options);
}

void FileImportPanel::clearStaging()
{
    if (m_importing)
        return;
    ++m_generation;
    m_entries.clear();
    m_list->refresh();
    m_inspector->clearEntry();
    m_summary->clearSummary();
    m_summary->setVisible(false);
    m_progress = 0.0;
    m_statusLabel->setText(BM_TJ("import.status.ready"), juce::dontSendNotification);
    updateCountLabel();
    repaint();
}

void FileImportPanel::toggleAll()
{
    bool anyUnselected = false;
    for (const auto& e : m_entries)
        if (!e.selected) { anyUnselected = true; break; }
    for (auto& e : m_entries)
        e.selected = anyUnselected;
    m_list->refresh();
    updateCountLabel();
}

void FileImportPanel::updateCountLabel()
{
    int selected = 0;
    for (const auto& e : m_entries)
        if (e.selected) ++selected;
    juce::String fmt = BM_TJ("import.staging.countFmt");
    m_countLabel->setText(fmt.replace("{total}", juce::String(m_entries.size()))
                              .replace("{selected}", juce::String(selected)),
                          juce::dontSendNotification);
}

void FileImportPanel::updateImportingState(bool importing)
{
    m_importing = importing;
    m_importBtn->setEnabled(!importing);
    m_clearBtn->setEnabled(!importing);
    m_toggleAllBtn->setEnabled(!importing);
    m_browseFilesBtn->setEnabled(!importing);
    m_browseFolderBtn->setEnabled(!importing);
    m_cancelBtn->setVisible(importing);
    repaint();
}

void FileImportPanel::setImportProgress(int current, int total, const juce::String& fileName)
{
    m_progress = total > 0 ? static_cast<double>(current) / total : 0.0;
    m_statusLabel->setText(BM_TJ("import.status.fileLoading") + " " + fileName,
                           juce::dontSendNotification);
    repaint();
}

void FileImportPanel::showImportReport(const Services::Library::FileImportReport& report)
{
    updateImportingState(false);
    m_progress = 0.0;
    m_statusLabel->setText(juce::String::formatted(BM_T("import.status.completedFmt").c_str(),
                                                   report.imported),
                           juce::dontSendNotification);

    std::vector<ImportSummaryCard::Counter> counters;
    counters.push_back({ BM_TJ("import.summary.imported"), report.imported, Colors::success() });
    counters.push_back({ BM_TJ("import.summary.duplicates"), report.duplicates, Colors::warning() });
    counters.push_back({ BM_TJ("import.summary.errors"), report.errors, Colors::error() });

    std::vector<ImportSummaryCard::ErrorLine> errors;
    for (const auto& e : report.errorFiles)
        errors.push_back({ juce::String::fromUTF8(e.filePath.c_str()),
                           juce::String::fromUTF8(e.reason.c_str()) });

    const bool offerAnalyze = !m_autoAnalyzeCheck->getToggleState() && report.imported > 0;
    m_summary->setVisible(true);
    m_summary->showSummary(std::move(counters), std::move(errors), offerAnalyze);

    ++m_generation;
    m_entries.clear();
    m_list->refresh();
    m_inspector->clearEntry();
    updateCountLabel();
}

void FileImportPanel::filesDropped(const juce::StringArray& files, int, int)
{
    m_dragOver = false;
    addPaths(files);
    repaint();
}

void FileImportPanel::paint(juce::Graphics& g)
{
    auto dz = dropZoneBounds().toFloat().reduced(0.0f, 2.0f);
    g.setColour(m_dragOver ? Colors::success().withAlpha(0.10f) : Colors::glassWhite());
    g.fillRoundedRectangle(dz, 10.0f);

    {
        const juce::Colour borderCol = m_dragOver ? Colors::success().withAlpha(0.65f)
                                                  : Colors::glassBorder();
        g.setColour(borderCol);
        const float dashes[] = { 8.0f, 5.0f };
        juce::Path outline;
        outline.addRoundedRectangle(dz.reduced(1.0f), 10.0f);
        juce::PathStrokeType stroke(1.4f);
        juce::Path dashed;
        stroke.createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    }

    {
        const juce::Colour glyphCol = m_dragOver ? Colors::success() : Colors::primary();
        const float ring = 18.0f;
        const float gcx = dz.getX() + 44.0f;
        const float gcy = dz.getCentreY();
        g.setColour(glyphCol.withAlpha(0.12f));
        g.fillEllipse(gcx - ring - 4.0f, gcy - ring - 4.0f, (ring + 4.0f) * 2.0f, (ring + 4.0f) * 2.0f);
        g.setColour(glyphCol);
        g.setFont(juce::Font(18.0f));
        g.drawText(juce::CharPointer_UTF8("\xe2\x99\xaa"),
                   static_cast<int>(gcx - ring), static_cast<int>(gcy - ring),
                   static_cast<int>(ring * 2.0f), static_cast<int>(ring * 2.0f),
                   juce::Justification::centred);

        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(13.0f));
        g.drawText(BM_TJ("import.dropzone"),
                   static_cast<int>(gcx + ring + 12.0f), static_cast<int>(dz.getY()),
                   getWidth() - static_cast<int>(gcx + ring) - 380, static_cast<int>(dz.getHeight()),
                   juce::Justification::centredLeft, true);
    }

    const int barY = getHeight() - 34;
    const float pbX = 230.0f;
    const float pbW = static_cast<float>(getWidth()) - pbX - 16.0f;
    if (m_importing && pbW > 50.0f)
    {
        g.setColour(Colors::bgLighter().withAlpha(0.5f));
        g.fillRoundedRectangle(pbX, static_cast<float>(barY), pbW, 12.0f, 6.0f);
        if (m_progress > 0)
        {
            const float fillW = pbW * static_cast<float>(m_progress);
            juce::ColourGradient grad(Colors::primary(), pbX, static_cast<float>(barY),
                                      Colors::success(), pbX + pbW, static_cast<float>(barY), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(pbX, static_cast<float>(barY), fillW, 12.0f, 6.0f);
            g.setColour(juce::Colour(0x26FFFFFF));
            g.fillRoundedRectangle(pbX + 2.0f, static_cast<float>(barY) + 1.0f,
                                   juce::jmax(0.0f, fillW - 4.0f), 3.0f, 1.5f);
        }
    }
}

void FileImportPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    const auto dz = dropZoneBounds();
    m_browseFilesBtn->setBounds(w - 356, dz.getCentreY() - 16, 168, 32);
    m_browseFolderBtn->setBounds(w - 180, dz.getCentreY() - 16, 168, 32);

    int y = kDropZoneH + 8;
    {
        int ox = 4;
        const int optW = juce::jmin(210, (w - 8) / 4);
        m_autoAnalyzeCheck->setBounds(ox, y, optW, kOptionsH); ox += optW;
        m_detectDuplicatesCheck->setBounds(ox, y, optW, kOptionsH); ox += optW;
        m_copyToLibraryCheck->setBounds(ox, y, optW, kOptionsH); ox += optW;
        m_readTagsCheck->setBounds(ox, y, optW, kOptionsH);
    }
    y += kOptionsH + 2;
    m_libraryFolderLabel->setBounds(24, y, w - 90, kFolderRowH - 2);
    m_libraryFolderBtn->setBounds(w - 58, y, 44, kFolderRowH - 4);
    y += kFolderRowH + 6;

    const int contentBottom = h - kFooterH;
    const int listW = w - kInspectorW - 10;
    m_list->setBounds(0, y, listW, contentBottom - y);

    const int rightX = listW + 10;
    const int rightH = contentBottom - y;
    if (m_summary->isVisible())
    {
        const int summaryH = juce::jmin(180, rightH * 45 / 100);
        m_inspector->setBounds(rightX, y, kInspectorW, rightH - summaryH - 8);
        m_summary->setBounds(rightX, contentBottom - summaryH, kInspectorW, summaryH);
    }
    else
    {
        m_inspector->setBounds(rightX, y, kInspectorW, rightH);
    }

    const int footY = h - kFooterH + 8;
    m_countLabel->setBounds(4, footY, 220, 24);
    m_toggleAllBtn->setBounds(230, footY, 130, 28);
    m_clearBtn->setBounds(368, footY, 110, 28);
    m_cancelBtn->setBounds(w - 262, footY, 110, 28);
    m_importBtn->setBounds(w - 144, footY, 140, 28);
    m_statusLabel->setBounds(4, h - 36, 220, 18);
}

void FileImportPanel::retranslateUi()
{
    m_browseFilesBtn->setButtonText(BM_TJ("import.btn.browseFiles"));
    m_browseFolderBtn->setButtonText(BM_TJ("import.btn.browseFolder"));
    m_autoAnalyzeCheck->setButtonText(BM_TJ("import.settings.autoAnalyze"));
    m_autoAnalyzeCheck->setTooltip(BM_TJ("import.settings.autoAnalyzeTip"));
    m_detectDuplicatesCheck->setButtonText(BM_TJ("import.settings.detectDuplicates"));
    m_detectDuplicatesCheck->setTooltip(BM_TJ("import.settings.detectDuplicatesTip"));
    m_copyToLibraryCheck->setButtonText(BM_TJ("import.settings.copyToLibrary"));
    m_copyToLibraryCheck->setTooltip(BM_TJ("import.settings.copyToLibraryTip"));
    m_readTagsCheck->setButtonText(BM_TJ("import.settings.readTags"));
    m_readTagsCheck->setTooltip(BM_TJ("import.settings.readTagsTip"));
    m_toggleAllBtn->setButtonText(BM_TJ("import.btn.selectAll"));
    m_clearBtn->setButtonText(BM_TJ("import.btn.clear"));
    m_cancelBtn->setButtonText(BM_TJ("import.btn.cancel"));
    m_importBtn->setButtonText(BM_TJ("import.btn.import"));
    if (!m_importing)
        m_statusLabel->setText(BM_TJ("import.status.ready"), juce::dontSendNotification);
    m_list->retranslateUi();
    m_inspector->retranslateUi();
    m_summary->retranslateUi();
    updateCountLabel();
    repaint();
}

}
