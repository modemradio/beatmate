#include "LibraryView.h"
#include "../widgets/browser/BrowserPanel.h"
#include "../styles/ColorPalette.h"
#include "analysis/AnalysisColumns.h"
#include "../../core/analysis/WaveformCacheService.h"
#include "../../core/analysis/RgbPeaksGenerator.h"
#include "../dialogs/ColorPickerDialog.h"
#include "../dialogs/DuplicatesDialog.h"
#include "../dialogs/DuplicateManagerDialog.h"
#include "../dialogs/RelinkDialog.h"
#include "../widgets/smartplaylist/NestedSmartRuleEditor.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/library/DuplicateDetector.h"
#include "../../services/persistence/SettingsStore.h"
#include "../../services/config/I18n.h"
#include "../../app/ServiceLocator.h"
#include "../utils/ViewPrefs.h"
#include "../utils/GenreFamily.h"
#include "../widgets/ToastNotifier.h"
#include "../../services/analysis/AudioIntegrityChecker.h"
#include "../../core/analysis/CamelotUtil.h"
#include "../dialogs/IntegrityDialog.h"
#include "../../services/security/LicenseService.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

#include "../../services/djsoftware/DJSoftwareManager.h"
#include "../../services/djsoftware/CollectionSyncService.h"
#include "../../services/djsoftware/rekordbox/RekordboxXmlParser.h"
#include "../../services/djsoftware/rekordbox/RekordboxEnvironment.h"
#include "../../services/djsoftware/rekordbox/RekordboxDatabase.h"
#include "../../services/djsoftware/rekordbox/RekordboxService.h"
#include "../../services/library/PlaylistManager.h"
#include "../../services/djsoftware/serato/SeratoDatabase.h"
#include "../../services/djsoftware/traktor/TraktorCollectionParser.h"
#include "../../services/djsoftware/virtualdj/VirtualDJDatabase.h"
#include "../../services/djsoftware/virtualdj/VirtualDJPlaylistReader.h"
#include "../../services/djsoftware/virtualdj/VirtualDJExporter.h"
#include "../../services/djsoftware/enginedj/EngineDJService.h"
#include "../../services/djsoftware/rekordbox/RekordboxXmlExporter.h"
#include "../../services/djsoftware/traktor/TraktorNmlExporter.h"
#include "../../services/library/TrackImporter.h"
#include "../../services/library/TrackDatabase.h"
#include "../../services/library/TrackMetadata.h"
#include "../../models/RekordboxTrack.h"
#include "../../models/SeratoTrack.h"
#include "../../models/TraktorTrack.h"
#include "../../models/VirtualDJTrack.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>
#include <map>
#include <functional>
#include <filesystem>

namespace BeatMate::UI {

namespace {
bool s_groupGenreFacets = true;

std::unordered_map<int64_t, uint8_t> s_fileHealthCache;   // 1=ok 2=manquant 3=corrompu

Services::Analysis::AudioIntegrityChecker& facetIntegrityChecker()
{
    static Services::Analysis::AudioIntegrityChecker checker;
    return checker;
}

uint8_t fileHealthFor(const LibraryView::TrackTableModel::Track& t)
{
    if (t.trackId <= 0) return 1;
    auto it = s_fileHealthCache.find(t.trackId);
    if (it != s_fileHealthCache.end()) return it->second;
    uint8_t v = 1;
    juce::File f(t.filePath);
    if (t.filePath.isEmpty() || ! f.existsAsFile())
    {
        v = 2;
    }
    else
    {
        using Status = Services::Analysis::AudioIntegrityChecker::Status;
        const auto report = facetIntegrityChecker().statusFor(t.filePath);
        if (report.status == Status::Corrupt || report.status == Status::Unreadable)
            v = 3;
    }
    s_fileHealthCache[t.trackId] = v;
    return v;
}
}

int LibraryView::kColWidths[LibraryView::kNumColumns] = {
    120,  // Waveform
    40,   // Album Art
    180,  // Title
    140,  // Artist
    120,  // Album
    55,   // BPM
    45,   // Key
    50,   // Camelot
    45,   // Energy
    55,   // Duration
    85,   // Genre
    55,   // Rating
    16,   // Color tag
    40,   // Cues
    50,   // Stems
    50,   // Year
    45,   // Plays
    52,   // LUFS
    85,   // Mood
    52,   // Danceability
    100,  // Label
    140,  // Comment
    52,   // Bitrate
    82,   // Last played
    82    // Date added
};
bool LibraryView::kColVisible[LibraryView::kNumColumns] = {
    true, true, true, true, true, true, true, true, true,
    true, true, true, true, true, true, true, true,
    false, false, false, false, false, false, false, false
};
constexpr const char* LibraryView::kColNames[kNumColumns];

LibraryView::LibraryView()
    : m_provider(nullptr)
{
    setupUI();
    setupFilters();
    retranslateUi();
}

LibraryView::LibraryView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    setupUI();
    setupFilters();
    if (m_provider)
    {
        // Chargement des pistes differe a la premiere ouverture de la vue :
        // l'ecran de demarrage est l'Accueil, pas la Bibliotheque.
        m_needsInitialLoad = true;
        auto aliveFlag = std::make_shared<std::atomic<bool>>(true);
        m_aliveFlag = aliveFlag;
        m_provider->onDataChanged([this, aliveFlag] {
            juce::MessageManager::callAsync([this, aliveFlag] {
                if (!aliveFlag->load() || m_refreshQueued) return;
                m_refreshQueued = true;
                juce::Timer::callAfterDelay(250, [this, aliveFlag] {
                    if (!aliveFlag->load()) return;
                    m_refreshQueued = false;
                    const bool rootNode =
                        m_activeNodeType == CollectionNodeType::BeatMateLibrary
                        || m_activeNodeType == CollectionNodeType::Root;
                    const int64_t dbCount = m_provider ? m_provider->getTrackCount() : -1;
                    const int64_t modelCount = m_model ? static_cast<int64_t>(m_model->tracks.size()) : -1;
                    const bool fullReload = rootNode && dbCount >= 0 && dbCount != modelCount;
                    spdlog::info("[Library] dataChanged node={} dbCount={} model={} -> {}",
                                 static_cast<int>(m_activeNodeType), dbCount, modelCount,
                                 fullReload ? "reload" : "inplace");
                    if (fullReload)
                        loadFromDatabase();
                    else
                        refreshVisibleRowsInPlace();
                });
            });
        });

        auto genres = m_provider->getGenreDistribution();
        int genreId = 2;
        for (auto& [genre, count] : genres)
        {
            if (!genre.empty())
            {
                m_genreFilterCombo->addItem(juce::String(genre) + " (" + juce::String(count) + ")",
                                            genreId++);
            }
        }

        auto artists = m_provider->getArtistDistribution();
        int artistId = 2;
        for (auto& [artist, count] : artists)
        {
            if (!artist.empty() && artistId <= 102)
            {
                m_artistFilterCombo->addItem(juce::String(artist) + " (" + juce::String(count) + ")",
                                              artistId++);
            }
        }
    }
    retranslateUi();
}

void LibraryView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("titleLabel", BM_TJ("library.title"));
    m_titleLabel->setFont(juce::Font(20.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_searchEdit = std::make_unique<juce::TextEditor>("searchEdit");
    m_searchEdit->setTextToShowWhenEmpty(BM_TJ("library.searchPlaceholder"), Colors::textMuted());
    m_searchEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgCard());
    m_searchEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEdit->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_searchEdit->addListener(this);
    addAndMakeVisible(*m_searchEdit);

    m_clearSearchBtn = std::make_unique<juce::TextButton>("X");
    m_clearSearchBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_clearSearchBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_clearSearchBtn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    m_clearSearchBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_clearSearchBtn->onClick = [this]()
    {
        m_searchEdit->setText("", true);
        applyAllFilters();
    };
    addAndMakeVisible(*m_clearSearchBtn);

    m_trackCountLabel = std::make_unique<juce::Label>("trackCount", BM_TJ("library.tracksCountZero"));
    m_trackCountLabel->setFont(juce::Font(11.0f));
    m_trackCountLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_trackCountLabel);

    m_importRekordboxXmlBtn = std::make_unique<juce::TextButton>(BM_TJ("library.importXml"));
    m_importRekordboxXmlBtn->setColour(juce::TextButton::buttonColourId, Colors::primaryDark());
    m_importRekordboxXmlBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primaryDark().brighter(0.2f));
    m_importRekordboxXmlBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_importRekordboxXmlBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_importRekordboxXmlBtn->setTooltip(BM_TJ("library.importXmlTooltip"));
    m_importRekordboxXmlBtn->onClick = [this] { importRekordboxXmlFlow(); };
    addAndMakeVisible(*m_importRekordboxXmlBtn);

    m_syncDjPlaylistsBtn = std::make_unique<juce::TextButton>(BM_TJ("library.resyncDj"));
    m_syncDjPlaylistsBtn->setColour(juce::TextButton::buttonColourId, Colors::success().darker(0.45f));
    m_syncDjPlaylistsBtn->setColour(juce::TextButton::buttonOnColourId, Colors::success().darker(0.25f));
    m_syncDjPlaylistsBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_syncDjPlaylistsBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_syncDjPlaylistsBtn->setTooltip(BM_TJ("library.resyncDjTooltip"));
    m_syncDjPlaylistsBtn->onClick = [this] {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (!g_serviceLocator) return;
        auto* sync = g_serviceLocator->tryGet<Services::DJSoftware::CollectionSyncService>();
        if (!sync) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("library.sync.title"),
                BM_TJ("library.sync.unavailable"), "OK");
            return;
        }

        if (m_syncBusy->exchange(true)) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                BM_TJ("library.sync.title"),
                BM_TJ("library.sync.busy"),
                "OK");
            return;
        }

        auto progress = std::make_shared<juce::AlertWindow>(
            BM_TJ("library.sync.title"),
            BM_TJ("library.sync.inProgress"),
            juce::MessageBoxIconType::InfoIcon);
        progress->enterModalState(false, nullptr, false);
        progress->setVisible(true);

        auto countPtr = std::make_shared<int>(0);
        auto busy = m_syncBusy;
        juce::Component::SafePointer<LibraryView> selfWeak(this);

        struct SyncJob : public juce::Thread {
            SyncJob() : juce::Thread("DjPlaylistSync") {}
            std::function<void()> work;
            std::function<void()> onDone;
            void run() override {
                if (work) work();
                auto cb = std::move(onDone);
                juce::MessageManager::callAsync([this, cb = std::move(cb)]() {
                    if (cb) cb();
                    delete this;
                });
            }
        };

        auto* job = new SyncJob();
        job->work = [sync, countPtr]() { *countPtr = sync->syncAllPlaylists(); };
        job->onDone = [selfWeak, countPtr, progress, busy]() {
            progress->exitModalState(0);
            progress->setVisible(false);
            busy->store(false);
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                BM_TJ("library.sync.title"),
                juce::String(*countPtr) + BM_TJ("library.sync.doneSuffix"),
                "OK");
            auto* self = selfWeak.getComponent();
            if (self == nullptr) return;
            self->rebuildCollectionTree();
            if (self->m_collectionTree) self->m_collectionTree->repaint();
            self->loadFromDatabase();
        };
        job->startThread();
    };
    addAndMakeVisible(*m_syncDjPlaylistsBtn);

    m_relinkBtn = std::make_unique<juce::TextButton>(BM_TJ("library.relinkMissing"));
    m_relinkBtn->setColour(juce::TextButton::buttonColourId, Colors::warning().darker(0.45f));
    m_relinkBtn->setColour(juce::TextButton::buttonOnColourId, Colors::warning().darker(0.25f));
    m_relinkBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_relinkBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_relinkBtn->setTooltip(BM_TJ("library.relinkMissingTooltip"));
    m_relinkBtn->onClick = [this] { RelinkDialog::show(m_provider); };
    addAndMakeVisible(*m_relinkBtn);

    m_browseFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("library.browsePc"));
    m_browseFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::secondary().darker(0.4f));
    m_browseFolderBtn->setColour(juce::TextButton::buttonOnColourId, Colors::secondary().darker(0.2f));
    m_browseFolderBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_browseFolderBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_browseFolderBtn->setTooltip(BM_TJ("library.browsePcTooltip"));
    m_browseFolderBtn->onClick = [this] { browseFoldersFlow(); };
    addAndMakeVisible(*m_browseFolderBtn);

    m_viewsBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Vues \xe2\x96\xbe"));
    m_viewsBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_viewsBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_viewsBtn->setTooltip(juce::String::fromUTF8("Vues sauvegard\xc3\xa9""es : recherche + facettes + tri + colonnes"));
    m_viewsBtn->onClick = [this] { showViewsMenu(); };
    addAndMakeVisible(*m_viewsBtn);

    m_tagEditorBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("\xc3\x89""diteur de tags"));
    m_tagEditorBtn->setClickingTogglesState(true);
    m_tagEditorBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_tagEditorBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_tagEditorBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_tagEditorBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_tagEditorBtn->setTooltip(juce::String::fromUTF8(
        "\xc3\x89""dition des tags fa\xc3\xa7on Mp3tag/MusicBee : multi-s\xc3\xa9lection, masques, "
        "renommage, pochettes, casse, nettoyage, tags perso"));
    m_tagEditorBtn->onClick = [this] { toggleTagEditor(m_tagEditorBtn->getToggleState()); };
    addAndMakeVisible(*m_tagEditorBtn);

    m_selectAllTableBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Tout cocher"));
    m_selectAllTableBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_selectAllTableBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_selectAllTableBtn->setTooltip(juce::String::fromUTF8(
        "Coche / d\xc3\xa9""coche tous les morceaux affich\xc3\xa9s. Les morceaux coch\xc3\xa9s sont ceux "
        "sur lesquels agissent l'\xc3\xa9""diteur de tags, l'analyse et les exports (Ctrl+A / Ctrl+D)."));
    m_selectAllTableBtn->onClick = [this] { toggleAllChecked(); };
    addAndMakeVisible(*m_selectAllTableBtn);

    m_repairCorruptBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("R\xc3\xa9parer les corrompus"));
    m_repairCorruptBtn->setColour(juce::TextButton::buttonColourId, Colors::error().darker(0.25f));
    m_repairCorruptBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_repairCorruptBtn->setTooltip(juce::String::fromUTF8(
        "Ouvre la liste des fichiers corrompus affich\xc3\xa9s : cochez ceux \xc3\xa0 r\xc3\xa9parer "
        "(l'original est sauvegard\xc3\xa9 dans integrity_backups)"));
    m_repairCorruptBtn->onClick = [this] { repairCorruptShown(); };
    m_repairCorruptBtn->setVisible(false);
    addAndMakeVisible(*m_repairCorruptBtn);

    m_duplicatesBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("Doublons\xe2\x80\xa6"));
    m_duplicatesBtn->setColour(juce::TextButton::buttonColourId, Colors::secondary().darker(0.15f));
    m_duplicatesBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_duplicatesBtn->setTooltip(juce::String::fromUTF8(
        "Recherche les doublons par empreinte de fichier, dur\xc3\xa9""e/BPM/cl\xc3\xa9, tags ou nom. "
        "Choisit automatiquement la version \xc3\xa0 conserver et indique l'espace r\xc3\xa9""cup\xc3\xa9rable."));
    m_duplicatesBtn->onClick = [this] {
        juce::Component::SafePointer<LibraryView> safe(this);
        DuplicateManagerDialog::show(m_provider, [safe] {
            if (safe != nullptr) safe->refreshLibrary();
        });
    };
    addAndMakeVisible(*m_duplicatesBtn);

    m_integrityBtn = std::make_unique<juce::TextButton>(juce::String::fromUTF8("V\xc3\xa9rifier / R\xc3\xa9parer \xe2\x96\xbe"));
    m_integrityBtn->setColour(juce::TextButton::buttonColourId, Colors::warning().darker(0.35f));
    m_integrityBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_integrityBtn->setTooltip(juce::String::fromUTF8(
        "V\xc3\xa9rifie les fichiers audio (tronqu\xc3\xa9s, corrompus) et les r\xc3\xa9pare sans perte"));
    m_integrityBtn->onClick = [this] { showIntegrityMenu(); };
    addAndMakeVisible(*m_integrityBtn);

    // Browser panel - kept for legacy listener wiring but not shown
    m_browserPanel = std::make_unique<BrowserPanel>();
    m_browserPanel->addListener(this);

    try {
        m_djManager = std::make_unique<Services::DJSoftware::DJSoftwareManager>();
    } catch (...) {
        m_djManager.reset();
    }

    m_collectionTree = std::make_unique<juce::TreeView>("collectionTree");
    m_collectionTree->setColour(juce::TreeView::backgroundColourId, Colors::bgSidebar());
    m_collectionTree->setDefaultOpenness(true);
    m_collectionTree->setMultiSelectEnabled(false);
    m_collectionTree->setIndentSize(14);
    addAndMakeVisible(*m_collectionTree);
    // buildCollectionTree() deferred to after m_trackList/m_model exist.

    m_model = std::make_unique<TrackTableModel>();

    m_trackList = std::make_unique<juce::ListBox>("trackList", m_model.get());
    m_trackList->setColour(juce::ListBox::backgroundColourId, Colors::bg());
    m_trackList->setRowHeight(34);
    m_trackList->setMultipleSelectionEnabled(true);
    addAndMakeVisible(*m_trackList);

    loadColWidths();
    loadColVisibility();
    updateTableContentWidth();
    m_trackList->getViewport()->getHorizontalScrollBar().addListener(this);

    m_model->onWaveformNeeded = [this](int64_t trackId) { requestRowWaveform(trackId); };
    m_model->onArtNeeded = [this](int64_t trackId, const juce::String& filePath) { requestRowArt(trackId, filePath); };
    m_model->onSelectionChanged = [this](int row) { showDetailForRow(row); };
    m_model->onCheckedChanged = [this] {
        if (m_trackList) m_trackList->repaint();
        updateCheckedUi();
        repaint();
    };

    m_detailPanel = std::make_unique<TrackDetailPanel>();
    addAndMakeVisible(*m_detailPanel);

    m_detailVisible = false;
    m_detailToggleBtn = std::make_unique<juce::TextButton>(BM_TJ("library.details"));
    m_detailToggleBtn->setClickingTogglesState(true);
    m_detailToggleBtn->setToggleState(m_detailVisible, juce::dontSendNotification);
    m_detailToggleBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_detailToggleBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_detailToggleBtn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    m_detailToggleBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_detailToggleBtn->onClick = [this] {
        m_detailVisible = m_detailToggleBtn->getToggleState();
        if (m_detailVisible && m_trackList)
            showDetailForRow(m_trackList->getSelectedRow());
        resized();
        repaint();
    };
    addAndMakeVisible(*m_detailToggleBtn);

    m_facetBar = std::make_unique<FacetBar>();
    m_facetBar->onDimChanged = [this](int d) {
        if (m_facetBar) { m_facetBar->dim = d; m_facetBar->scrollX = 0.0f; }
        applyFacetsOnly();
    };
    m_facetBar->onChipToggled = [this](const juce::String& value) {
        const int d = m_facetBar ? m_facetBar->dim : 0;
        auto& active = m_activeFacets[d];
        if (! active.insert(value).second) active.erase(value);
        if (active.empty()) m_activeFacets.erase(d);
        applyFacetsOnly();
    };
    m_facetBar->onClearAll = [this] {
        m_activeFacets.clear();
        applyFacetsOnly();
    };
    m_facetBar->groupGenres = Prefs::getBool("library.groupGenres", true);
    s_groupGenreFacets = m_facetBar->groupGenres;
    m_facetBar->onGroupToggled = [this](bool grouped) {
        s_groupGenreFacets = grouped;
        Prefs::setBool("library.groupGenres", grouped);
        m_activeFacets.erase(0);   // les valeurs actives changent de forme
        applyFacetsOnly();
    };
    addAndMakeVisible(*m_facetBar);

    m_model->onCellClicked = [this](int row, int columnIndex, const juce::String& value) {
        switch (columnIndex)
        {
            case 3: // Artist
                filterChanged("Artistes", value);
                break;
            case 5: // BPM - set both sliders to exact value
            {
                int bpmVal = value.getIntValue();
                if (bpmVal > 0)
                {
                    m_bpmMinSlider->setValue(bpmVal, juce::dontSendNotification);
                    m_bpmMaxSlider->setValue(bpmVal, juce::dontSendNotification);
                    m_bpmRangeLabel->setText(juce::String(bpmVal) + " BPM", juce::dontSendNotification);
                    applyAllFilters();
                }
                break;
            }
            case 6: // Key
            case 7: // Camelot (same filter target)
            {
                for (int i = 0; i < m_keyFilterCombo->getNumItems(); ++i)
                {
                    if (m_keyFilterCombo->getItemText(i) == value)
                    {
                        m_keyFilterCombo->setSelectedId(m_keyFilterCombo->getItemId(i), juce::sendNotificationSync);
                        return;
                    }
                }
                m_searchEdit->setText(value, true);
                break;
            }
            case 10: // Genre
                filterChanged("Genres", value);
                break;
            case 12: // Color tag → inline palette
                showColorMenuForRow(row);
                break;
            default:
                break;
        }
    };

    m_model->onDoubleClicked = [this](int row) {
        if (row >= 0 && row < static_cast<int>(m_model->filteredTracks.size())) {
            auto& t = m_model->filteredTracks[static_cast<size_t>(row)];
            m_listeners.call([&t](Listener& l) { l.trackDoubleClicked(t.trackId); });
            if (onTrackPreview) {
                double bpmVal = t.bpm.getDoubleValue();
                onTrackPreview(t.title, t.artist, bpmVal, t.key, t.filePath);
            }
        }
    };

    m_model->onRightClick = [this](int row, const juce::MouseEvent& e) {
        showContextMenu(row, e);
    };

    m_model->onRatingChanged = [this](int64_t trackId, int newRating) {
        if (m_provider && trackId > 0) {
            auto dbTrack = m_provider->getTrack(trackId);
            dbTrack.rating = newRating;
            m_provider->updateTrack(dbTrack);
            if (m_trackList) m_trackList->repaint();
        }
    };

    m_statsPanel = std::make_unique<StatisticsPanel>();
    m_statsPanel->setVisible(false);
    addAndMakeVisible(m_statsPanel.get());

    m_statsToggleBtn = std::make_unique<juce::TextButton>(BM_TJ("library.stats"));
    m_statsToggleBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_statsToggleBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
    m_statsToggleBtn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    m_statsToggleBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_statsToggleBtn->onClick = [this]() {
        m_statsVisible = !m_statsVisible;
        m_statsPanel->setVisible(m_statsVisible);
        if (m_statsVisible) refreshStats();
        Prefs::setBool("library.statsVisible", m_statsVisible);
        resized();
    };
    addAndMakeVisible(m_statsToggleBtn.get());

    m_presetCombo = std::make_unique<juce::ComboBox>("presets");
    m_presetCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_presetCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_presetCombo->onChange = [this]() {
        int idx = m_presetCombo->getSelectedId() - 1;
        Prefs::setInt("library.presetIndex", idx);
        applyFilterPreset(idx);
    };
    addAndMakeVisible(m_presetCombo.get());
    loadFilterPresets();

    addKeyListener(this);

    // Build the collection tree now that the track list + model exist.
    buildCollectionTree();
}


void LibraryView::setupFilters()
{
    m_filterSectionLabel = std::make_unique<juce::Label>("filterLabel", BM_TJ("library.filters"));
    m_filterSectionLabel->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    m_filterSectionLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_filterSectionLabel);

    m_bpmMinSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_bpmMinSlider->setRange(60, 200, 1);
    m_bpmMinSlider->setValue(60, juce::dontSendNotification);
    m_bpmMinSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_bpmMinSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_bpmMinSlider->setColour(juce::Slider::thumbColourId, Colors::primaryHover());
    m_bpmMinSlider->onValueChange = [this]()
    {
        if (m_bpmMinSlider->getValue() > m_bpmMaxSlider->getValue())
            m_bpmMaxSlider->setValue(m_bpmMinSlider->getValue(), juce::dontSendNotification);
        m_bpmRangeLabel->setText(juce::String((int)m_bpmMinSlider->getValue()) + "-" +
                                 juce::String((int)m_bpmMaxSlider->getValue()) + " BPM",
                                 juce::dontSendNotification);
        Prefs::setInt("library.bpmMin", (int)m_bpmMinSlider->getValue());
        Prefs::setInt("library.bpmMax", (int)m_bpmMaxSlider->getValue());
        applyAllFilters();
    };
    addAndMakeVisible(*m_bpmMinSlider);

    m_bpmMaxSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_bpmMaxSlider->setRange(60, 200, 1);
    m_bpmMaxSlider->setValue(200, juce::dontSendNotification);
    m_bpmMaxSlider->setColour(juce::Slider::trackColourId, Colors::primary());
    m_bpmMaxSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_bpmMaxSlider->setColour(juce::Slider::thumbColourId, Colors::primaryHover());
    m_bpmMaxSlider->onValueChange = [this]()
    {
        if (m_bpmMaxSlider->getValue() < m_bpmMinSlider->getValue())
            m_bpmMinSlider->setValue(m_bpmMaxSlider->getValue(), juce::dontSendNotification);
        m_bpmRangeLabel->setText(juce::String((int)m_bpmMinSlider->getValue()) + "-" +
                                 juce::String((int)m_bpmMaxSlider->getValue()) + " BPM",
                                 juce::dontSendNotification);
        Prefs::setInt("library.bpmMin", (int)m_bpmMinSlider->getValue());
        Prefs::setInt("library.bpmMax", (int)m_bpmMaxSlider->getValue());
        applyAllFilters();
    };
    addAndMakeVisible(*m_bpmMaxSlider);

    m_bpmRangeLabel = std::make_unique<juce::Label>("bpmRange", "60-200 BPM");
    m_bpmRangeLabel->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    m_bpmRangeLabel->setColour(juce::Label::textColourId, Colors::primary());
    addAndMakeVisible(*m_bpmRangeLabel);

    m_keyFilterCombo = std::make_unique<juce::ComboBox>("keyFilter");
    m_keyFilterCombo->addItem(BM_TJ("library.allKeys"), 1);
    // Musical keys (standard notation) — matches what's actually in the database
    juce::StringArray keys = {
        "Am", "A",  "Bm", "B",  "Cm", "C",  "Dm", "D",
        "Em", "E",  "Fm", "F",  "Gm", "G",
        "Abm", "Ab", "Bbm", "Bb", "Dbm", "Db",
        "Ebm", "Eb", "F#m", "F#", "G#m", "G#",
        // Camelot notation (for tracks imported with Camelot keys)
        "1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B",
        "5A", "5B", "6A", "6B", "7A", "7B", "8A", "8B",
        "9A", "9B", "10A", "10B", "11A", "11B", "12A", "12B"
    };
    for (int i = 0; i < keys.size(); ++i)
        m_keyFilterCombo->addItem(keys[i], i + 2);
    m_keyFilterCombo->setSelectedId(1, juce::dontSendNotification);
    m_keyFilterCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_keyFilterCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_keyFilterCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_keyFilterCombo->onChange = [this]() {
        Prefs::setInt("library.keyFilterId", m_keyFilterCombo->getSelectedId());
        applyAllFilters();
    };
    addAndMakeVisible(*m_keyFilterCombo);

    m_genreFilterCombo = std::make_unique<juce::ComboBox>("genreFilter");
    m_genreFilterCombo->addItem(BM_TJ("library.allGenres"), 1);
    m_genreFilterCombo->setSelectedId(1, juce::dontSendNotification);
    m_genreFilterCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_genreFilterCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_genreFilterCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_genreFilterCombo->onChange = [this]() {
        Prefs::setString("library.genreFilterText", m_genreFilterCombo->getText().toStdString());
        applyAllFilters();
    };
    addAndMakeVisible(*m_genreFilterCombo);

    m_artistFilterCombo = std::make_unique<juce::ComboBox>("artistFilter");
    m_artistFilterCombo->addItem(BM_TJ("library.allArtists"), 1);
    m_artistFilterCombo->setSelectedId(1, juce::dontSendNotification);
    m_artistFilterCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_artistFilterCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_artistFilterCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_artistFilterCombo->onChange = [this]() {
        Prefs::setString("library.artistFilterText", m_artistFilterCombo->getText().toStdString());
        applyAllFilters();
    };
    addAndMakeVisible(*m_artistFilterCombo);

    m_energyMinSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_energyMinSlider->setRange(1, 10, 1);
    m_energyMinSlider->setValue(1, juce::dontSendNotification);
    m_energyMinSlider->setColour(juce::Slider::trackColourId, Colors::warning());
    m_energyMinSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_energyMinSlider->setColour(juce::Slider::thumbColourId, Colors::warning());
    m_energyMinSlider->onValueChange = [this]()
    {
        if (m_energyMinSlider->getValue() > m_energyMaxSlider->getValue())
            m_energyMaxSlider->setValue(m_energyMinSlider->getValue(), juce::dontSendNotification);
        m_energyRangeLabel->setText("E:" + juce::String((int)m_energyMinSlider->getValue()) + "-" +
                                    juce::String((int)m_energyMaxSlider->getValue()),
                                    juce::dontSendNotification);
        Prefs::setInt("library.energyMin", (int)m_energyMinSlider->getValue());
        Prefs::setInt("library.energyMax", (int)m_energyMaxSlider->getValue());
        applyAllFilters();
    };
    addAndMakeVisible(*m_energyMinSlider);

    m_energyMaxSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_energyMaxSlider->setRange(1, 10, 1);
    m_energyMaxSlider->setValue(10, juce::dontSendNotification);
    m_energyMaxSlider->setColour(juce::Slider::trackColourId, Colors::warning());
    m_energyMaxSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_energyMaxSlider->setColour(juce::Slider::thumbColourId, Colors::warning());
    m_energyMaxSlider->onValueChange = [this]()
    {
        if (m_energyMaxSlider->getValue() < m_energyMinSlider->getValue())
            m_energyMinSlider->setValue(m_energyMaxSlider->getValue(), juce::dontSendNotification);
        m_energyRangeLabel->setText("E:" + juce::String((int)m_energyMinSlider->getValue()) + "-" +
                                    juce::String((int)m_energyMaxSlider->getValue()),
                                    juce::dontSendNotification);
        Prefs::setInt("library.energyMin", (int)m_energyMinSlider->getValue());
        Prefs::setInt("library.energyMax", (int)m_energyMaxSlider->getValue());
        applyAllFilters();
    };
    addAndMakeVisible(*m_energyMaxSlider);

    m_energyRangeLabel = std::make_unique<juce::Label>("energyRange", "E:1-10");
    m_energyRangeLabel->setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    m_energyRangeLabel->setColour(juce::Label::textColourId, Colors::warning());
    addAndMakeVisible(*m_energyRangeLabel);

    m_ratingFilterCombo = std::make_unique<juce::ComboBox>("ratingFilter");
    m_ratingFilterCombo->addItem(BM_TJ("library.allRatings"), 1);
    m_ratingFilterCombo->addItem("1+", 2);
    m_ratingFilterCombo->addItem("2+", 3);
    m_ratingFilterCombo->addItem("3+", 4);
    m_ratingFilterCombo->addItem("4+", 5);
    m_ratingFilterCombo->addItem("5", 6);
    m_ratingFilterCombo->setSelectedId(1, juce::dontSendNotification);
    m_ratingFilterCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_ratingFilterCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_ratingFilterCombo->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_ratingFilterCombo->onChange = [this]() {
        Prefs::setInt("library.ratingFilterId", m_ratingFilterCombo->getSelectedId());
        applyAllFilters();
    };
    addAndMakeVisible(*m_ratingFilterCombo);

    m_resetFiltersBtn = std::make_unique<juce::TextButton>(BM_TJ("library.resetFilters"));
    m_resetFiltersBtn->setColour(juce::TextButton::buttonColourId, Colors::error().withAlpha(0.3f));
    m_resetFiltersBtn->setColour(juce::TextButton::buttonOnColourId, Colors::error().withAlpha(0.5f));
    m_resetFiltersBtn->setColour(juce::TextButton::textColourOffId, Colors::error());
    m_resetFiltersBtn->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
    m_resetFiltersBtn->onClick = [this]()
    {
        m_searchEdit->setText("", false);
        m_bpmMinSlider->setValue(60, juce::dontSendNotification);
        m_bpmMaxSlider->setValue(200, juce::dontSendNotification);
        m_bpmRangeLabel->setText("60-200 BPM", juce::dontSendNotification);
        m_keyFilterCombo->setSelectedId(1, juce::dontSendNotification);
        m_genreFilterCombo->setSelectedId(1, juce::dontSendNotification);
        if (m_artistFilterCombo) m_artistFilterCombo->setSelectedId(1, juce::dontSendNotification);
        m_energyMinSlider->setValue(1, juce::dontSendNotification);
        m_energyMaxSlider->setValue(10, juce::dontSendNotification);
        m_energyRangeLabel->setText("E:1-10", juce::dontSendNotification);
        m_ratingFilterCombo->setSelectedId(1, juce::dontSendNotification);
        Prefs::setInt("library.bpmMin", 60);
        Prefs::setInt("library.bpmMax", 200);
        Prefs::setInt("library.energyMin", 1);
        Prefs::setInt("library.energyMax", 10);
        Prefs::setInt("library.keyFilterId", 1);
        Prefs::setInt("library.ratingFilterId", 1);
        Prefs::setString("library.genreFilterText", "");
        Prefs::setString("library.artistFilterText", "");
        applyAllFilters();
    };
    addAndMakeVisible(*m_resetFiltersBtn);

    {
        const int bpmMin = Prefs::getInt("library.bpmMin", 60);
        const int bpmMax = Prefs::getInt("library.bpmMax", 200);
        const int eMin   = Prefs::getInt("library.energyMin", 1);
        const int eMax   = Prefs::getInt("library.energyMax", 10);
        const int keyId  = Prefs::getInt("library.keyFilterId", 1);
        const int rateId = Prefs::getInt("library.ratingFilterId", 1);
        m_bpmMinSlider->setValue(bpmMin, juce::dontSendNotification);
        m_bpmMaxSlider->setValue(bpmMax, juce::dontSendNotification);
        m_bpmRangeLabel->setText(juce::String(bpmMin) + "-" + juce::String(bpmMax) + " BPM",
                                 juce::dontSendNotification);
        m_energyMinSlider->setValue(eMin, juce::dontSendNotification);
        m_energyMaxSlider->setValue(eMax, juce::dontSendNotification);
        m_energyRangeLabel->setText("E:" + juce::String(eMin) + "-" + juce::String(eMax),
                                    juce::dontSendNotification);
        if (keyId >= 1 && keyId <= m_keyFilterCombo->getNumItems() + 1)
            m_keyFilterCombo->setSelectedId(keyId, juce::dontSendNotification);
        if (rateId >= 1 && rateId <= m_ratingFilterCombo->getNumItems() + 1)
            m_ratingFilterCombo->setSelectedId(rateId, juce::dontSendNotification);

        const bool statsVisible = Prefs::getBool("library.statsVisible", false);
        if (statsVisible != m_statsVisible) {
            m_statsVisible = statsVisible;
            if (m_statsPanel) m_statsPanel->setVisible(m_statsVisible);
        }
    }
}

void LibraryView::retranslateUi()
{
    auto retranslateFirstItem = [](juce::ComboBox* cb, const juce::String& text) {
        if (!cb) return;
        const int sel = cb->getSelectedId();
        cb->changeItemText(1, text);
        if (sel == 1)
            cb->setSelectedId(1, juce::dontSendNotification);
    };

    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("library.title"), juce::dontSendNotification);

    if (m_searchEdit)
        m_searchEdit->setTextToShowWhenEmpty(BM_TJ("library.searchPlaceholder"), Colors::textMuted());

    if (m_relinkBtn) {
        m_relinkBtn->setButtonText(BM_TJ("library.relinkMissing"));
        m_relinkBtn->setTooltip(BM_TJ("library.relinkMissingTooltip"));
    }

    if (m_detailToggleBtn)
        m_detailToggleBtn->setButtonText(BM_TJ("library.details"));
    if (m_detailPanel)
        m_detailPanel->retranslateUi();

    if (m_importRekordboxXmlBtn) {
        m_importRekordboxXmlBtn->setButtonText(BM_TJ("library.importXml"));
        m_importRekordboxXmlBtn->setTooltip(BM_TJ("library.importXmlTooltip"));
    }

    if (m_syncDjPlaylistsBtn) {
        m_syncDjPlaylistsBtn->setButtonText(BM_TJ("library.resyncDj"));
        m_syncDjPlaylistsBtn->setTooltip(BM_TJ("library.resyncDjTooltip"));
    }

    if (m_browseFolderBtn) {
        m_browseFolderBtn->setButtonText(BM_TJ("library.browsePc"));
        m_browseFolderBtn->setTooltip(BM_TJ("library.browsePcTooltip"));
    }

    if (m_statsToggleBtn)
        m_statsToggleBtn->setButtonText(BM_TJ("library.stats"));

    if (m_filterSectionLabel)
        m_filterSectionLabel->setText(BM_TJ("library.filters"), juce::dontSendNotification);

    retranslateFirstItem(m_keyFilterCombo.get(),    BM_TJ("library.allKeys"));
    retranslateFirstItem(m_genreFilterCombo.get(),  BM_TJ("library.allGenres"));
    retranslateFirstItem(m_artistFilterCombo.get(), BM_TJ("library.allArtists"));
    retranslateFirstItem(m_ratingFilterCombo.get(), BM_TJ("library.allRatings"));

    if (m_resetFiltersBtn)
        m_resetFiltersBtn->setButtonText(BM_TJ("library.resetFilters"));

    repaint();
}


void LibraryView::sortByColumn(SortColumn col)
{
    if (m_sortColumn == col)
        m_sortAscending = !m_sortAscending;
    else
    {
        m_sortColumn = col;
        m_sortAscending = true;
    }
    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    repaint(); // redraw header arrows
}

void LibraryView::handleColumnHeaderClick(int columnIndex)
{
    if (columnIndex >= 0 && columnIndex < kNumColumns)
    {
        spdlog::info("[Library] Sort by column {}: {}", columnIndex, kColNames[columnIndex]);
        sortByColumn(static_cast<SortColumn>(columnIndex));
    }
}

void LibraryView::mouseDown(const juce::MouseEvent& e)
{
    const int edgeCol = columnEdgeAtX(e.x, e.y);
    if (edgeCol >= 0)
    {
        m_resizingCol = edgeCol;
        m_resizeStartX = e.x;
        m_resizeStartW = kColWidths[edgeCol];
        return;
    }

    int headerY = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight;
    int sidebarWidth = 260; // Collection tree width

    if (e.y >= headerY && e.y < headerY + kColumnHeaderHeight && e.x >= sidebarWidth)
    {
        if (e.mods.isPopupMenu())
        {
            showColumnPicker();
            return;
        }
        const int viewX = m_trackList ? m_trackList->getViewport()->getViewPositionX() : 0;
        if (e.x < sidebarWidth + kCheckGutter - viewX) { toggleAllChecked(); return; }
        int colX = sidebarWidth + kCheckGutter - viewX;
        for (int i = 0; i < kNumColumns; ++i)
        {
            if (! kColVisible[i]) continue;
            if (e.x >= colX && e.x < colX + kColWidths[i])
            {
                handleColumnHeaderClick(i);
                return;
            }
            colX += kColWidths[i];
        }
    }
}

int LibraryView::columnEdgeAtX(int mouseX, int mouseY) const
{
    const int headerY = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight;
    if (mouseY < headerY || mouseY >= headerY + kColumnHeaderHeight)
        return -1;
    const int sidebarWidth = 260;
    if (mouseX < sidebarWidth)
        return -1;
    const int viewX = m_trackList ? m_trackList->getViewport()->getViewPositionX() : 0;
    int colX = sidebarWidth + kCheckGutter - viewX;
    for (int i = 0; i < kNumColumns; ++i)
    {
        if (! kColVisible[i]) continue;
        colX += kColWidths[i];
        if (std::abs(mouseX - colX) <= 4)
            return i;
    }
    return -1;
}

void LibraryView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(columnEdgeAtX(e.x, e.y) >= 0 || m_resizingCol >= 0
                       ? juce::MouseCursor::LeftRightResizeCursor
                       : juce::MouseCursor::NormalCursor);
}

void LibraryView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_resizingCol < 0)
        return;
    kColWidths[m_resizingCol] = juce::jlimit(16, 600, m_resizeStartW + (e.x - m_resizeStartX));
    updateTableContentWidth();
    if (m_trackList) m_trackList->repaint();
    repaint();
}

void LibraryView::mouseUp(const juce::MouseEvent&)
{
    if (m_resizingCol >= 0)
    {
        m_resizingCol = -1;
        saveColWidths();
    }
}

void LibraryView::scrollBarMoved(juce::ScrollBar*, double)
{
    // Horizontal scroll: keep the painted column headers in sync with the rows.
    const int headerY = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight;
    repaint(0, headerY, getWidth(), kColumnHeaderHeight);
}

void LibraryView::loadColWidths()
{
    const auto csv = juce::String(Prefs::getString("library.colWidths", ""));
    if (csv.isEmpty()) return;
    auto parts = juce::StringArray::fromTokens(csv, ",", "");
    for (int i = 0; i < juce::jmin(kNumColumns, parts.size()); ++i)
    {
        const int v = parts[i].getIntValue();
        if (v >= 16 && v <= 600) kColWidths[i] = v;
    }
}

void LibraryView::saveColWidths()
{
    juce::StringArray parts;
    for (int i = 0; i < kNumColumns; ++i) parts.add(juce::String(kColWidths[i]));
    Prefs::setString("library.colWidths", parts.joinIntoString(",").toStdString());
}

void LibraryView::updateTableContentWidth()
{
    if (! m_trackList) return;
    int total = kCheckGutter + 8;
    for (int i = 0; i < kNumColumns; ++i)
        if (kColVisible[i]) total += kColWidths[i];
    m_trackList->setMinimumContentWidth(total);
}

void LibraryView::loadColVisibility()
{
    const auto csv = juce::String(Prefs::getString("library.colVisible", ""));
    if (csv.isEmpty()) return;
    auto parts = juce::StringArray::fromTokens(csv, ",", "");
    for (int i = 0; i < juce::jmin(kNumColumns, parts.size()); ++i)
        kColVisible[i] = parts[i].getIntValue() != 0;
    kColVisible[2] = true;   // TITRE reste toujours visible
}

void LibraryView::saveColVisibility()
{
    juce::StringArray parts;
    for (int i = 0; i < kNumColumns; ++i) parts.add(kColVisible[i] ? "1" : "0");
    Prefs::setString("library.colVisible", parts.joinIntoString(",").toStdString());
}

void LibraryView::showColumnPicker()
{
    static const char* kPickerKeys[kNumColumns] = {
        "library.col.wave", "library.col.art", "library.col.title", "library.col.artist",
        "library.col.album", "library.col.bpm", "library.col.key", "library.col.camelot",
        "library.col.energy", "library.col.duration", "library.col.genre", "library.col.rating",
        "library.col.colorTag", "library.col.cues", "library.col.stems", "library.col.year",
        "library.col.plays",
        "library.col.lufs", "library.col.mood", "library.col.danceability", "library.col.label",
        "library.col.comment", "library.col.bitrate", "library.col.lastPlayed", "library.col.dateAdded"
    };

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Colonnes affich\xc3\xa9""es"));
    for (int i = 0; i < kNumColumns; ++i)
    {
        if (i == 2) continue;   // TITRE non désactivable
        juce::String label = BM_TJ(kPickerKeys[i]);
        if (label == juce::String(kPickerKeys[i])) label = kColNames[i]; // clé non traduite → nom brut
        m.addItem(1000 + i, label, true, kColVisible[i]);
    }

    juce::Component::SafePointer<LibraryView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe](int r)
        {
            if (safe == nullptr || r < 1000 || r >= 1000 + kNumColumns) return;
            const int col = r - 1000;
            kColVisible[col] = ! kColVisible[col];
            safe->saveColVisibility();
            safe->updateTableContentWidth();
            if (safe->m_trackList) safe->m_trackList->repaint();
            safe->repaint();
            safe->showColumnPicker();   // menu rouvert : cocher plusieurs colonnes d'affilée
        });
}

void LibraryView::drawSortArrow(juce::Graphics& g, int x, int y, int w, int h, bool ascending)
{
    float cx = static_cast<float>(x + w - 14);
    float cy = static_cast<float>(y + h / 2);
    juce::Path arrow;
    if (ascending)
    {
        arrow.addTriangle(cx - 6, cy + 5, cx + 6, cy + 5, cx, cy - 5);
    }
    else
    {
        arrow.addTriangle(cx - 6, cy - 5, cx + 6, cy - 5, cx, cy + 5);
    }
    g.setColour(Colors::textPrimary().withAlpha(0.2f));
    g.fillPath(arrow, juce::AffineTransform::scale(1.4f, 1.4f, cx, cy));
    g.setColour(Colors::textPrimary());
    g.fillPath(arrow);
    g.setColour(Colors::textPrimary().withAlpha(0.6f));
    g.strokePath(arrow, juce::PathStrokeType(1.0f));
}

void LibraryView::TrackTableModel::sortTracks()
{
    auto compare = [this](const Track& a, const Track& b) -> bool {
        int cmp = 0;
        switch (sortCol)
        {
        case SortColumn::Title:
            cmp = a.title.compareIgnoreCase(b.title);
            break;
        case SortColumn::Artist:
            cmp = a.artist.compareIgnoreCase(b.artist);
            break;
        case SortColumn::Album:
            cmp = a.album.compareIgnoreCase(b.album);
            break;
        case SortColumn::BPM:
        {
            double ba = a.bpm.getDoubleValue();
            double bb = b.bpm.getDoubleValue();
            cmp = (ba < bb) ? -1 : (ba > bb ? 1 : 0);
            break;
        }
        case SortColumn::Key:
            cmp = a.key.compareIgnoreCase(b.key);
            break;
        case SortColumn::Camelot:
            cmp = a.camelot.compareIgnoreCase(b.camelot);
            break;
        case SortColumn::Energy:
        {
            int ea = a.energy.getIntValue();
            int eb = b.energy.getIntValue();
            cmp = ea - eb;
            break;
        }
        case SortColumn::Duration:
            cmp = a.duration.compareIgnoreCase(b.duration);
            break;
        case SortColumn::Genre:
            cmp = a.genre.compareIgnoreCase(b.genre);
            break;
        case SortColumn::Rating:
        {
            int ra = a.rating.getIntValue();
            int rb = b.rating.getIntValue();
            cmp = ra - rb;
            break;
        }
        case SortColumn::Cues:
            cmp = a.cueCount - b.cueCount;
            break;
        case SortColumn::Stems:
            cmp = a.stems.compareIgnoreCase(b.stems);
            break;
        case SortColumn::Year:
            cmp = a.year.compareIgnoreCase(b.year);
            break;
        case SortColumn::PlayCount:
            cmp = a.playCount - b.playCount;
            break;
        case SortColumn::Lufs:
        {
            const double la = a.lufs.getDoubleValue();
            const double lb = b.lufs.getDoubleValue();
            cmp = (la < lb) ? -1 : (la > lb ? 1 : 0);
            break;
        }
        case SortColumn::Mood:
            cmp = a.mood.compareIgnoreCase(b.mood);
            break;
        case SortColumn::Danceability:
        {
            const int da = a.danceability.getIntValue();
            const int db = b.danceability.getIntValue();
            cmp = da - db;
            break;
        }
        case SortColumn::RecordLabel:
            cmp = a.label.compareIgnoreCase(b.label);
            break;
        case SortColumn::Comment:
            cmp = a.comment.compareIgnoreCase(b.comment);
            break;
        case SortColumn::Bitrate:
            cmp = a.bitrate.getIntValue() - b.bitrate.getIntValue();
            break;
        case SortColumn::LastPlayed:
            cmp = a.lastPlayed.compare(b.lastPlayed);   // format ISO → tri lexicographique correct
            break;
        case SortColumn::DateAdded:
            cmp = a.dateAdded.compare(b.dateAdded);
            break;
        default:
            cmp = 0;
        }
        return sortAsc ? (cmp < 0) : (cmp > 0);
    };

    std::stable_sort(filteredTracks.begin(), filteredTracks.end(), compare);
}


void LibraryView::timerCallback()
{
    if (m_hoveredRow < 0)
    {
        if (m_hoverPreviewActive && onHoverPreviewStop)
        {
            onHoverPreviewStop();
            m_hoverPreviewActive = false;
        }
        stopTimer();
        return;
    }

    double now = juce::Time::getMillisecondCounterHiRes();
    if (!m_hoverPreviewActive && (now - m_hoverStartTime >= kHoverDelayMs))
    {
        if (m_hoveredRow >= 0 && m_hoveredRow < static_cast<int>(m_model->filteredTracks.size()))
        {
            auto& t = m_model->filteredTracks[static_cast<size_t>(m_hoveredRow)];
            if (onHoverPreview && t.filePath.isNotEmpty())
            {
                onHoverPreview(t.filePath, t.title, t.artist);
                m_hoverPreviewActive = true;
            }
        }
    }
}


void LibraryView::applyAllFilters()
{
    if (m_provider)
    {
        float bpmMin = static_cast<float>(m_bpmMinSlider->getValue());
        float bpmMax = static_cast<float>(m_bpmMaxSlider->getValue());

        std::string keyFilter;
        if (m_keyFilterCombo->getSelectedId() > 1)
            keyFilter = m_keyFilterCombo->getText().toStdString();

        std::string genreFilter;
        if (m_genreFilterCombo->getSelectedId() > 1)
        {
            juce::String genreText = m_genreFilterCombo->getText();
            int parenIdx = genreText.lastIndexOf(" (");
            if (parenIdx > 0)
                genreText = genreText.substring(0, parenIdx);
            genreFilter = genreText.toStdString();
        }

        std::string artistFilter;
        if (m_artistFilterCombo && m_artistFilterCombo->getSelectedId() > 1)
        {
            juce::String artistText = m_artistFilterCombo->getText();
            int parenIdx = artistText.lastIndexOf(" (");
            if (parenIdx > 0)
                artistText = artistText.substring(0, parenIdx);
            artistFilter = artistText.toStdString();
        }

        float energyMin = static_cast<float>(m_energyMinSlider->getValue());
        float energyMax = static_cast<float>(m_energyMaxSlider->getValue());

        int ratingMin = 0;
        int ratingId = m_ratingFilterCombo->getSelectedId();
        if (ratingId > 1)
            ratingMin = ratingId - 1;

        juce::String searchText = m_searchEdit->getText();

        std::vector<Models::Track> dbTracks;

        bool hasFilter = (bpmMin > 60 || bpmMax < 200 || !keyFilter.empty() ||
                         !genreFilter.empty() || !artistFilter.empty() ||
                         energyMin > 1 || energyMax < 10 || ratingMin > 0);
        bool hasSearch = searchText.isNotEmpty();

        if (hasFilter || hasSearch)
        {
            dbTracks = m_provider->getTracksByFilterWithSearch(
                hasSearch ? searchText.toStdString() : "",
                bpmMin, bpmMax, keyFilter, genreFilter, artistFilter,
                energyMin, energyMax, ratingMin);
        }
        else
        {
            dbTracks = m_provider->getAllTracks();
        }

        const auto cueCounts = m_provider->getCueCounts();

        m_model->filteredTracks.clear();
        for (auto& t : dbTracks)
        {

            juce::String durStr;
            if (t.duration > 0.0) {
                int mins = static_cast<int>(t.duration / 60.0);
                int secs = static_cast<int>(t.duration) % 60;
                durStr = juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
            } else {
                durStr = "-";
            }
            juce::String keyStr = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
            juce::String bpmStr = (t.bpm > 0.0) ? juce::String(t.bpm, 0) : "-";

            int cueCount = 0;
            {
                auto itc = cueCounts.find(t.id);
                if (itc != cueCounts.end()) cueCount = itc->second;
            }

            TrackTableModel::Track ft;
            ft.title = t.title.empty() ? juce::File(t.filePath).getFileNameWithoutExtension() : juce::String(t.title);
            ft.artist = juce::String(t.artist);
            ft.album = juce::String(t.album);
            ft.bpm = bpmStr;
            ft.key = keyStr;
            ft.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
            ft.energy = t.energy > 0 ? juce::String(static_cast<int>(t.energy)) : juce::String("-");
            ft.duration = durStr;
            ft.genre = juce::String(t.genre);
            ft.rating = juce::String(t.rating);
            ft.filePath = juce::String(t.filePath);
            ft.year = t.year > 0 ? juce::String(t.year) : "-";
            {
                juce::File trackFile(juce::String(t.filePath));
                juce::File stemDir = trackFile.getParentDirectory().getChildFile("StemsCache")
                                        .getChildFile(trackFile.getFileNameWithoutExtension());
                if (stemDir.isDirectory() && stemDir.getNumberOfChildFiles(juce::File::findFiles) > 0)
                    ft.stems = "Yes";
                else
                    ft.stems = "?";  // unknown / not yet processed
            }
            ft.cueCount = cueCount;
            ft.playCount = t.playCount;
            ft.trackId = t.id;
            ft.analyzed = t.analyzed;
            ft.color = juce::String(t.color);
            ft.bitrate = t.bitRate > 0 ? (juce::String(t.bitRate) + " kbps") : juce::String("-");
            ft.lastPlayed = (t.lastPlayed > 0)
                ? juce::Time(t.lastPlayed * 1000LL).formatted("%Y-%m-%d")
                : juce::String("-");
            ft.label = juce::String(t.label);
            ft.lufs = t.lufs != 0.0f ? juce::String(t.lufs, 1) : juce::String("-");
            ft.mood = juce::String(t.mood);
            ft.danceability = t.danceability > 0.0f
                ? juce::String(static_cast<int>(t.danceability * 100.0f + 0.5f)) + "%"
                : juce::String("-");
            ft.comment = juce::String(t.comment);
            ft.dateAdded = (t.dateAdded > 0)
                ? juce::Time(t.dateAdded * 1000LL).formatted("%Y-%m-%d")
                : juce::String("-");
            switch (t.source) {
                case Models::TrackSource::Rekordbox: ft.sourceBadge = "R"; break;
                case Models::TrackSource::Serato:    ft.sourceBadge = "S"; break;
                case Models::TrackSource::Traktor:   ft.sourceBadge = "T"; break;
                case Models::TrackSource::VirtualDJ: ft.sourceBadge = "V"; break;
                case Models::TrackSource::EngineDJ:  ft.sourceBadge = "E"; break;
                default:                             ft.sourceBadge = "B"; break;
            }
            m_model->filteredTracks.push_back(ft);
        }

        m_facetBase = m_model->filteredTracks;   // base pour les clics de chips
        applyFacetsAndRebuildChips();

        m_model->sortCol = m_sortColumn;
        m_model->sortAsc = m_sortAscending;
        m_model->sortTracks();

        m_trackList->updateContent();
        m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix"),
                                   juce::dontSendNotification);
    }
    else
    {
        m_model->filterText = m_searchEdit->getText();
        m_model->applyFilter();
        m_facetBase = m_model->filteredTracks;
        applyFacetsAndRebuildChips();
        m_model->sortTracks();
        m_trackList->updateContent();
        m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix"),
                                   juce::dontSendNotification);
    }
}


void LibraryView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    g.setColour(Colors::bgSurface());
    g.fillRect(0, 0, getWidth(), kSearchBarHeight);
    g.setColour(Colors::border());
    g.drawHorizontalLine(kSearchBarHeight - 1, 0.0f, static_cast<float>(getWidth()));

    m_searchFocused = m_searchEdit->hasKeyboardFocus(true);
    if (m_searchFocused)
    {
        auto sb = m_searchEdit->getBounds().toFloat();
        g.setColour(Colors::primary().withAlpha(0.12f));
        g.fillRoundedRectangle(sb.expanded(4.0f, 4.0f), 6.0f);
        g.setColour(Colors::primary().withAlpha(0.08f));
        g.fillRoundedRectangle(sb.expanded(8.0f, 8.0f), 8.0f);
        g.setColour(Colors::primary().withAlpha(0.6f));
        g.drawRoundedRectangle(sb.expanded(1.0f), 4.0f, 1.5f);
    }

    g.setColour(Colors::bgCard());
    g.fillRect(0, kSearchBarHeight, getWidth(), kFilterBarHeight);
    g.setColour(Colors::border());
    g.drawHorizontalLine(kSearchBarHeight + kFilterBarHeight - 1, 0.0f, static_cast<float>(getWidth()));

    const int kTreeWidth = 260;
    int headerY = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight;
    g.setColour(Colors::bgCard().brighter(0.05f));
    g.fillRect(kTreeWidth, headerY, getWidth() - kTreeWidth, kColumnHeaderHeight);

    {
        float ulY = static_cast<float>(headerY + kColumnHeaderHeight - 2);
        juce::ColourGradient headerUnderline(Colors::primary().withAlpha(0.4f), (float)kTreeWidth, ulY,
                                             Colors::secondary().withAlpha(0.1f), static_cast<float>(getWidth()), ulY, false);
        g.setGradientFill(headerUnderline);
        g.fillRect((float)kTreeWidth, ulY, static_cast<float>(getWidth() - kTreeWidth), 2.0f);
    }

    g.setFont(juce::Font(juce::FontOptions("Segoe UI", 11.0f, juce::Font::bold)));
    // Follow the table's horizontal scroll so headers stay aligned with rows,
    const int headerViewX = m_trackList ? m_trackList->getViewport()->getViewPositionX() : 0;
    juce::Graphics::ScopedSaveState headerClip(g);
    g.reduceClipRegion(kTreeWidth, headerY, getWidth() - kTreeWidth, kColumnHeaderHeight);
    int colX = kTreeWidth + kCheckGutter - headerViewX;

    {
        const int total = m_model ? (int) m_model->filteredTracks.size() : 0;
        const int nb = m_model ? (int) m_model->checkedIds.size() : 0;
        const bool all = total > 0 && nb >= total;
        juce::Rectangle<float> box((float) (kTreeWidth + 6 - headerViewX),
                                   (float) headerY + kColumnHeaderHeight * 0.5f - 7.0f, 14.0f, 14.0f);
        g.setColour(nb > 0 ? Colors::primary() : Colors::bgLighter());
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(nb > 0 ? Colors::primary().brighter(0.3f) : Colors::border());
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        if (nb > 0)
        {
            g.setColour(juce::Colours::white);
            if (all)
            {
                juce::Path tick;
                tick.startNewSubPath(box.getX() + 3.2f, box.getCentreY());
                tick.lineTo(box.getX() + 5.8f, box.getBottom() - 3.8f);
                tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
                g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
            else
            {
                g.fillRect(box.getX() + 3.0f, box.getCentreY() - 1.0f, box.getWidth() - 6.0f, 2.0f);
            }
        }
    }


    // Column header i18n keys (parallel to kColNames).
    static const char* kColKeys[kNumColumns] = {
        "library.col.wave", "library.col.art", "library.col.title", "library.col.artist",
        "library.col.album", "library.col.bpm", "library.col.key", "library.col.camelot",
        "library.col.energy", "library.col.duration", "library.col.genre", "library.col.rating",
        "library.col.colorTag", "library.col.cues", "library.col.stems", "library.col.year",
        "library.col.plays",
        "library.col.lufs", "library.col.mood", "library.col.danceability", "library.col.label",
        "library.col.comment", "library.col.bitrate", "library.col.lastPlayed", "library.col.dateAdded"
    };

    juce::Colour colAccents[kNumColumns] = {
        Colors::textMuted(),     // WAVE
        Colors::textMuted(),     // ART
        Colors::textPrimary(),   // TITRE
        Colors::textSecondary(), // ARTISTE
        Colors::textMuted(),     // ALBUM
        Colors::bpmBadge(),      // BPM
        Colors::keyBadge(),      // KEY
        Colors::keyBadge(),      // CAMELOT
        Colors::energyBadge(),   // ENERGY
        Colors::textMuted(),     // DUREE
        Colors::textSecondary(), // GENRE
        Colors::starFilled(),    // NOTE
        Colors::textMuted(),     // COL
        Colors::success(),       // CUES
        Colors::success(),       // STEMS
        Colors::textMuted(),     // ANNEE
        Colors::textMuted(),     // PLAYS
        Colors::warning(),       // LUFS
        Colors::secondary(),     // MOOD
        Colors::accent(),        // DANSE
        Colors::textSecondary(), // LABEL
        Colors::textMuted(),     // COMMENT
        Colors::textMuted(),     // KBPS
        Colors::textMuted(),     // JOUE LE
        Colors::textMuted()      // AJOUTE
    };

    for (int i = 0; i < kNumColumns; ++i)
    {
        if (! kColVisible[i]) continue;

        bool isSorted = (static_cast<int>(m_sortColumn) == i);
        if (isSorted)
        {
            g.setColour(Colors::primary().withAlpha(0.1f));
            g.fillRect(colX - 4, headerY, kColWidths[i], kColumnHeaderHeight);
        }

        g.setColour(isSorted ? Colors::primary() : Colors::textSecondary());
        g.drawText(BM_TJ(kColKeys[i]), colX, headerY + 4, kColWidths[i] - 16, 18,
                   juce::Justification::centredLeft);

        if (i == 5 || i == 6 || i == 7 || i == 8)  // BPM, KEY, CAMELOT, ENERGY
        {
            float dotX = static_cast<float>(colX) - 1.0f;
            float dotY = static_cast<float>(headerY) + 4.0f;
            g.setColour(colAccents[i].withAlpha(0.7f));
            g.fillEllipse(dotX, dotY, 4.0f, 4.0f);
        }

        if (isSorted)
        {
            drawSortArrow(g, colX, headerY + 4, kColWidths[i], 18, m_sortAscending);
        }

        colX += kColWidths[i];

        g.setColour(Colors::border().withAlpha(m_resizingCol == i ? 0.9f : 0.35f));
        g.fillRect(colX - 1, headerY + 5, 1, kColumnHeaderHeight - 10);
    }

    if (m_hoveredRow >= 0)
    {
        int totalHeaderH = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight + kColumnHeaderHeight;
        int rowY = totalHeaderH + m_hoveredRow * m_trackList->getRowHeight()
                 - static_cast<int>(m_trackList->getViewport()->getViewPositionY());
        if (rowY >= totalHeaderH && rowY < getHeight())
        {
            g.setColour(Colors::primary().withAlpha(0.8f));
            float py = static_cast<float>(rowY + 8);
            float px = 266.0f;
            juce::Path playIcon;
            playIcon.addTriangle(px, py, px, py + 12.0f, px + 10.0f, py + 6.0f);
            g.fillPath(playIcon);
        }
    }

    if (m_model != nullptr && m_model->filteredTracks.empty() && m_trackList != nullptr)
    {
        ProDraw::emptyState(g, m_trackList->getBounds(),
                            juce::CharPointer_UTF8("\xe2\x99\xaa"),
                            BM_TJ("library.list.emptyTitle"),
                            BM_TJ("library.list.emptyBody"),
                            Colors::primary());
    }
}


void LibraryView::resized()
{
    auto area = getLocalBounds();

    int x = 16;
    int y = 12;
    int h = 28;

    m_titleLabel->setBounds(x, y - 2, 120, h);
    m_titleLabel->setFont(juce::Font("Segoe UI", 16.0f, juce::Font::bold));
    x += 130;

    m_searchEdit->setBounds(x, y, 250, h);
    x += 254;

    m_clearSearchBtn->setBounds(x, y, 28, h);
    x += 36;

    m_trackCountLabel->setBounds(x, y, 120, h);
    x += 124;

    if (m_tagEditor && m_tagEditorVisible)
        m_tagEditor->setBounds(getLocalBounds().withTrimmedTop(kSearchBarHeight));

    int fx = 16;
    int fy = kSearchBarHeight + 8;
    int fh = 28;

    m_filterSectionLabel->setBounds(fx, fy, 50, fh);
    fx += 52;

    m_bpmRangeLabel->setBounds(fx, fy, 80, fh);
    fx += 82;
    m_bpmMinSlider->setBounds(fx, fy + 4, 55, fh - 8);
    fx += 57;
    m_bpmMaxSlider->setBounds(fx, fy + 4, 55, fh - 8);
    fx += 62;

    m_keyFilterCombo->setBounds(fx, fy, 70, fh);
    fx += 76;

    m_genreFilterCombo->setBounds(fx, fy, 110, fh);
    fx += 116;

    if (m_artistFilterCombo)
    {
        m_artistFilterCombo->setBounds(fx, fy, 120, fh);
        fx += 126;
    }

    m_energyRangeLabel->setBounds(fx, fy, 50, fh);
    fx += 52;
    m_energyMinSlider->setBounds(fx, fy + 4, 40, fh - 8);
    fx += 42;
    m_energyMaxSlider->setBounds(fx, fy + 4, 40, fh - 8);
    fx += 46;

    m_ratingFilterCombo->setBounds(fx, fy, 70, fh);
    fx += 76;

    m_resetFiltersBtn->setBounds(fx, fy, 130, fh);
    fx += 142;

    if (m_detailToggleBtn)
        m_detailToggleBtn->setBounds(getWidth() - 96, fy, 80, fh);

    // Barre d'actions : bascule automatique sur la 2e ligne (espace libre des
    // filtres) quand la 1re deborde, largeurs reduites en dernier recours.
    {
        struct Act { juce::Component* c; int w; };
        std::vector<Act> acts;
        auto add = [&acts](juce::Component* c, int w) { if (c != nullptr) acts.push_back({ c, w }); };
        add(m_importRekordboxXmlBtn.get(), 190);
        add(m_syncDjPlaylistsBtn.get(), 170);
        add(m_relinkBtn.get(), 150);
        add(m_browseFolderBtn.get(), 140);
        add(m_viewsBtn.get(), 82);
        add(m_tagEditorBtn.get(), 130);
        add(m_selectAllTableBtn.get(), 128);
        add(m_integrityBtn.get(), 160);
        if (m_repairCorruptBtn && m_repairCorruptBtn->isVisible())
            add(m_repairCorruptBtn.get(), 170);
        add(m_duplicatesBtn.get(), 130);

        const int row1Limit = getWidth() - 16;
        const int row2Limit = getWidth() - 108;

        size_t firstWrapped = acts.size();
        int cx = x;
        for (size_t i = 0; i < acts.size(); ++i)
        {
            if (cx + acts[i].w > row1Limit) { firstWrapped = i; break; }
            acts[i].c->setBounds(cx, y, acts[i].w, h);
            cx += acts[i].w + 6;
        }

        if (firstWrapped < acts.size())
        {
            int needed = 0;
            for (size_t i = firstWrapped; i < acts.size(); ++i) needed += acts[i].w + 6;
            const int avail = juce::jmax(120, row2Limit - fx);
            const double scale = needed > avail ? (double) avail / (double) needed : 1.0;

            int wx = fx;
            for (size_t i = firstWrapped; i < acts.size(); ++i)
            {
                const int w = juce::jmax(64, (int) (acts[i].w * scale));
                acts[i].c->setBounds(wx, fy, w, fh);
                wx += w + 6;
            }
        }
    }

    if (m_facetBar)
        m_facetBar->setBounds(0, kSearchBarHeight + kFilterBarHeight, getWidth(), kFacetBarHeight);

    int totalHeaderH = kSearchBarHeight + kFilterBarHeight + kFacetBarHeight + kColumnHeaderHeight;
    auto content = area;
    content.removeFromTop(totalHeaderH);

    const int kTreeWidth = 260;
    auto leftPane = content.removeFromLeft(kTreeWidth);

    // BrowserPanel kept as legacy listener holder, never shown.
    if (m_browserPanel) m_browserPanel->setBounds(0, 0, 0, 0);

    if (m_collectionTree)
    {
        // Include the column-header strip height above so the tree header lines
        auto treeBounds = leftPane.reduced(4, 4);
        m_collectionTree->setBounds(treeBounds);
    }

    const bool detailShown = m_detailVisible && getWidth() >= 1080;
    if (m_detailPanel)
    {
        m_detailPanel->setVisible(detailShown);
        if (detailShown)
            m_detailPanel->setBounds(content.removeFromRight(getWidth() >= 1400 ? 348 : 320).reduced(4));
    }

    m_trackList->setBounds(content);
}


void LibraryView::loadFromDatabase()
{
    if (!m_provider) return;

    auto dbTracks = m_provider->getAllTracks();
    m_model->tracks.clear();
    for (auto& t : dbTracks)
    {
        juce::String durStr;
        if (t.duration > 0.0) {
            int mins = static_cast<int>(t.duration / 60.0);
            int secs = static_cast<int>(t.duration) % 60;
            durStr = juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
        } else {
            durStr = "-";
        }
        juce::String keyStr = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
        juce::String bpmStr = (t.bpm > 0.0) ? juce::String(t.bpm, 0) : "-";

        // OPTIMISATION : pas de getCuePoints() par track (trop lent sur 5000 tracks)
        int cueCount = 0;

        TrackTableModel::Track trk;
        trk.title = t.title.empty() ? juce::File(t.filePath).getFileNameWithoutExtension() : juce::String(t.title);
        trk.artist = juce::String(t.artist);
        trk.album = juce::String(t.album);
        trk.bpm = bpmStr;
        trk.key = keyStr;
        trk.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
        trk.energy = t.energy > 0 ? juce::String(static_cast<int>(t.energy)) : juce::String("-");
        trk.duration = durStr;
        trk.genre = juce::String(t.genre);
        trk.rating = juce::String(t.rating);
        trk.filePath = juce::String(t.filePath);
        trk.year = t.year > 0 ? juce::String(t.year) : "-";

        // OPTIMISATION : pas de check StemsCache disque par track (5000 acces fichiers = crash)
        trk.stems = "?";

        trk.cueCount = cueCount;
        trk.playCount = t.playCount;
        trk.trackId = t.id;
        trk.analyzed = t.analyzed;
        trk.color = juce::String(t.color);
        trk.label = juce::String(t.label);
        trk.bitrate = t.bitRate > 0 ? (juce::String(t.bitRate) + " kbps") : juce::String("-");
        if (t.lastPlayed > 0)
            trk.lastPlayed = juce::Time(t.lastPlayed * 1000LL).formatted("%Y-%m-%d");
        else
            trk.lastPlayed = "-";
        switch (t.source) {
            case Models::TrackSource::Rekordbox: trk.sourceBadge = "R"; break;
            case Models::TrackSource::Serato:    trk.sourceBadge = "S"; break;
            case Models::TrackSource::Traktor:   trk.sourceBadge = "T"; break;
            case Models::TrackSource::VirtualDJ: trk.sourceBadge = "V"; break;
            case Models::TrackSource::EngineDJ:  trk.sourceBadge = "E"; break;
            default:                             trk.sourceBadge = "B"; break;
        }
        m_model->tracks.push_back(trk);
    }

    applyAllFilters();
}


void LibraryView::textEditorTextChanged(juce::TextEditor& editor)
{
    if (&editor == m_searchEdit.get())
    {
        applyAllFilters();
    }
}

void LibraryView::filterChanged(const juce::String& category, const juce::String& value)
{
    spdlog::info("BrowserPanel filter: {} = {}", category.toStdString(), value.toStdString());

    if (category == "Genres")
    {
        for (int i = 1; i <= m_genreFilterCombo->getNumItems(); ++i)
        {
            int itemId = m_genreFilterCombo->getItemId(i - 1);
            juce::String itemText = m_genreFilterCombo->getItemText(i - 1);
            if (itemText.startsWith(value))
            {
                m_genreFilterCombo->setSelectedId(itemId, juce::sendNotificationSync);
                return;
            }
        }
        m_searchEdit->setText(value, true);
    }
    else if (category == "BPM")
    {
        if (value.startsWith("< "))
        {
            m_bpmMinSlider->setValue(60, juce::dontSendNotification);
            m_bpmMaxSlider->setValue(value.substring(2).getIntValue(), juce::dontSendNotification);
        }
        else if (value.startsWith("> "))
        {
            m_bpmMinSlider->setValue(value.substring(2).getIntValue(), juce::dontSendNotification);
            m_bpmMaxSlider->setValue(200, juce::dontSendNotification);
        }
        else if (value.contains("-"))
        {
            auto parts = juce::StringArray::fromTokens(value, "-", "");
            if (parts.size() == 2)
            {
                m_bpmMinSlider->setValue(parts[0].getIntValue(), juce::dontSendNotification);
                m_bpmMaxSlider->setValue(parts[1].getIntValue(), juce::dontSendNotification);
            }
        }
        m_bpmRangeLabel->setText(juce::String((int)m_bpmMinSlider->getValue()) + "-" +
                                 juce::String((int)m_bpmMaxSlider->getValue()) + " BPM",
                                 juce::dontSendNotification);
        applyAllFilters();
    }
    else if (category == "Artistes")
    {
        m_searchEdit->setText(value, true);
    }
    else if (category == "Playlists")
    {
        m_searchEdit->setText(value, true);
    }
    else if (category == "Sources DJ")
    {
        if (!m_provider) return;

        Models::TrackSource source = Models::TrackSource::Local;
        if (value == "Rekordbox") source = Models::TrackSource::Rekordbox;
        else if (value == "VirtualDJ") source = Models::TrackSource::VirtualDJ;
        else if (value == "Serato") source = Models::TrackSource::Serato;
        else if (value == "Traktor") source = Models::TrackSource::Traktor;
        else if (value == "Engine DJ") source = Models::TrackSource::EngineDJ;

        auto tracks = m_provider->getTracksBySource(source);
        const auto cueCountsBySource = m_provider->getCueCounts();

        m_model->filteredTracks.clear();
        for (const auto& t : tracks)
        {
            auto bpmStr = t.bpm > 0 ? juce::String(t.bpm, 1) : juce::String("-");
            auto keyStr = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
            int minutes = static_cast<int>(t.duration) / 60;
            int seconds = static_cast<int>(t.duration) % 60;
            auto durStr = juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2);

            int cueCount = 0;
            {
                auto itc = cueCountsBySource.find(t.id);
                if (itc != cueCountsBySource.end()) cueCount = itc->second;
            }

            TrackTableModel::Track ft;
            ft.title = t.title.empty() ? juce::File(t.filePath).getFileNameWithoutExtension() : juce::String(t.title);
            ft.artist = juce::String(t.artist);
            ft.album = juce::String(t.album);
            ft.bpm = bpmStr;
            ft.key = keyStr;
            ft.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
            ft.energy = t.energy > 0 ? juce::String(static_cast<int>(t.energy)) : juce::String("-");
            ft.duration = durStr;
            ft.genre = juce::String(t.genre);
            ft.rating = juce::String(t.rating);
            ft.filePath = juce::String(t.filePath);
            ft.year = t.year > 0 ? juce::String(t.year) : "-";
            {
                juce::File trackFile(juce::String(t.filePath));
                juce::File stemDir = trackFile.getParentDirectory().getChildFile("StemsCache")
                                        .getChildFile(trackFile.getFileNameWithoutExtension());
                if (stemDir.isDirectory() && stemDir.getNumberOfChildFiles(juce::File::findFiles) > 0)
                    ft.stems = "Yes";
                else
                    ft.stems = "?";
            }
            ft.cueCount = cueCount;

            m_model->filteredTracks.push_back(ft);
        }

        m_model->sortCol = SortColumn::Title;
        m_trackList->updateContent();
        m_trackList->repaint();
        m_trackCountLabel->setText(juce::String(static_cast<int>(m_model->filteredTracks.size())) +
                                   BM_TJ("library.tracksSuffix") + " (" + value + ")", juce::dontSendNotification);
        return;
    }
    else if (category == "Dossier")
    {
        juce::File folder(value);
        if (!folder.isDirectory())
            return;

        m_genreFilterCombo->setSelectedId(1, juce::dontSendNotification);
        m_bpmMinSlider->setValue(60, juce::dontSendNotification);
        m_bpmMaxSlider->setValue(200, juce::dontSendNotification);
        m_bpmRangeLabel->setText("60-200 BPM", juce::dontSendNotification);
        m_keyFilterCombo->setSelectedId(1, juce::dontSendNotification);
        m_energyMinSlider->setValue(1, juce::dontSendNotification);
        m_energyMaxSlider->setValue(10, juce::dontSendNotification);
        m_energyRangeLabel->setText("E:1-10", juce::dontSendNotification);
        m_ratingFilterCombo->setSelectedId(1, juce::dontSendNotification);
        m_searchEdit->setText("", false);

        if (m_provider)
        {
            auto allTracks = m_provider->getAllTracks();
            m_model->filteredTracks.clear();

            juce::String folderPath = folder.getFullPathName();
            folderPath = folderPath.replace("\\", "/");
            if (!folderPath.endsWithChar('/'))
                folderPath += "/";

            for (auto& t : allTracks)
            {
                juce::String trackPath = juce::String(t.filePath).replace("\\", "/");
                if (trackPath.startsWith(folderPath))
                {
                    juce::String remainder = trackPath.substring(folderPath.length());
                    if (!remainder.contains("/"))
                    {
                        juce::String durStr;
                        if (t.duration > 0.0) {
                            int mins = static_cast<int>(t.duration / 60.0);
                            int secs = static_cast<int>(t.duration) % 60;
                            durStr = juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
                        } else {
                            durStr = "-";
                        }
                        juce::String keyStr = t.camelotKey.empty() ? juce::String(t.key) : juce::String(t.camelotKey);
                        juce::String bpmStr = (t.bpm > 0.0) ? juce::String(t.bpm, 0) : "-";

                        int cueCount = 0;
                        {
                            static thread_local std::map<int64_t, int> folderCueCounts;
                            static thread_local juce::uint32 folderCueStamp = 0;
                            const auto now = juce::Time::getMillisecondCounter();
                            if (folderCueCounts.empty() || now - folderCueStamp > 5000)
                            {
                                folderCueCounts = m_provider->getCueCounts();
                                folderCueStamp = now;
                            }
                            auto itc = folderCueCounts.find(t.id);
                            if (itc != folderCueCounts.end()) cueCount = itc->second;
                        }

                        TrackTableModel::Track ft;
                        ft.title = t.title.empty() ? juce::File(t.filePath).getFileNameWithoutExtension() : juce::String(t.title);
                        ft.artist = juce::String(t.artist);
                        ft.album = juce::String(t.album);
                        ft.bpm = bpmStr;
                        ft.key = keyStr;
                        ft.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
                        ft.energy = t.energy > 0 ? juce::String(static_cast<int>(t.energy)) : juce::String("-");
                        ft.duration = durStr;
                        ft.genre = juce::String(t.genre);
                        ft.rating = juce::String(t.rating);
                        ft.filePath = juce::String(t.filePath);
                        ft.year = t.year > 0 ? juce::String(t.year) : "-";
                        {
                            juce::File trackFile(juce::String(t.filePath));
                            juce::File stemDir = trackFile.getParentDirectory().getChildFile("StemsCache")
                                                    .getChildFile(trackFile.getFileNameWithoutExtension());
                            if (stemDir.isDirectory() && stemDir.getNumberOfChildFiles(juce::File::findFiles) > 0)
                                ft.stems = "Yes";
                            else
                                ft.stems = "?";
                        }
                        ft.cueCount = cueCount;

                        m_model->filteredTracks.push_back(ft);
                    }
                }
            }

            m_model->sortTracks();
            m_trackList->updateContent();
            m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + " " + BM_TJ("analysis.stats.tracks"),
                                       juce::dontSendNotification);
        }
    }
}

void LibraryView::visibilityChanged()
{
    if (! isVisible() || ! m_needsInitialLoad || ! m_provider) return;

    juce::Component::SafePointer<LibraryView> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe == nullptr || ! safe->isVisible() || ! safe->m_needsInitialLoad) return;
        safe->m_needsInitialLoad = false;
        const auto t0 = juce::Time::getMillisecondCounter();
        safe->loadFromDatabase();
        spdlog::info("[Library] chargement initial en {} ms",
                     juce::Time::getMillisecondCounter() - t0);
    });
}

void LibraryView::refreshLibrary()
{
    // Ré-évalue la santé des fichiers (manquants / corrompus) au prochain
    s_fileHealthCache.clear();
    facetIntegrityChecker().reload();

    if (m_provider)
    {
        m_needsInitialLoad = false;
        m_provider->invalidateAllTracksCache();
        if (m_model) m_model->artCache.clear();
        loadFromDatabase();
    }
    else
    {
        m_model->applyFilter();
        m_model->sortTracks();
        m_trackList->updateContent();
        m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix"),
                                   juce::dontSendNotification);
    }
}

void LibraryView::setSearchText(const juce::String& text)
{
    m_searchEdit->setText(text);
}


void LibraryView::TrackTableModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(filteredTracks.size())) return;

    bool isHovered = (row == hoveredRow);

    if (selected)
    {
        juce::ColourGradient selGrad(Colors::primary().withAlpha(0.28f), 0.0f, 0.0f,
                                     Colors::primary().withAlpha(0.14f), static_cast<float>(w), 0.0f, false);
        g.setGradientFill(selGrad);
        g.fillRect(0, 0, w, h);

        g.setColour(Colors::primary());
        g.fillRect(0, 0, 3, h);

        g.setColour(Colors::primary().withAlpha(0.35f));
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(w));
    }
    else if (isHovered)
    {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary().withAlpha(0.5f));
        g.fillRect(0, 0, 2, h);
    }
    else
    {
        g.fillAll(Colors::bg());
        if (row % 2 == 1)
        {
            g.setColour(juce::Colours::white.withAlpha(0.022f));
            g.fillRect(0, 0, w, h);
        }
    }

    auto& t = filteredTracks[static_cast<size_t>(row)];

    juce::String cols[] = {
        "", "",  // WAVE, ART (rendered separately)
        t.title, t.artist, t.album, t.bpm, t.key, t.camelot, t.energy,
        t.duration, t.genre, t.rating, "",  // COL (rendered separately)
        "", "", t.year,
        juce::String(t.playCount),
        t.lufs, t.mood, t.danceability, t.label, t.comment,
        t.bitrate, t.lastPlayed, t.dateAdded
    };
    juce::Colour colColors[] = {
        Colors::textMuted(),     // WAVE
        Colors::textMuted(),     // ART
        Colors::textPrimary(),   // Title
        Colors::textSecondary(), // Artist
        Colors::textMuted(),     // Album
        Colors::primary(),       // BPM
        Colors::accent(),        // Key
        Colors::accent(),        // Camelot
        Colors::warning(),       // Energy
        Colors::textMuted(),     // Duration
        Colors::textSecondary(), // Genre
        Colors::starFilled(),    // Rating
        Colors::textMuted(),     // Color
        Colors::success(),       // Cues (handled separately)
        Colors::success(),       // Stems (handled separately)
        Colors::textMuted(),     // Year
        Colors::textMuted(),     // Plays
        Colors::warning(),       // LUFS
        Colors::secondary(),     // Mood
        Colors::accent(),        // Danse
        Colors::textSecondary(), // Label
        Colors::textMuted(),     // Comment
        Colors::textMuted(),     // Kbps
        Colors::textMuted(),     // Last played
        Colors::textMuted()      // Date added
    };

    {
        const bool on = isChecked(t.trackId);
        juce::Rectangle<float> box(6.0f, h * 0.5f - 7.0f, 14.0f, 14.0f);
        g.setColour(on ? Colors::primary() : Colors::bgLighter());
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(on ? Colors::primary().brighter(0.3f) : Colors::border());
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath(box.getX() + 3.2f, box.getCentreY());
            tick.lineTo(box.getX() + 5.8f, box.getBottom() - 3.8f);
            tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
            g.setColour(juce::Colours::white);
            g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    int x = kCheckGutter;
    for (int i = 0; i < kNumColumns; ++i) {
        if (! kColVisible[i]) continue;
        if (i == 0) { // WAVE column - real 3-band mini waveform from the analysis cache
            if (t.trackId > 0) {
                auto it = waveCache.find(t.trackId);
                if (it == waveCache.end()) {
                    if (onWaveformNeeded)
                        onWaveformNeeded(t.trackId);
                } else if (!it->second.exists
                           && juce::Time::getMillisecondCounter() - it->second.stampMs > 20000) {
                    waveCache.erase(it);
                    if (onWaveformNeeded)
                        onWaveformNeeded(t.trackId);
                } else if (it->second.exists) {
                    const auto& wv = it->second;
                    const int n = static_cast<int>(wv.low.size());
                    if (n > 0) {
                        const float cy = static_cast<float>(h) * 0.5f;
                        const float areaW = static_cast<float>(kColWidths[i]) - 10.0f;
                        const float bw = areaW / static_cast<float>(n);
                        const float maxAmp = static_cast<float>(h) * 0.5f - 3.0f;
                        auto band = [&](const std::vector<float>& d, juce::Colour c, float alpha) {
                            g.setColour(c.withAlpha(alpha));
                            for (int k = 0; k < n && k < static_cast<int>(d.size()); ++k) {
                                const float v = juce::jlimit(0.0f, 1.0f, d[static_cast<size_t>(k)]);
                                const float bh2 = juce::jmax(0.8f, v * maxAmp);
                                g.fillRect(static_cast<float>(x) + static_cast<float>(k) * bw,
                                           cy - bh2, juce::jmax(1.0f, bw - 0.6f), bh2 * 2.0f);
                            }
                        };
                        band(wv.low,  Colors::waveformBass(),   0.75f);
                        band(wv.mid,  Colors::waveformMid(),    0.65f);
                        band(wv.high, Colors::waveformTreble(), 0.6f);
                    }
                }
            }
            x += kColWidths[i];
            continue;
        }

        if (i == 1) { // ART column - real album cover (Lexicon style), source badge as fallback
            juce::Colour badgeBg = juce::Colours::grey;
            if      (t.sourceBadge == "R") badgeBg = juce::Colour(0xFFE25822);
            else if (t.sourceBadge == "S") badgeBg = juce::Colour(0xFF2EB5FF);
            else if (t.sourceBadge == "T") badgeBg = juce::Colour(0xFF66BB6A);
            else if (t.sourceBadge == "V") badgeBg = juce::Colour(0xFFE91E63);
            else if (t.sourceBadge == "E") badgeBg = juce::Colour(0xFFFFCA28);
            else if (t.sourceBadge == "B") badgeBg = juce::Colour(0xFF7C4DFF);

            const float side = static_cast<float>(h) - 6.0f;
            const float ax = static_cast<float>(x) + 1.0f;
            const float ay = 3.0f;

            const RowArt* art = nullptr;
            if (t.trackId > 0) {
                auto it = artCache.find(t.trackId);
                if (it == artCache.end()) {
                    if (onArtNeeded) onArtNeeded(t.trackId, t.filePath);
                } else {
                    art = &it->second;
                }
            }

            if (art != nullptr && art->exists && art->img.isValid()) {
                juce::Rectangle<float> dst(ax, ay, side, side);
                juce::Path clip;
                clip.addRoundedRectangle(dst, 4.0f);
                juce::Graphics::ScopedSaveState save(g);
                g.reduceClipRegion(clip);
                g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
                g.drawImage(art->img, dst, juce::RectanglePlacement::fillDestination);
                g.setColour(juce::Colours::black.withAlpha(0.25f));
                g.drawRoundedRectangle(dst, 4.0f, 1.0f);
                if (t.sourceBadge.isNotEmpty()) {
                    const float dotR = 4.0f;
                    const float dcx2 = ax + side - dotR - 1.5f;
                    const float dcy2 = ay + side - dotR - 1.5f;
                    g.setColour(juce::Colours::black.withAlpha(0.55f));
                    g.fillEllipse(dcx2 - 1.0f, dcy2 - 1.0f, dotR * 2.0f + 2.0f, dotR * 2.0f + 2.0f);
                    g.setColour(badgeBg);
                    g.fillEllipse(dcx2, dcy2, dotR * 2.0f, dotR * 2.0f);
                }
            }
            else if (t.sourceBadge.isNotEmpty()) {
                juce::Rectangle<float> dst(ax, ay, side, side);
                g.setColour(badgeBg.withAlpha(0.16f));
                g.fillRoundedRectangle(dst, 4.0f);
                g.setColour(badgeBg.withAlpha(0.55f));
                g.drawRoundedRectangle(dst, 4.0f, 1.0f);
                g.setColour(badgeBg.brighter(0.4f));
                g.setFont(juce::Font(juce::FontOptions("Segoe UI", 12.0f, juce::Font::bold)));
                g.drawText(t.sourceBadge, dst.toNearestInt(), juce::Justification::centred);
            }
            x += kColWidths[i];
            continue;
        }

        if (i == 12) { // COLOR tag column - track colour dot
            if (t.color.isNotEmpty()) {
                juce::String hex = t.color.removeCharacters("#");
                if (hex.length() == 6) hex = "FF" + hex;
                const juce::Colour dot = juce::Colour::fromString(hex);
                if (!dot.isTransparent()) {
                    const float dr = 5.0f;
                    const float dcx = static_cast<float>(x) + dr;
                    const float dcy = static_cast<float>(h) * 0.5f;
                    g.setColour(dot.withAlpha(0.25f));
                    g.fillEllipse(dcx - dr - 2.0f, dcy - dr - 2.0f, (dr + 2.0f) * 2.0f, (dr + 2.0f) * 2.0f);
                    g.setColour(dot);
                    g.fillEllipse(dcx - dr, dcy - dr, dr * 2.0f, dr * 2.0f);
                }
            }
            x += kColWidths[i];
            continue;
        }

        if (i == 11) { // RATING column - star rendering
            int ratingColW = kColWidths[i];
            int ratingVal = t.rating.getIntValue();
            if (ratingVal < 0) ratingVal = 0;
            if (ratingVal > 5) ratingVal = 5;
            g.setFont(juce::Font("Segoe UI", 12.0f, juce::Font::plain));
            float starX = (float)x + 2.0f;
            for (int s = 0; s < 5; ++s)
            {
                if (s < ratingVal)
                {
                    g.setColour(selected ? Colors::textPrimary() : Colors::starFilled());
                    g.drawText(juce::String::charToString((juce::juce_wchar)0x2605), // filled star
                               (int)starX, 0, 11, h, juce::Justification::centredLeft, false);
                }
                else
                {
                    g.setColour(Colors::textMuted().withAlpha(0.3f));
                    g.drawText(juce::String::charToString((juce::juce_wchar)0x2606), // empty star
                               (int)starX, 0, 11, h, juce::Justification::centredLeft, false);
                }
                starX += 10.0f;
            }
            x += ratingColW;
            continue;
        }

        if (i == 13) { // CUES column - special rendering with dot
            int cueColW = kColWidths[i];
            float dotX = (float)x + 4.0f;
            float dotY = (float)(h / 2) - 4.0f;
            if (t.cueCount > 0) {
                g.setColour(Colors::success());
                g.fillEllipse(dotX, dotY, 8.0f, 8.0f);
                g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
                g.setColour(selected ? Colors::textPrimary() : Colors::success());
                g.drawText(juce::String(t.cueCount), (int)dotX + 10, 0, cueColW - 14, h,
                           juce::Justification::centredLeft, true);
            } else {
                g.setColour(Colors::textMuted().withAlpha(0.4f));
                g.drawEllipse(dotX, dotY, 8.0f, 8.0f, 1.0f);
            }
            x += cueColW;
            continue;
        }

        if (i == 14) { // STEMS column - green "Yes", orange "?", or grey "-"
            int stemsColW = kColWidths[i];
            g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
            if (t.stems == "Yes") {
                g.setColour(selected ? Colors::textPrimary() : Colors::success());
                g.drawText("Yes", x, 0, stemsColW - 4, h, juce::Justification::centredLeft, true);
            } else if (t.stems == "?") {
                g.setColour(selected ? Colors::textPrimary() : Colors::warning());
                g.drawText("?", x, 0, stemsColW - 4, h, juce::Justification::centredLeft, true);
            } else {
                g.setColour(Colors::textMuted().withAlpha(0.4f));
                g.drawText("-", x, 0, stemsColW - 4, h, juce::Justification::centredLeft, true);
            }
            x += stemsColW;
            continue;
        }

        if (i == 7 && t.camelot.isNotEmpty()) { // CAMELOT - pill badge
            g.setFont(juce::Font(juce::FontOptions("Segoe UI", 10.0f, juce::Font::bold)));
            float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), t.camelot);
            float bw = juce::jmin((float)kColWidths[i] - 6.0f, tw + 14.0f);
            float bh = 16.0f;
            float bx = (float)x;
            float by = (float)(h / 2) - bh * 0.5f;
            juce::Colour pill = AnalysisColumns::camelotColour(t.camelot);
            g.setColour(pill.withAlpha(selected ? 0.30f : 0.16f));
            g.fillRoundedRectangle(bx, by, bw, bh, bh * 0.5f);
            g.setColour(pill.withAlpha(selected ? 0.6f : 0.4f));
            g.drawRoundedRectangle(bx, by, bw, bh, bh * 0.5f, 0.8f);
            g.setColour(selected ? Colors::textPrimary() : pill);
            g.drawText(t.camelot, (int)bx, (int)by, (int)bw, (int)bh, juce::Justification::centred, false);
            x += kColWidths[i];
            continue;
        }

        if (i == 8) { // ENERGY - colored badge scaled green->red
            const int energyVal = t.energy.getIntValue();
            if (energyVal > 0) {
                const juce::Colour ec = AnalysisColumns::energyColour(
                    static_cast<float>(energyVal) / 10.0f);
                const float bw = 24.0f;
                const float bh = 16.0f;
                const float bx = static_cast<float>(x);
                const float by = static_cast<float>(h / 2) - bh * 0.5f;
                g.setColour(ec.withAlpha(selected ? 0.30f : 0.16f));
                g.fillRoundedRectangle(bx, by, bw, bh, bh * 0.5f);
                g.setColour(selected ? Colors::textPrimary() : ec);
                g.setFont(juce::Font(juce::FontOptions("Segoe UI", 10.0f, juce::Font::bold)));
                g.drawText(t.energy, (int)bx, (int)by, (int)bw, (int)bh,
                           juce::Justification::centred, false);
            } else {
                g.setColour(Colors::textMuted().withAlpha(0.4f));
                g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
                g.drawText("-", x, 0, kColWidths[i] - 4, h, juce::Justification::centredLeft, true);
            }
            x += kColWidths[i];
            continue;
        }

        if (i == 5 && t.bpm.isNotEmpty()) { // BPM - mono digits for column alignment
            g.setColour(selected ? Colors::textPrimary() : colColors[i]);
            g.setFont(juce::Font(juce::FontOptions("Consolas", 11.5f, juce::Font::bold)));
            g.drawText(t.bpm, x, 0, kColWidths[i] - 4, h, juce::Justification::centredLeft, true);
            x += kColWidths[i];
            continue;
        }

        g.setColour(selected ? Colors::textPrimary() : colColors[i]);
        g.setFont(juce::Font("Segoe UI", 11.0f, i == 2 ? juce::Font::bold : juce::Font::plain));
        g.drawText(cols[i], x, 0, kColWidths[i] - 4, h, juce::Justification::centredLeft, true);
        x += kColWidths[i];
    }

    if (!selected)
    {
        g.setColour(Colors::border().withAlpha(0.35f));
        g.drawHorizontalLine(h - 1, 8.0f, static_cast<float>(w - 8));
    }
}

void LibraryView::TrackTableModel::applyFilter()
{
    if (filterText.isEmpty()) {
        filteredTracks = tracks;
        return;
    }
    filteredTracks.clear();
    auto lowerFilter = filterText.toLowerCase();
    for (auto& t : tracks) {
        if (t.title.toLowerCase().contains(lowerFilter) ||
            t.artist.toLowerCase().contains(lowerFilter) ||
            t.album.toLowerCase().contains(lowerFilter) ||
            t.genre.toLowerCase().contains(lowerFilter) ||
            t.bpm.contains(lowerFilter) ||
            t.key.toLowerCase().contains(lowerFilter)) {
            filteredTracks.push_back(t);
        }
    }
}

void LibraryView::TrackTableModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int>(filteredTracks.size())) {
        auto& track = filteredTracks[static_cast<size_t>(row)];
        spdlog::info("Preview track: {}", track.title.toStdString());
        if (onDoubleClicked)
            onDoubleClicked(row);
    }
}

std::vector<int64_t> LibraryView::TrackTableModel::checkedOrSelected(juce::ListBox* lb) const
{
    std::vector<int64_t> ids;
    if (! checkedIds.empty())
    {
        for (const auto& t : filteredTracks)
            if (t.trackId > 0 && isChecked(t.trackId)) ids.push_back(t.trackId);
        if (! ids.empty()) return ids;
    }
    if (lb != nullptr)
    {
        auto sel = lb->getSelectedRows();
        for (int i = 0; i < sel.size(); ++i)
        {
            const int r = sel[i];
            if (r >= 0 && r < static_cast<int>(filteredTracks.size())
                && filteredTracks[static_cast<size_t>(r)].trackId > 0)
                ids.push_back(filteredTracks[static_cast<size_t>(r)].trackId);
        }
    }
    return ids;
}

void LibraryView::TrackTableModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(filteredTracks.size()))
        return;

    if (e.mods.isPopupMenu() && onRightClick)
    {
        onRightClick(row, e);
        return;
    }

    if (e.x < kCheckGutter)
    {
        const int64_t id = filteredTracks[static_cast<size_t>(row)].trackId;
        if (id > 0)
        {
            if (e.mods.isShiftDown() && lastCheckedRow >= 0
                && lastCheckedRow < static_cast<int>(filteredTracks.size()))
            {
                const bool on = ! isChecked(id);
                const int a = juce::jmin(lastCheckedRow, row);
                const int b = juce::jmax(lastCheckedRow, row);
                for (int r = a; r <= b; ++r)
                {
                    const int64_t rid = filteredTracks[static_cast<size_t>(r)].trackId;
                    if (rid <= 0) continue;
                    if (on) checkedIds.insert(rid); else checkedIds.erase(rid);
                }
            }
            else if (isChecked(id)) checkedIds.erase(id);
            else checkedIds.insert(id);

            lastCheckedRow = row;
            if (onCheckedChanged) onCheckedChanged();
        }
        return;
    }

    if (onSelectionChanged)
        onSelectionChanged(row);

    int x = kCheckGutter;
    int clickedCol = -1;
    int colStartX = kCheckGutter;
    for (int i = 0; i < kNumColumns; ++i)
    {
        if (! kColVisible[i]) continue;
        if (e.x >= x && e.x < x + kColWidths[i])
        {
            clickedCol = i;
            colStartX = x;
            break;
        }
        x += kColWidths[i];
    }

    if (clickedCol < 0)
        return;

    auto& t = filteredTracks[static_cast<size_t>(row)];

    if (clickedCol == 11) {
        // Stars drawn starting at colStartX + 2 with 10px spacing, 5 stars
        int relX = e.x - (colStartX + 2);
        int starIndex = relX / 10;
        if (starIndex < 0) starIndex = 0;
        if (starIndex > 4) starIndex = 4;
        int newRating = starIndex + 1;
        if (t.rating.getIntValue() == newRating) newRating = 0;
        t.rating = juce::String(newRating);
        spdlog::info("[Library] Rating set to {} for trackId={}", newRating, t.trackId);
        if (onRatingChanged) onRatingChanged(t.trackId, newRating);
        return;
    }

    juce::String value;
    switch (clickedCol)
    {
        case 3:  value = t.artist;  break; // Artist
        case 5:  value = t.bpm;     break; // BPM
        case 6:  value = t.key;     break; // Key
        case 7:  value = t.camelot; break; // Camelot
        case 10: value = t.genre;   break; // Genre
        case 12: value = "#";       break; // Color tag → inline palette (marker)
        default: return; // Not a filterable column
    }

    if (value.isNotEmpty() && value != "-" && onCellClicked)
    {
        spdlog::info("[Library] Cell click filter: col={} value={}", clickedCol, value.toStdString());
        onCellClicked(row, clickedCol, value);
    }
}


juce::var LibraryView::TrackTableModel::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
{
    juce::String desc = "BeatMateTrackDrag:";
    for (int i = 0; i < selectedRows.size(); ++i)
    {
        if (i > 0) desc += ",";
        desc += juce::String(selectedRows[i]);
    }
    return desc;
}

void LibraryView::showContextMenu(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int)m_model->filteredTracks.size()) return;

    auto& trk = m_model->filteredTracks[row];
    auto menu = juce::PopupMenu();

    menu.addItem(1, BM_TJ("library.menu.play"));
    menu.addItem(4, BM_TJ("library.menu.hotcues"));
    menu.addItem(5, BM_TJ("library.menu.analyze"));
    menu.addItem(6, BM_TJ("library.menu.normalize"));
    menu.addSeparator();
    menu.addItem(2, BM_TJ("library.menu.edit"));
    menu.addSeparator();
    menu.addItem(3, BM_TJ("library.menu.addToSet"));

    if (m_provider)
    {
        auto playlists = m_provider->getAllPlaylists();
        juce::PopupMenu playlistMenu;
        playlistMenu.addItem(200, BM_TJ("library.menu.newPlaylist"));
        if (!playlists.empty()) {
            playlistMenu.addSeparator();
            int plId = 100;
            for (auto& pl : playlists)
                playlistMenu.addItem(plId++, juce::String(pl.name));
        } else {
            playlistMenu.addSeparator();
            playlistMenu.addItem(-1, BM_TJ("library.menu.noPlaylist"), false);
        }
        menu.addSubMenu(BM_TJ("library.menu.addToPlaylist"), playlistMenu);
    }

    menu.addSeparator();
    menu.addItem(40, juce::String::fromUTF8("V\xc3\xa9rifier l'int\xc3\xa9grit\xc3\xa9 du fichier"));
    menu.addItem(41, juce::String::fromUTF8("R\xc3\xa9parer le fichier (sauvegarde de l'original)"));
    menu.addSeparator();
    menu.addItem(10, BM_TJ("library.menu.locate"));
    menu.addItem(11, BM_TJ("library.menu.copyInfo"));
    menu.addSeparator();

    juce::PopupMenu exportMenu;
    exportMenu.addItem(20, "Rekordbox XML");
    exportMenu.addItem(21, "Traktor NML");
    exportMenu.addItem(22, "Serato Crates");
    menu.addSubMenu(BM_TJ("library.menu.exportTo"), exportMenu);

    menu.addSeparator();
    menu.addItem(30, BM_TJ("library.menu.findDupes"));
    menu.addItem(32, BM_TJ("library.menu.relink"));
    menu.addItem(31, BM_TJ("library.menu.delete"));

    juce::Point<int> screenPt = e.getScreenPosition();
    menu.showMenuAsync(juce::PopupMenu::Options()
            .withTargetScreenArea(juce::Rectangle<int>(screenPt.x, screenPt.y, 1, 1)),
        [this, row, &trk](int result) {
            switch (result)
            {
                case 1: // Pre-ecouter
                    if (onTrackPreview)
                        onTrackPreview(trk.title, trk.artist, trk.bpm.getDoubleValue(), trk.key, trk.filePath);
                    break;
                case 4: // Ouvrir dans Hot Cues
                    if (onOpenInHotCues)
                        onOpenInHotCues(trk.trackId);
                    break;
                case 5: // Analyser la selection
                case 6: // Normaliser la selection
                {
                    auto& cb = (result == 5) ? onAnalyzeTracks : onNormalizeTracks;
                    if (!cb)
                        break;
                    auto ids = m_model->checkedOrSelected(m_trackList.get());
                    if (ids.empty())
                        ids.push_back(trk.trackId);
                    cb(std::move(ids));
                    break;
                }
                case 2: // Batch edit
                    showBatchEditDialog();
                    break;
                case 3: // Ajouter au set
                {
                    auto ids = m_model->checkedOrSelected(m_trackList.get());
                    if (ids.empty())
                        ids.push_back(trk.trackId);
                    if (onAddToSet)
                        onAddToSet(ids);
                    m_listeners.call([&ids](Listener& l) { l.addToSetRequested(ids); });
                    break;
                }
                default:
                    // Playlist selection (100-199)
                    if (result >= 100 && result < 200 && m_provider) {
                        auto playlists = m_provider->getAllPlaylists();
                        int idx = result - 100;
                        if (idx >= 0 && idx < (int)playlists.size()) {
                            m_provider->addToPlaylist(playlists[idx].id, trk.trackId);
                            spdlog::info("[Library] Added track {} to playlist '{}'", trk.trackId, playlists[idx].name);
                        }
                    } else if (result == 200) {
                        // New playlist - show async dialog
                        auto* win = new juce::AlertWindow(BM_TJ("library.newPlaylist.title"), BM_TJ("library.newPlaylist.prompt"), juce::AlertWindow::QuestionIcon);
                        win->addTextEditor("name", "");
                        win->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                        win->addButton(BM_TJ("common.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                        win->enterModalState(true, juce::ModalCallbackFunction::create(
                            [this, trackId = trk.trackId, win](int res){
                                if (res == 1 && m_provider) {
                                    auto name = win->getTextEditorContents("name").toStdString();
                                    if (!name.empty()) {
                                        m_provider->createPlaylist(name);
                                        auto pls = m_provider->getAllPlaylists();
                                        for (auto& pl : pls) {
                                            if (pl.name == name) {
                                                m_provider->addToPlaylist(pl.id, trackId);
                                                break;
                                            }
                                        }
                                    }
                                }
                                delete win;
                            }), false);
                    }
                    break;
                case 40:
                    quickIntegrity(row, false);
                    break;
                case 41:
                    quickIntegrity(row, true);
                    break;
                case 10: // Localiser
                    juce::File(trk.filePath).revealToUser();
                    break;
                case 11: // Copier
                    juce::SystemClipboard::copyTextToClipboard(trk.artist + " - " + trk.title);
                    break;
                case 20: // Export -> Rekordbox XML
                case 21: // Export -> Traktor NML
                case 22: // Export -> Serato (M3U crate fallback — no SeratoCrateWriter available)
                {
                    if (auto* lic = g_serviceLocator
                            ? g_serviceLocator->tryGet<Services::Security::LicenseService>() : nullptr) {
                        if (! lic->canUseExport()) {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                juce::String::fromUTF8("Export"),
                                juce::String::fromUTF8(
                                    "L'export vers les logiciels DJ fait partie des versions "
                                    "payantes de BeatMate.\n\nActivez votre licence dans "
                                    "Param\xc3\xa8tres > Licence."),
                                BM_TJ("common.ok"));
                            break;
                        }
                    }
                    auto pickIds = m_model->checkedOrSelected(m_trackList.get());
                    std::set<int64_t> pickSet(pickIds.begin(), pickIds.end());
                    std::vector<LibraryView::TrackTableModel::Track> picked;
                    if (! pickSet.empty()) {
                        for (const auto& ft : m_model->filteredTracks)
                            if (pickSet.count(ft.trackId) > 0)
                                picked.push_back(ft);
                    } else {
                        picked.push_back(trk);
                    }
                    if (picked.empty()) break;

                    const int exportKind = result; // 20/21/22
                    const juce::String filterPattern = (exportKind == 20) ? "*.xml"
                                                     : (exportKind == 21) ? "*.nml"
                                                                          : "*.m3u8";
                    const juce::String defaultName =
                          (exportKind == 20) ? "BeatMate_rekordbox.xml"
                        : (exportKind == 21) ? "BeatMate_traktor.nml"
                                             : "BeatMate_serato_crate.m3u8";
                    auto startDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

                    auto chooser = std::make_shared<juce::FileChooser>(
                        BM_TJ("library.menu.exportTo"),
                        startDir.getChildFile(defaultName),
                        filterPattern);

                    chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                          juce::FileBrowserComponent::canSelectFiles |
                                          juce::FileBrowserComponent::warnAboutOverwriting,
                        [this, chooser, picked, exportKind](const juce::FileChooser& fc)
                        {
                            auto dest = fc.getResult();
                            if (dest == juce::File{}) return;

                            bool ok = false;
                            juce::String errMsg;
                            try
                            {
                                if (exportKind == 20) {
                                    Services::Rekordbox::RekordboxXmlExporter exporter;
                                    for (const auto& ft : picked) {
                                        Services::Rekordbox::RekordboxXmlExporter::ExportTrack et;
                                        et.trackId   = ft.trackId;
                                        et.filePath  = ft.filePath.toStdString();
                                        et.title     = ft.title.toStdString();
                                        et.artist    = ft.artist.toStdString();
                                        et.album     = ft.album.toStdString();
                                        et.genre     = ft.genre.toStdString();
                                        et.key       = ft.key.toStdString();
                                        et.bpm       = static_cast<float>(ft.bpm.getDoubleValue());
                                        et.rating    = ft.rating.getIntValue();
                                        et.color     = ft.color.toStdString();
                                        et.label     = ft.label.toStdString();
                                        et.year      = ft.year.getIntValue();
                                        if (m_provider && ft.trackId > 0) {
                                            auto cues = m_provider->getCuePoints(ft.trackId);
                                            for (const auto& c : cues) {
                                                Services::Rekordbox::RekordboxXmlExporter::ExportTrack::CuePointExport cp;
                                                cp.number  = c.number;
                                                cp.type    = (c.type == Models::CuePointType::Loop) ? 4 : 0;
                                                cp.startMs = c.position * 1000.0;
                                                cp.endMs   = (c.type == Models::CuePointType::Loop && c.length > 0.0)
                                                                ? c.endPosition() * 1000.0 : -1.0;
                                                cp.name    = c.name;
                                                et.cuePoints.push_back(cp);
                                            }
                                        }
                                        exporter.addTrack(et);
                                    }
                                    Services::Rekordbox::RekordboxXmlExporter::ExportPlaylist pl;
                                    pl.name = "BeatMate Export";
                                    for (const auto& ft : picked) if (ft.trackId > 0) pl.trackIds.push_back(ft.trackId);
                                    exporter.addPlaylist(pl);
                                    ok = exporter.exportToFile(dest.getFullPathName().toStdString());
                                }
                                else if (exportKind == 21) {
                                    Services::Traktor::TraktorNmlExporter exporter;
                                    for (const auto& ft : picked) {
                                        Services::Traktor::TraktorNmlExporter::ExportTrack et;
                                        et.filePath = ft.filePath.toStdString();
                                        et.title    = ft.title.toStdString();
                                        et.artist   = ft.artist.toStdString();
                                        et.album    = ft.album.toStdString();
                                        et.genre    = ft.genre.toStdString();
                                        et.key      = ft.key.toStdString();
                                        et.bpm      = static_cast<float>(ft.bpm.getDoubleValue());
                                        exporter.addTrack(et);
                                    }
                                    ok = exporter.exportToFile(dest.getFullPathName().toStdString());
                                }
                                else /* 22: Serato crate via M3U8 */ {
                                    // Serato doesn't have a simple public "crate" format, but
                                    juce::String m3u;
                                    m3u << "#EXTM3U" << juce::newLine;
                                    for (const auto& ft : picked) {
                                        m3u << "#EXTINF:";
                                        // Duration seconds from "mm:ss"
                                        int secs = 0;
                                        auto d = ft.duration.trim();
                                        if (d.contains(":")) {
                                            auto mm = d.upToFirstOccurrenceOf(":", false, false).getIntValue();
                                            auto ss = d.fromFirstOccurrenceOf(":", false, false).getIntValue();
                                            secs = mm * 60 + ss;
                                        }
                                        m3u << secs << "," << ft.artist << " - " << ft.title << juce::newLine;
                                        m3u << ft.filePath << juce::newLine;
                                    }
                                    ok = dest.replaceWithText(m3u, true /*asUtf8*/, true /*BOM*/);
                                }
                            }
                            catch (const std::exception& e) { errMsg = e.what(); ok = false; }
                            catch (...)                     { errMsg = "unknown error"; ok = false; }

                            if (ok) {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::InfoIcon,
                                    BM_TJ("library.menu.exportTo"),
                                    juce::String((int)picked.size()) + " -> " + dest.getFullPathName(),
                                    "OK");
                                spdlog::info("[Library] Exported {} tracks -> {}",
                                             picked.size(),
                                             dest.getFullPathName().toStdString());
                            } else {
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    BM_TJ("library.menu.exportTo"),
                                    juce::String("Export failed") + (errMsg.isEmpty() ? juce::String() : (juce::String(": ") + errMsg)),
                                    "OK");
                                spdlog::warn("[Library] Export failed for {} -> {} ({})",
                                             picked.size(),
                                             dest.getFullPathName().toStdString(),
                                             errMsg.toStdString());
                            }
                        });
                    break;
                }
                case 30: // Trouver les doublons - dialog de resolution complet
                    { juce::Component::SafePointer<LibraryView> sp(this);
                      DuplicateManagerDialog::show(m_provider,
                          [sp] { if (sp != nullptr) sp->refreshLibrary(); }); }
                    break;
                case 32: // Relier les fichiers manquants
                    RelinkDialog::show(m_provider);
                    break;
                case 31: // Supprimer
                    if (m_provider && trk.trackId > 0)
                    {
                        m_provider->deleteTrack(trk.trackId);
                        loadFromDatabase();
                    }
                    break;
            }
        });
}


void LibraryView::showBatchEditDialog()
{
    if (!m_batchEditDialog)
    {
        m_batchEditDialog = std::make_unique<BatchEditDialog>();
        addAndMakeVisible(m_batchEditDialog.get());

        auto arm = [](juce::TextEditor* editor, juce::ToggleButton* toggle) {
            if (editor && toggle)
                editor->onTextChange = [toggle] {
                    toggle->setToggleState(true, juce::dontSendNotification);
                };
        };
        arm(m_batchEditDialog->titleEdit.get(),   m_batchEditDialog->enableTitle.get());
        arm(m_batchEditDialog->artistEdit.get(),  m_batchEditDialog->enableArtist.get());
        arm(m_batchEditDialog->albumEdit.get(),   m_batchEditDialog->enableAlbum.get());
        arm(m_batchEditDialog->genreEdit.get(),   m_batchEditDialog->enableGenre.get());
        arm(m_batchEditDialog->bpmEdit.get(),     m_batchEditDialog->enableBPM.get());
        arm(m_batchEditDialog->keyEdit.get(),     m_batchEditDialog->enableKey.get());
        arm(m_batchEditDialog->labelEdit.get(),   m_batchEditDialog->enableLabel.get());
        arm(m_batchEditDialog->commentEdit.get(), m_batchEditDialog->enableComment.get());
        if (m_batchEditDialog->energySlider && m_batchEditDialog->enableEnergy)
            m_batchEditDialog->energySlider->onValueChange =
                [t = m_batchEditDialog->enableEnergy.get()] {
                    t->setToggleState(true, juce::dontSendNotification);
                };
        if (m_batchEditDialog->ratingSlider && m_batchEditDialog->enableRating)
            m_batchEditDialog->ratingSlider->onValueChange =
                [t = m_batchEditDialog->enableRating.get()] {
                    t->setToggleState(true, juce::dontSendNotification);
                };
    }

    const auto batchIds = m_model->checkedOrSelected(m_trackList.get());
    m_batchEditDialog->trackCount = (int) batchIds.size();

    for (auto* toggle : { m_batchEditDialog->enableTitle.get(), m_batchEditDialog->enableArtist.get(),
                          m_batchEditDialog->enableAlbum.get(), m_batchEditDialog->enableGenre.get(),
                          m_batchEditDialog->enableBPM.get(), m_batchEditDialog->enableKey.get(),
                          m_batchEditDialog->enableEnergy.get(), m_batchEditDialog->enableRating.get(),
                          m_batchEditDialog->enableColor.get(), m_batchEditDialog->enableLabel.get(),
                          m_batchEditDialog->enableComment.get() })
        if (toggle)
            toggle->setToggleState(false, juce::dontSendNotification);

    if (batchIds.size() == 1 && m_provider && m_model)
    {
        {
            auto t = m_provider->getTrack(batchIds.front());
            if (t.id > 0)
            {
                auto fill = [](juce::TextEditor* e, const juce::String& v) {
                    if (e) e->setText(v, false);
                };
                fill(m_batchEditDialog->titleEdit.get(),   juce::String(t.title));
                fill(m_batchEditDialog->artistEdit.get(),  juce::String(t.artist));
                fill(m_batchEditDialog->albumEdit.get(),   juce::String(t.album));
                fill(m_batchEditDialog->genreEdit.get(),   juce::String(t.genre));
                fill(m_batchEditDialog->bpmEdit.get(),     t.bpm > 0 ? juce::String(t.bpm, 1) : juce::String());
                fill(m_batchEditDialog->keyEdit.get(),
                     juce::String(t.camelotKey.empty() ? t.key : t.camelotKey));
                fill(m_batchEditDialog->labelEdit.get(),   juce::String(t.label));
                fill(m_batchEditDialog->commentEdit.get(), juce::String(t.comment));
                if (m_batchEditDialog->energySlider)
                    m_batchEditDialog->energySlider->setValue(t.energy, juce::dontSendNotification);
                if (m_batchEditDialog->ratingSlider)
                    m_batchEditDialog->ratingSlider->setValue(t.rating, juce::dontSendNotification);
                for (auto* toggle : { m_batchEditDialog->enableTitle.get(),
                                      m_batchEditDialog->enableArtist.get(),
                                      m_batchEditDialog->enableAlbum.get(),
                                      m_batchEditDialog->enableGenre.get(),
                                      m_batchEditDialog->enableBPM.get(),
                                      m_batchEditDialog->enableKey.get(),
                                      m_batchEditDialog->enableEnergy.get(),
                                      m_batchEditDialog->enableRating.get(),
                                      m_batchEditDialog->enableLabel.get(),
                                      m_batchEditDialog->enableComment.get() })
                    if (toggle)
                        toggle->setToggleState(false, juce::dontSendNotification);
            }
        }
    }
    m_batchEditDialog->headerLabel->setText(
        BM_TJ("library.batchEdit.header") + juce::String(" (") + juce::String((int) batchIds.size()) + BM_TJ("library.tracksParenSuffix"), juce::dontSendNotification);

    m_batchEditDialog->onApply = [this]() {
        auto result = m_batchEditDialog->getResult();
        auto ids = m_model->checkedOrSelected(m_trackList.get());

        if (m_provider)
        {
            for (int64_t id : ids)
            {
                if (id <= 0) continue;

                auto dbTrack = m_provider->getTrack(id);
                if (result.hasTitle) dbTrack.title = result.title.toStdString();
                if (result.hasArtist) dbTrack.artist = result.artist.toStdString();
                if (result.hasAlbum) dbTrack.album = result.album.toStdString();
                if (result.hasGenre) dbTrack.genre = result.genre.toStdString();
                if (result.hasBPM) dbTrack.bpm = result.bpm;
                if (result.hasKey) { dbTrack.key = result.key.toStdString(); dbTrack.camelotKey = result.key.toStdString(); }
                if (result.hasEnergy) dbTrack.energy = result.energy;
                if (result.hasRating) dbTrack.rating = result.rating;
                if (result.hasColor) dbTrack.color = result.color.toStdString();
                if (result.hasLabel) dbTrack.label = result.label.toStdString();
                if (result.hasComment) dbTrack.comment = result.comment.toStdString();
                m_provider->updateTrack(dbTrack);
            }
        }

        m_batchEditDialog->setVisible(false);
        loadFromDatabase();
    };

    m_batchEditDialog->onCancel = [this]() {
        m_batchEditDialog->setVisible(false);
    };

    int dw = 450, dh = 480;
    m_batchEditDialog->setBounds((getWidth() - dw) / 2, (getHeight() - dh) / 2, dw, dh);
    m_batchEditDialog->setVisible(true);
    m_batchEditDialog->toFront(true);
}

bool LibraryView::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress('f', juce::ModifierKeys::ctrlModifier, 0))
    {
        m_searchEdit->grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        m_trackList->selectRangeOfRows(0, m_model->getNumRows() - 1);
        for (const auto& t : m_model->filteredTracks)
            if (t.trackId > 0) m_model->checkedIds.insert(t.trackId);
        m_trackList->repaint();
        updateCheckedUi();
        repaint();
        return true;
    }
    if (key == juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 0))
    {
        m_trackList->deselectAllRows();
        m_model->checkedIds.clear();
        m_model->lastCheckedRow = -1;
        m_trackList->repaint();
        updateCheckedUi();
        repaint();
        return true;
    }
    if (key == juce::KeyPress('e', juce::ModifierKeys::ctrlModifier, 0))
    {
        showBatchEditDialog();
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        int sel = m_trackList->getSelectedRow();
        if (sel >= 0 && sel < (int)m_model->filteredTracks.size())
        {
            auto& t = m_model->filteredTracks[sel];
            if (onHoverPreview) onHoverPreview(t.filePath, t.title, t.artist);
        }
        return true;
    }
    if (key == juce::KeyPress::deleteKey)
    {
        auto sel = m_trackList->getSelectedRows();
        if (sel.size() > 0 && m_provider)
        {
            int confirm = juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("library.delete.title"),
                BM_TJ("library.delete.confirmPrefix") + juce::String(sel.size()) + BM_TJ("library.delete.confirmSuffix"),
                BM_TJ("library.delete.title"), BM_TJ("common.cancel"));
            if (confirm)
            {
                for (int i = sel.size() - 1; i >= 0; --i)
                {
                    int idx = sel[i];
                    if (idx >= 0 && idx < (int)m_model->filteredTracks.size())
                        m_provider->deleteTrack(m_model->filteredTracks[idx].trackId);
                }
                loadFromDatabase();
            }
        }
        return true;
    }
    return false;
}

void LibraryView::StatisticsPanel::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    ProDraw::glassPanel(g, b, 10.0f);

    if (!hasData) return;

    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("library.statsTitle"), 10, 4, (int)b.getWidth() - 20, 14, juce::Justification::centredLeft);
    g.setColour(Colors::primary());
    g.fillRoundedRectangle(10.0f, 18.0f, 80.0f, 2.0f, 1.0f);

    int y = 24;

    g.setFont(juce::Font(10.0f));
    g.setColour(Colors::textPrimary());
    g.drawText(juce::String((int)report.totalTracks) + BM_TJ("library.stats.tracksSuffix") +
               juce::String(report.totalDurationHours, 1) + "h | " +
               report.totalSizeFormatted,
               10, y, (int)b.getWidth() - 20, 14, juce::Justification::centredLeft);
    y += 18;

    ProDraw::badge(g, "BPM", 10, (float)y, 36, 14, Colors::bpmBadge());
    g.setFont(juce::Font(9.0f));
    g.setColour(Colors::textPrimary());
    g.drawText(BM_TJ("library.stats.avg") + juce::String(report.averageBPM, 1) +
               BM_TJ("library.stats.min") + juce::String(report.minBPM, 0) +
               BM_TJ("library.stats.max") + juce::String(report.maxBPM, 0),
               50, y, (int)b.getWidth() - 60, 14, juce::Justification::centredLeft);
    y += 18;

    ProDraw::badge(g, "NRG", 10, (float)y, 36, 14, Colors::energyBadge());
    g.drawText(BM_TJ("library.stats.avg") + juce::String(report.averageEnergy, 1),
               50, y, (int)b.getWidth() - 60, 14, juce::Justification::centredLeft);
    y += 18;

    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("library.stats.completeness") + juce::String(report.metadataCompleteness, 0) + "%",
               10, y, (int)b.getWidth() - 20, 14, juce::Justification::centredLeft);

    y += 14;
    float barW = b.getWidth() - 20.0f;
    g.setColour(Colors::bgLightest());
    g.fillRoundedRectangle(10.0f, (float)y, barW, 6.0f, 3.0f);
    float fillW = barW * (report.metadataCompleteness / 100.0f);
    juce::Colour fillCol = report.metadataCompleteness >= 80 ? Colors::success()
                         : report.metadataCompleteness >= 50 ? Colors::warning() : Colors::error();
    g.setColour(fillCol);
    g.fillRoundedRectangle(10.0f, (float)y, fillW, 6.0f, 3.0f);
}

void LibraryView::refreshStats()
{
    if (!m_provider || !m_statsPanel) return;
    // CollectionStats needs TrackDatabase, but we can compute basic stats from provider
    auto allTracks = m_provider->getAllTracks();
    auto& r = m_statsPanel->report;
    r.totalTracks = allTracks.size();
    double totalDur = 0, totalBpm = 0;
    int bpmCount = 0, eCount = 0;
    float totalE = 0;
    r.minBPM = 999; r.maxBPM = 0;
    int withMeta = 0;

    for (auto& t : allTracks)
    {
        totalDur += t.duration;
        if (t.bpm > 0) { totalBpm += t.bpm; bpmCount++; r.minBPM = std::min(r.minBPM, t.bpm); r.maxBPM = std::max(r.maxBPM, t.bpm); }
        if (t.energy > 0) { totalE += t.energy; eCount++; }
        if (!t.title.empty() && !t.artist.empty() && t.bpm > 0) withMeta++;
    }

    r.totalDurationHours = totalDur / 3600.0;
    r.averageBPM = bpmCount > 0 ? totalBpm / bpmCount : 0;
    r.averageEnergy = eCount > 0 ? totalE / eCount : 0;
    r.metadataCompleteness = r.totalTracks > 0 ? (float)withMeta / r.totalTracks * 100.0f : 0;
    r.totalSizeFormatted = ""; // Would need file size sum

    m_statsPanel->hasData = true;
    m_statsPanel->repaint();
}

namespace {
// Filter presets persistence: %APPDATA%/BeatMate/filter_presets.json.
static juce::File filterPresetsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("BeatMate")
               .getChildFile("filter_presets.json");
}
} // namespace

void LibraryView::loadFilterPresets()
{
    m_filterPresets.clear();

    // System presets (always re-added first, in fixed order, can't be deleted).
    m_filterPresets.push_back({BM_TJ("library.preset.all"),         60, 200, "", "", "", 1, 10, 0, true});
    m_filterPresets.push_back({BM_TJ("library.preset.fourStars"),   60, 200, "", "", "", 1, 10, 4, true});
    m_filterPresets.push_back({BM_TJ("library.preset.highEnergy"),  60, 200, "", "", "", 7, 10, 0, true});
    m_filterPresets.push_back({BM_TJ("library.preset.chill"),       60, 200, "", "", "", 1,  4, 0, true});

    // DB-first: pull the presets blob from SettingsStore. Falls back to the
    std::string payload;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            if (auto blob = store->getFilterPreset("__all__"); blob.has_value())
                payload = blob->jsonPayload;
        }
    }
    if (payload.empty()) {
        juce::File f = filterPresetsFile();
        if (f.existsAsFile()) {
            try {
                payload = f.loadFileAsString().toStdString();
            } catch (...) {}
        }
    }

    if (!payload.empty())
    {
        try {
            auto j = nlohmann::json::parse(payload);
            if (j.contains("userPresets") && j["userPresets"].is_array())
            {
                for (auto& entry : j["userPresets"])
                {
                    FilterPreset p;
                    p.name      = juce::String::fromUTF8(entry.value("name", std::string()).c_str());
                    p.bpmMin    = entry.value("bpmMin", 60.0f);
                    p.bpmMax    = entry.value("bpmMax", 200.0f);
                    p.key       = juce::String::fromUTF8(entry.value("key", std::string()).c_str());
                    p.genre     = juce::String::fromUTF8(entry.value("genre", std::string()).c_str());
                    p.artist    = juce::String::fromUTF8(entry.value("artist", std::string()).c_str());
                    p.energyMin = entry.value("energyMin", 1.0f);
                    p.energyMax = entry.value("energyMax", 10.0f);
                    p.ratingMin = entry.value("ratingMin", 0);
                    p.isSystem  = false;
                    if (!p.name.isEmpty())
                        m_filterPresets.push_back(std::move(p));
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("[LibraryView] Could not parse filter_presets payload: {}", e.what());
        }
    }

    if (m_presetCombo)
    {
        m_presetCombo->clear(juce::dontSendNotification);
        int id = 1;
        for (auto& p : m_filterPresets)
            m_presetCombo->addItem(p.name, id++);
        m_presetCombo->setSelectedId(1, juce::dontSendNotification);
    }
}

void LibraryView::saveFilterPresets()
{
    nlohmann::json j;
    j["userPresets"] = nlohmann::json::array();
    for (const auto& p : m_filterPresets)
    {
        if (p.isSystem) continue; // system presets are recreated, not stored.
        nlohmann::json entry;
        entry["name"]      = p.name.toStdString();
        entry["bpmMin"]    = p.bpmMin;
        entry["bpmMax"]    = p.bpmMax;
        entry["key"]       = p.key.toStdString();
        entry["genre"]     = p.genre.toStdString();
        entry["artist"]    = p.artist.toStdString();
        entry["energyMin"] = p.energyMin;
        entry["energyMax"] = p.energyMax;
        entry["ratingMin"] = p.ratingMin;
        j["userPresets"].push_back(std::move(entry));
    }

    const std::string dumped = j.dump(2);

    // Primary: DB. Secondary: JSON file as export/backup.
    bool dbOk = false;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            dbOk = store->upsertFilterPreset("__all__", dumped);
        }
    }
    juce::File f = filterPresetsFile();
    f.getParentDirectory().createDirectory();
    try {
        std::ofstream out(f.getFullPathName().toStdString());
        out << dumped;
    } catch (const std::exception& e) {
        spdlog::warn("[LibraryView] Could not write filter_presets.json: {}", e.what());
    }
    if (!dbOk)
        spdlog::warn("[LibraryView] saveFilterPresets: DB write failed (using JSON fallback)");
}

void LibraryView::applyFilterPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= (int)m_filterPresets.size()) return;
    auto& p = m_filterPresets[presetIndex];

    m_bpmMinSlider->setValue(p.bpmMin, juce::dontSendNotification);
    m_bpmMaxSlider->setValue(p.bpmMax, juce::dontSendNotification);
    m_energyMinSlider->setValue(p.energyMin, juce::dontSendNotification);
    m_energyMaxSlider->setValue(p.energyMax, juce::dontSendNotification);
    if (p.ratingMin > 0 && m_ratingFilterCombo)
        m_ratingFilterCombo->setSelectedId(p.ratingMin + 1, juce::dontSendNotification);
    else if (m_ratingFilterCombo)
        m_ratingFilterCombo->setSelectedId(1, juce::dontSendNotification);

    applyAllFilters();
}


namespace {
    static juce::String fmtDurationSecs(double seconds)
    {
        if (seconds <= 0.0) return "-";
        int mins = static_cast<int>(seconds / 60.0);
        int secs = static_cast<int>(seconds) % 60;
        return juce::String(mins) + ":" + juce::String(secs).paddedLeft('0', 2);
    }

    static juce::String fmtUnixDate(int64_t unixTs)
    {
        if (unixTs <= 0) return "-";
        return juce::Time(unixTs * 1000LL).formatted("%Y-%m-%d");
    }

    static void appendModelTrack(std::vector<LibraryView::TrackTableModel::Track>& out,
                                 const Models::Track& t,
                                 const juce::String& sourceBadge)
    {
        LibraryView::TrackTableModel::Track ft;
        ft.title = t.title.empty() ? juce::File(juce::String(t.filePath)).getFileNameWithoutExtension()
                                   : juce::String(t.title);
        ft.artist = juce::String(t.artist);
        ft.album = juce::String(t.album);
        ft.bpm = (t.bpm > 0.0) ? juce::String(t.bpm, 1) : juce::String("-");
        ft.key = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
        ft.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
        ft.energy = t.energy > 0 ? juce::String(static_cast<int>(t.energy)) : juce::String("-");
        ft.duration = fmtDurationSecs(t.duration);
        ft.genre = juce::String(t.genre);
        ft.rating = juce::String(t.rating);
        ft.filePath = juce::String(t.filePath);
        ft.year = t.year > 0 ? juce::String(t.year) : "-";
        ft.stems = "?";
        ft.cueCount = 0;
        ft.playCount = t.playCount;
        ft.trackId = t.id;
        ft.analyzed = t.analyzed;
        ft.color = juce::String(t.color);
        ft.label = juce::String(t.label);
        ft.bitrate = t.bitRate > 0 ? (juce::String(t.bitRate) + " kbps") : juce::String("-");
        ft.lastPlayed = fmtUnixDate(t.lastPlayed);
        ft.sourceBadge = sourceBadge;
        out.push_back(std::move(ft));
    }
}


LibraryView::CollectionTreeItem::CollectionTreeItem(LibraryView& owner,
                                                    CollectionNodeType type,
                                                    juce::String name,
                                                    juce::String payload)
    : type_(type), name_(std::move(name)), payload_(std::move(payload)), owner_(owner)
{
}

juce::String LibraryView::CollectionTreeItem::getUniqueName() const
{
    return juce::String((int)type_) + "::" + name_ + "::" + payload_;
}

bool LibraryView::CollectionTreeItem::mightContainSubItems()
{
    switch (type_)
    {
        case CollectionNodeType::Root:
        case CollectionNodeType::DJRoot:
        case CollectionNodeType::RekordboxPlaylists:
        case CollectionNodeType::SeratoCrates:
        case CollectionNodeType::SeratoSmartCrates:
        case CollectionNodeType::TraktorPlaylists:
        case CollectionNodeType::VirtualDJFolders:
        case CollectionNodeType::Folder:
            return true;
        default:
            return false;
    }
}

void LibraryView::CollectionTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    if (isSelected())
    {
        g.setColour(Colors::primary().withAlpha(0.25f));
        g.fillRect(0, 0, width, height);
    }

    juce::String marker;
    juce::Colour markerCol = Colors::textSecondary();
    switch (type_) {
        case CollectionNodeType::BeatMateLibrary: marker = "B"; markerCol = Colors::primary(); break;
        case CollectionNodeType::DJRoot:
        case CollectionNodeType::DJRootUnavailable:
            if (name_.startsWithIgnoreCase("Rekordbox"))   { marker = "R"; markerCol = juce::Colour(0xFFE25822); }
            else if (name_.startsWithIgnoreCase("Serato")) { marker = "S"; markerCol = juce::Colour(0xFF2EB5FF); }
            else if (name_.startsWithIgnoreCase("Traktor")){ marker = "T"; markerCol = juce::Colour(0xFF66BB6A); }
            else if (name_.startsWithIgnoreCase("Virtual")){ marker = "V"; markerCol = juce::Colour(0xFFE91E63); }
            else if (name_.startsWithIgnoreCase("Engine")) { marker = "E"; markerCol = juce::Colour(0xFFFFCA28); }
            else                                            { marker = "*"; }
            break;
        case CollectionNodeType::RekordboxPlaylists:
        case CollectionNodeType::RekordboxHistory:
        case CollectionNodeType::SeratoCrates:
        case CollectionNodeType::SeratoSmartCrates:
        case CollectionNodeType::TraktorPlaylists:
        case CollectionNodeType::VirtualDJFolders:
            marker = ">";
            markerCol = Colors::textMuted();
            break;
        case CollectionNodeType::Folder:
            marker = "F";
            markerCol = Colors::textMuted();
            break;
        case CollectionNodeType::Playlist:
            marker = "-";
            markerCol = Colors::textMuted();
            break;
        default:
            marker = "";
            break;
    }

    int x = 2;
    if (marker.isNotEmpty())
    {
        g.setColour(markerCol);
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 10.5f, juce::Font::bold)));
        g.drawText(marker, x, 0, 14, height, juce::Justification::centred);
        x += 14;
    }

    g.setColour(type_ == CollectionNodeType::DJRootUnavailable
                ? Colors::textMuted()
                : Colors::textPrimary());
    g.setFont(juce::Font(juce::FontOptions("Segoe UI",
                                            type_ == CollectionNodeType::DJRoot ? 12.5f : 11.5f,
                                            type_ == CollectionNodeType::DJRoot ? juce::Font::bold : juce::Font::plain)));
    g.drawText(name_, x, 0, width - x - 4, height, juce::Justification::centredLeft, true);
}

void LibraryView::CollectionTreeItem::itemSelectionChanged(bool nowSelected)
{
    if (nowSelected)
        owner_.onTreeNodeSelected(type_, payload_);
}

void LibraryView::CollectionTreeItem::itemClicked(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()
        && (type_ == CollectionNodeType::SmartPlaylist
            || type_ == CollectionNodeType::BeatMateSmartlists))
        owner_.showSmartlistMenu(type_, payload_);
}

void LibraryView::CollectionTreeItem::itemOpennessChanged(bool isNowOpen)
{
    if (isNowOpen && !lazyLoaded_)
    {
        lazyLoaded_ = true;
        // Lazy subtree loads for DJ root nodes.
        if (type_ == CollectionNodeType::DJRoot)
        {
            Services::DJSoftware::DJSoftwareType dj{};
            if      (name_.startsWithIgnoreCase("Rekordbox"))   dj = Services::DJSoftware::DJSoftwareType::Rekordbox;
            else if (name_.startsWithIgnoreCase("Serato"))      dj = Services::DJSoftware::DJSoftwareType::Serato;
            else if (name_.startsWithIgnoreCase("Traktor"))     dj = Services::DJSoftware::DJSoftwareType::Traktor;
            else if (name_.startsWithIgnoreCase("VirtualDJ"))   dj = Services::DJSoftware::DJSoftwareType::VirtualDJ;
            else if (name_.startsWithIgnoreCase("Engine"))      dj = Services::DJSoftware::DJSoftwareType::EngineDJ;
            else return;
            owner_.populateDJSubtree(this, dj);
        }
    }
}


void LibraryView::buildCollectionTree()
{
    if (!m_collectionTree) return;

    m_rootItem = std::make_unique<CollectionTreeItem>(*this, CollectionNodeType::Root, BM_TJ("library.tree.collection"));
    m_collectionTree->setRootItem(m_rootItem.get());
    m_collectionTree->setRootItemVisible(false);

    auto beat = std::make_unique<CollectionTreeItem>(*this, CollectionNodeType::BeatMateLibrary, BM_TJ("library.myLibrary"));
    beat->setOpen(true);
    m_rootItem->addSubItem(beat.release());

    // BeatMate smartlists — restored from the DB at every tree build so they
    if (m_provider)
    {
        auto smartRoot = std::make_unique<CollectionTreeItem>(
            *this, CollectionNodeType::BeatMateSmartlists, "Smartlists");
        int smartCount = 0;
        for (const auto& pl : m_provider->getAllPlaylists())
        {
            if (!pl.isSmartPlaylist) continue;
            smartRoot->addSubItem(new CollectionTreeItem(
                *this, CollectionNodeType::SmartPlaylist,
                juce::String(pl.name), juce::String((juce::int64) pl.id)));
            ++smartCount;
        }
        smartRoot->setOpen(smartCount > 0);
        m_rootItem->addSubItem(smartRoot.release());
    }

    // Add one subtree per DJ software — TOUJOURS les 5, même si le détecteur
    struct DJEntry { const char* displayName; };
    static const DJEntry kDJs[] = {
        { "Rekordbox" },
        { "Serato" },
        { "Traktor" },
        { "VirtualDJ" },
        { "Engine DJ" },
    };

    std::map<std::string, bool> availability;
    if (m_djManager) {
        std::vector<Services::DJSoftware::DJSoftwareInfo> detected;
        try { detected = m_djManager->getDetectedSoftware(); } catch (...) {}
        for (const auto& info : detected)
            availability[info.name] = info.isInstalled;
    }

    for (const auto& dj : kDJs)
    {
        auto name = juce::String(dj.displayName);
        const bool installed = availability.count(dj.displayName)
                                   ? availability[dj.displayName]
                                   : true; // par défaut on tente d'afficher
        auto* djNode = new CollectionTreeItem(
            *this,
            installed ? CollectionNodeType::DJRoot
                      : CollectionNodeType::DJRootUnavailable,
            installed ? name : (name + " " + BM_TJ("library.notAvailable")));
        djNode->setOpen(false);
        m_rootItem->addSubItem(djNode);
    }

    if (m_rootItem->getNumSubItems() > 0)
        m_rootItem->getSubItem(0)->setSelected(true, true, juce::dontSendNotification);
}

void LibraryView::rebuildCollectionTree()
{
    if (!m_collectionTree) return;
    m_collectionTree->setRootItem(nullptr);
    m_rootItem.reset();
    buildCollectionTree();
}

void LibraryView::populateDJSubtree(CollectionTreeItem* djRoot,
                                    Services::DJSoftware::DJSoftwareType dj)
{
    if (!djRoot) return;

    using DJT = Services::DJSoftware::DJSoftwareType;

    try
    {
        switch (dj)
        {
            case DJT::Rekordbox:
            {
                auto* playlists = new CollectionTreeItem(*this, CollectionNodeType::RekordboxPlaylists, BM_TJ("library.tree.playlists"));
                auto* history   = new CollectionTreeItem(*this, CollectionNodeType::RekordboxHistory, BM_TJ("library.tree.history"));
                djRoot->addSubItem(playlists);
                djRoot->addSubItem(history);

                // Stratégie 1 : master.db directement (SQLCipher via
                bool populated = false;
                {
                    Services::Rekordbox::RekordboxEnvironment env;
                    std::string dbPath = env.findDatabasePath();
                    Services::Rekordbox::RekordboxDatabase db;
                    bool opened = false;
                    if (!dbPath.empty())
                    {
                        std::string decrypted = dbPath + ".decrypted.db";
                        if (std::filesystem::exists(decrypted))
                            opened = db.openDatabase(decrypted);
                        if (!opened)
                            opened = db.openDatabase(dbPath);
                    }
                    if (opened)
                    {
                        auto infos = db.readPlaylistsRich();
                        std::map<std::string, std::vector<const Services::Rekordbox::RekordboxDatabase::RekordboxPlaylistInfo*>> byParent;
                        for (const auto& info : infos)
                            byParent[info.parentExternalId].push_back(&info);
                        // Stable order: sort each bucket by seq then name so the
                        for (auto& kv : byParent) {
                            std::sort(kv.second.begin(), kv.second.end(),
                                [](auto* a, auto* b) {
                                    if (a->seq != b->seq) return a->seq < b->seq;
                                    return a->name < b->name;
                                });
                        }
                        std::function<void(const std::string&, CollectionTreeItem*)> addChildren;
                        int added = 0;
                        addChildren = [&](const std::string& parentId, CollectionTreeItem* parentItem) {
                            auto it = byParent.find(parentId);
                            if (it == byParent.end()) return;
                            for (const auto* info : it->second) {
                                if (info->name.empty()) continue;
                                // attribute==1 -> folder, attribute==0 -> playlist
                                auto* node = new CollectionTreeItem(
                                    *this,
                                    info->attribute == 1
                                        ? CollectionNodeType::RekordboxPlaylists
                                        : CollectionNodeType::Playlist,
                                    juce::String(info->name),
                                    juce::String(info->externalId));
                                parentItem->addSubItem(node);
                                ++added;
                                if (info->attribute == 1)
                                    addChildren(info->externalId, node);
                            }
                        };
                        // Root parents differ across Rekordbox versions :
                        std::set<std::string> knownIds;
                        for (const auto& info : infos) knownIds.insert(info.externalId);

                        std::set<std::string> rootParents{"", "0", "root"};
                        for (const auto& info : infos) {
                            if (!knownIds.count(info.parentExternalId))
                                rootParents.insert(info.parentExternalId);
                        }

                        int rootsCount = 0;
                        for (const auto& rp : rootParents) {
                            if (byParent.count(rp)) {
                                ++rootsCount;
                                addChildren(rp, playlists);
                            }
                        }
                        db.close();
                        populated = (added > 0);
                        spdlog::info("[Library] Rekordbox tree: {} roots + orphans, {} items added",
                                     rootsCount, added);
                    } else {
                        spdlog::info("[Library] Rekordbox master.db introuvable ou non décryptable, fallback XML");
                    }
                }

                // Stratégie 2 (fallback) : XML exporté par l'utilisateur.
                if (!populated)
                {
                    Services::Rekordbox::RekordboxEnvironment env;
                    auto xmlPath = env.findXmlPath();
                    if (!xmlPath.empty())
                    {
                        Services::Rekordbox::RekordboxXmlParser parser;
                        if (parser.parseXml(xmlPath))
                        {
                            auto pls = parser.getPlaylists();
                            for (const auto& p : pls)
                            {
                                auto* node = new CollectionTreeItem(
                                    *this, CollectionNodeType::Playlist,
                                    juce::String(p.name) + " (" + juce::String(p.trackIds.size()) + ")",
                                    juce::String(p.id));
                                playlists->addSubItem(node);
                            }
                            spdlog::info("[Library] Rekordbox XML fallback: {} playlists", (int) pls.size());
                        }
                    }
                }
                break;
            }
            case DJT::Serato:
            {
                auto* crates = new CollectionTreeItem(*this, CollectionNodeType::SeratoCrates, BM_TJ("library.tree.crates"));
                auto* smart  = new CollectionTreeItem(*this, CollectionNodeType::SeratoSmartCrates, BM_TJ("library.tree.smartCrates"));
                djRoot->addSubItem(crates);
                djRoot->addSubItem(smart);

                Services::Serato::SeratoDatabase sdb;
                if (sdb.open(""))
                {
                    auto names = sdb.readCrateNames();
                    // Serato uses "%%" as hierarchy separator in crate names :
                    std::map<juce::String, CollectionTreeItem*> folderByPath;
                    for (const auto& cn : names)
                    {
                        juce::String full(cn);
                        const bool isSmart = full.containsIgnoreCase("smart");
                        CollectionTreeItem* base = isSmart ? smart : crates;

                        juce::StringArray parts;
                        parts.addTokens(full, "%%", "");
                        parts.removeEmptyStrings();
                        if (parts.isEmpty()) continue;

                        CollectionTreeItem* parent = base;
                        juce::String pathAcc;
                        for (int i = 0; i < parts.size() - 1; ++i)
                        {
                            pathAcc += (i == 0 ? juce::String() : juce::String("%%")) + parts[i];
                            auto it = folderByPath.find(pathAcc);
                            if (it == folderByPath.end())
                            {
                                auto* folder = new CollectionTreeItem(
                                    *this, CollectionNodeType::SeratoCrates,
                                    parts[i], pathAcc);
                                parent->addSubItem(folder);
                                folderByPath[pathAcc] = folder;
                                parent = folder;
                            }
                            else
                            {
                                parent = it->second;
                            }
                        }
                        parent->addSubItem(new CollectionTreeItem(
                            *this, CollectionNodeType::Playlist,
                            parts[parts.size() - 1], juce::String(cn)));
                    }
                    sdb.close();
                }
                break;
            }
            case DJT::Traktor:
            {
                auto* playlists = new CollectionTreeItem(*this, CollectionNodeType::TraktorPlaylists, BM_TJ("library.tree.playlists"));
                djRoot->addSubItem(playlists);

                Services::Traktor::TraktorCollectionParser parser;
                juce::Array<juce::File> searchRoots;
                searchRoots.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                    .getChildFile("Native Instruments"));
                searchRoots.add(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                    .getChildFile("Native Instruments"));

                juce::Array<juce::File> nmls;
                for (const auto& root : searchRoots) {
                    if (root.isDirectory()) {
                        root.findChildFiles(nmls, juce::File::findFiles, true, "collection.nml");
                    }
                }
                if (!nmls.isEmpty())
                {
                    {
                        auto tracks = parser.parse(nmls[0].getFullPathName().toStdString());
                        std::set<std::string> paths;
                        for (const auto& t : tracks)
                            for (const auto& pe : t.playlistEntries)
                                paths.insert(pe.playlistPath);
                        // Traktor playlists are addressed like
                        std::map<juce::String, CollectionTreeItem*> folderByPath;
                        for (const auto& p : paths)
                        {
                            if (p.empty()) continue;
                            juce::StringArray parts;
                            parts.addTokens(juce::String(p), "/\\", "");
                            parts.removeEmptyStrings();
                            if (parts.isEmpty()) continue;

                            CollectionTreeItem* parent = playlists;
                            juce::String pathAcc;
                            for (int i = 0; i < parts.size() - 1; ++i)
                            {
                                pathAcc += "/" + parts[i];
                                auto it = folderByPath.find(pathAcc);
                                if (it == folderByPath.end())
                                {
                                    auto* folder = new CollectionTreeItem(
                                        *this, CollectionNodeType::TraktorPlaylists,
                                        parts[i], pathAcc);
                                    parent->addSubItem(folder);
                                    folderByPath[pathAcc] = folder;
                                    parent = folder;
                                }
                                else
                                {
                                    parent = it->second;
                                }
                            }
                            parent->addSubItem(new CollectionTreeItem(
                                *this, CollectionNodeType::Playlist,
                                parts[parts.size() - 1], juce::String(p)));
                        }
                    }
                }
                break;
            }
            case DJT::VirtualDJ:
            {
                auto* playlists = new CollectionTreeItem(
                    *this, CollectionNodeType::VirtualDJFolders, BM_TJ("library.tree.playlists"));
                djRoot->addSubItem(playlists);

                // Prefer the playlists reader (real user tree) to the database
                Services::VirtualDJ::VirtualDJPlaylistReader reader;
                auto entries = reader.readPlaylists("");
                if (!entries.empty())
                {
                    std::map<std::string, CollectionTreeItem*> byPath;
                    byPath[""] = playlists;
                    // First pass : create all folder nodes so playlists find
                    auto ensureFolder = [&](const std::string& fullPath,
                                            const std::string& leafName,
                                            const std::string& parentPath)
                    {
                        if (fullPath.empty() || byPath.count(fullPath)) return;
                        auto pit = byPath.find(parentPath);
                        CollectionTreeItem* parent = pit != byPath.end() ? pit->second : playlists;
                        auto* folder = new CollectionTreeItem(
                            *this, CollectionNodeType::VirtualDJFolders,
                            juce::String(leafName),
                            juce::String(fullPath));
                        parent->addSubItem(folder);
                        byPath[fullPath] = folder;
                    };
                    for (const auto& vdjEntry : entries)
                        if (vdjEntry.isFolder)
                            ensureFolder(vdjEntry.fullPath, vdjEntry.name, vdjEntry.parentPath);
                    for (const auto& vdjEntry : entries)
                    {
                        if (vdjEntry.isFolder) continue;
                        auto pit = byPath.find(vdjEntry.parentPath);
                        CollectionTreeItem* parent = pit != byPath.end() ? pit->second : playlists;
                        parent->addSubItem(new CollectionTreeItem(
                            *this, CollectionNodeType::Playlist,
                            juce::String(vdjEntry.name),
                            juce::String(vdjEntry.filePath)));
                    }
                }
                else
                {
                    // Fallback : list folder origins from db.
                    Services::VirtualDJ::VirtualDJDatabase vdb;
                    if (vdb.open(""))
                    {
                        auto tracks = vdb.readAllTracks();
                        std::set<std::string> uniq;
                        for (const auto& t : tracks)
                            if (!t.fileFolder.empty()) uniq.insert(t.fileFolder);
                        for (const auto& f : uniq)
                        {
                            auto name = juce::String(f);
                            playlists->addSubItem(new CollectionTreeItem(
                                *this, CollectionNodeType::Playlist, name, name));
                        }
                        vdb.close();
                    }
                }
                break;
            }
            case DJT::EngineDJ:
            {
                auto* playlistsNode = new CollectionTreeItem(
                    *this, CollectionNodeType::EngineDJPlaylists, BM_TJ("library.tree.playlists"));
                djRoot->addSubItem(playlistsNode);

                Services::EngineDJ::EngineDJService svc;
                if (svc.initialize() && svc.isAvailable())
                {
                    auto playlists = svc.readPlaylists();
                    // Engine DJ uses parentListId : rebuild tree by id.
                    std::map<int64_t, CollectionTreeItem*> byId;
                    byId[0] = playlistsNode;
                    std::map<int64_t, std::vector<const Services::EngineDJ::EngineDJPlaylistInfo*>> byParent;
                    for (const auto& p : playlists)
                        byParent[p.parentEngineId].push_back(&p);
                    std::function<void(int64_t, CollectionTreeItem*)> addChildren;
                    addChildren = [&](int64_t parentId, CollectionTreeItem* parentItem) {
                        auto it = byParent.find(parentId);
                        if (it == byParent.end()) return;
                        for (const auto* p : it->second) {
                            if (p->name.empty()) continue;
                            const bool isFolder = p->trackPaths.empty()
                                && byParent.count(p->engineId) > 0;
                            auto* node = new CollectionTreeItem(
                                *this,
                                isFolder ? CollectionNodeType::EngineDJPlaylists
                                         : CollectionNodeType::Playlist,
                                juce::String(p->name),
                                juce::String(std::to_string(p->engineId)));
                            parentItem->addSubItem(node);
                            byId[p->engineId] = node;
                            if (isFolder) addChildren(p->engineId, node);
                        }
                    };
                    addChildren(0, playlistsNode);
                }
                break;
            }
            default:
                break;
        }
    }
    catch (const std::exception& e)
    {
        spdlog::warn("[Library] populateDJSubtree failed: {}", e.what());
    }
    catch (...)
    {
        spdlog::warn("[Library] populateDJSubtree failed (unknown exception)");
    }
}


void LibraryView::showSmartlistMenu(CollectionNodeType type, const juce::String& payload)
{
    if (!m_provider) return;
    juce::PopupMenu menu;
    menu.addItem(1, BM_TJ("library.menu.newSmartlist"));
    if (type == CollectionNodeType::SmartPlaylist) {
        menu.addItem(2, BM_TJ("library.menu.editSmartlist"));
        menu.addItem(3, BM_TJ("library.menu.deleteSmartlist"));
    }
    const int64_t plId = payload.getLargeIntValue();
    juce::Component::SafePointer<LibraryView> self(this);
    menu.showMenuAsync({}, [self, type, plId](int r) {
        auto* lv = self.getComponent();
        if (!lv || !lv->m_provider) return;
        if (r == 1) {
            auto* win = new juce::AlertWindow("Nouvelle smartlist", "Nom :", juce::AlertWindow::QuestionIcon);
            win->addTextEditor("name", "");
            win->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
            win->addButton(BM_TJ("common.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            win->enterModalState(true, juce::ModalCallbackFunction::create([self, win](int res) {
                auto* lv2 = self.getComponent();
                if (res == 1 && lv2 && lv2->m_provider) {
                    auto name = win->getTextEditorContents("name").toStdString();
                    if (!name.empty()) {
                        const int64_t id = lv2->m_provider->createSmartPlaylist(name, {});
                        lv2->rebuildCollectionTree();
                        lv2->openSmartlistEditor(id);
                    }
                }
                delete win;
            }), false);
        } else if (r == 2 && plId > 0) {
            lv->openSmartlistEditor(plId);
        } else if (r == 3 && plId > 0) {
            lv->m_provider->deletePlaylist(plId);
            lv->rebuildCollectionTree();
        }
    });
}

void LibraryView::openSmartlistEditor(int64_t playlistId)
{
    if (!m_provider || playlistId <= 0) return;
    auto* editor = new Widgets::NestedSmartRuleEditor();
    editor->setSize(620, 520);
    editor->setGroup(m_provider->getSmartPlaylistRules(playlistId));
    juce::Component::SafePointer<LibraryView> self(this);
    editor->onApply = [self, playlistId, editor](const Models::SmartPlaylistRuleGroup& g) {
        auto* lv = self.getComponent();
        if (lv && lv->m_provider) {
            lv->m_provider->updateSmartPlaylistRules(playlistId, g);
            lv->onTreeNodeSelected(CollectionNodeType::SmartPlaylist,
                                   juce::String((juce::int64) playlistId));
        }
        if (auto* dw = editor->findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    editor->onCancel = [editor] {
        if (auto* dw = editor->findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(editor);
    opts.dialogTitle = "Regles de la smartlist";
    opts.dialogBackgroundColour = Colors::bgDarkest();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

void LibraryView::onTreeNodeSelected(CollectionNodeType type, const juce::String& payload)
{
    m_activeNodeType = type;
    m_activeNodePayload = payload;

    switch (type)
    {
        case CollectionNodeType::BeatMateLibrary:
        case CollectionNodeType::Root:
            applyAllFilters();
            break;

        case CollectionNodeType::DJRoot:
        case CollectionNodeType::RekordboxPlaylists:
        case CollectionNodeType::RekordboxHistory:
            loadRekordboxTracksForNode(payload);
            break;

        case CollectionNodeType::SeratoCrates:
        case CollectionNodeType::SeratoSmartCrates:
            // Showing all crates at once: aggregate via Serato DB.
            loadSeratoTracksForNode("");
            break;

        case CollectionNodeType::TraktorPlaylists:
            loadTraktorTracksForNode("");
            break;

        case CollectionNodeType::VirtualDJFolders:
            loadVirtualDJTracksForNode("");
            break;

        case CollectionNodeType::EngineDJPlaylists:
            loadEngineDJTracksForNode("");
            break;

        case CollectionNodeType::BeatMateSmartlists:
            applyAllFilters();
            break;

        case CollectionNodeType::SmartPlaylist:
        {
            if (!m_provider) break;
            const int64_t plId = payload.getLargeIntValue();
            auto tracks = m_provider->refreshSmartPlaylist(plId);
            m_model->filteredTracks.clear();
            for (const auto& t : tracks)
                appendModelTrack(m_model->filteredTracks, t, "S");
            m_model->sortCol = m_sortColumn;
            m_model->sortAsc = m_sortAscending;
            m_model->sortTracks();
            m_trackList->updateContent();
            m_trackCountLabel->setText(
                juce::String(m_model->filteredTracks.size())
                + BM_TJ("library.tracksSuffix") + " [Smartlist]",
                juce::dontSendNotification);
            break;
        }

        case CollectionNodeType::Playlist:
        case CollectionNodeType::Folder:
        {
            // Route according to parent DJ: scan upwards to find DJRoot name.
            auto selItem = dynamic_cast<CollectionTreeItem*>(m_collectionTree->getSelectedItem(0));
            juce::String djName;
            for (auto* p = selItem ? selItem->getParentItem() : nullptr; p != nullptr; p = p->getParentItem())
            {
                auto* cti = dynamic_cast<CollectionTreeItem*>(p);
                if (cti && cti->type_ == CollectionNodeType::DJRoot) { djName = cti->name_; break; }
            }
            if      (djName.startsWithIgnoreCase("Rekordbox"))  loadRekordboxTracksForNode(payload);
            else if (djName.startsWithIgnoreCase("Serato"))     loadSeratoTracksForNode(payload);
            else if (djName.startsWithIgnoreCase("Traktor"))    loadTraktorTracksForNode(payload);
            else if (djName.startsWithIgnoreCase("VirtualDJ"))  loadVirtualDJTracksForNode(payload);
            else if (djName.startsWithIgnoreCase("Engine"))     loadEngineDJTracksForNode(payload);
            else applyAllFilters();
            break;
        }

        case CollectionNodeType::DJRootUnavailable:
        {
            m_model->filteredTracks.clear();
            m_trackList->updateContent();
            m_trackList->repaint();
            m_trackCountLabel->setText(BM_TJ("library.trackCount.sourceUnavailable"), juce::dontSendNotification);
            break;
        }
    }
}


void LibraryView::loadRekordboxTracksForNode(const juce::String& payload)
{
    spdlog::info("[Library] loadRekordboxTracksForNode called, payload='{}'", payload.toStdString());
    m_model->filteredTracks.clear();
    try
    {
        Services::Rekordbox::RekordboxEnvironment env;

        std::string dbPath = env.findDatabasePath();
        spdlog::info("[Library] Rekordbox dbPath='{}' (empty={})", dbPath, dbPath.empty());
        if (!dbPath.empty()) {
            Services::Rekordbox::RekordboxDatabase db;
            std::string decrypted = dbPath + ".decrypted.db";
            bool opened = false;
            if (std::filesystem::exists(decrypted))
                opened = db.openDatabase(decrypted);
            if (!opened) opened = db.openDatabase(dbPath);
            spdlog::info("[Library] Rekordbox open result: {} (decrypted exists={})", opened, std::filesystem::exists(decrypted));
            if (opened) {
                std::vector<std::string> filePaths;
                if (payload.isNotEmpty()) {
                    auto cids = db.readPlaylistContentIds(payload.toStdString());
                    spdlog::info("[Library] Rekordbox playlist '{}' -> {} content IDs", payload.toStdString(), cids.size());
                    for (const auto& cid : cids) {
                        auto fp = db.readContentFilePath(cid);
                        if (!fp.empty()) filePaths.push_back(std::move(fp));
                    }
                    spdlog::info("[Library] Rekordbox playlist filePaths resolved: {}/{}", filePaths.size(), cids.size());
                }
                auto rows = db.readAllTracks();
                spdlog::info("[Library] Rekordbox readAllTracks: {} rows", rows.size());
                std::set<std::string> wanted(filePaths.begin(), filePaths.end());
                for (const auto& r : rows) {
                    if (!wanted.empty() && !wanted.count(r.externalPath)) continue;
                    Models::Track mt;
                    mt.filePath = r.externalPath;
                    mt.title    = r.title;
                    mt.artist   = r.artist;
                    mt.album    = r.album;
                    mt.genre    = r.genre;
                    mt.bpm      = r.bpm;
                    mt.key      = r.tonality;
                    mt.year     = r.year;
                    mt.duration = r.duration;
                    mt.rating   = r.rating;
                    mt.color    = r.color;
                    mt.comment  = r.comment;
                    mt.source   = Models::TrackSource::Rekordbox;
                    appendModelTrack(m_model->filteredTracks, mt, "R");
                }
                db.close();
                spdlog::info("[Library] Rekordbox filteredTracks built: {} tracks", m_model->filteredTracks.size());
                if (!m_model->filteredTracks.empty()) {
                    m_model->sortCol = m_sortColumn;
                    m_model->sortAsc = m_sortAscending;
                    m_model->sortTracks();
                    m_trackList->updateContent();
                    m_trackCountLabel->setText(
                        juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [Rekordbox DB]",
                        juce::dontSendNotification);
                    return;
                }
            }
        }

        auto xmlPath = env.findXmlPath();
        if (xmlPath.empty())
        {
            m_trackCountLabel->setText(BM_TJ("library.error.rekordboxXmlMissing"),
                                       juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        Services::Rekordbox::RekordboxXmlParser parser;
        if (!parser.parseXml(xmlPath))
        {
            m_trackCountLabel->setText(BM_TJ("library.error.rekordboxXmlUnreadable"), juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        auto rkTracks = parser.getTracks();
        auto playlists = parser.getPlaylists();

        std::set<int64_t> keepIds;
        bool filterByPlaylist = payload.isNotEmpty();
        if (filterByPlaylist)
        {
            int64_t pid = payload.getLargeIntValue();
            for (const auto& p : playlists)
                if (p.id == pid) for (auto id : p.trackIds) keepIds.insert(id);
        }

        for (const auto& rt : rkTracks)
        {
            if (filterByPlaylist && !keepIds.count(rt.localTrackId))
                continue;

            Models::Track mt;
            mt.id = rt.localTrackId;
            mt.filePath = rt.externalPath;
            mt.title = rt.title;
            mt.artist = rt.artist;
            mt.album = rt.album;
            mt.genre = rt.genre;
            mt.year = rt.year;
            mt.bpm = rt.bpm;
            mt.key = rt.tonality;
            mt.duration = rt.duration;
            mt.rating = rt.rating;
            mt.color = rt.color;
            mt.label = rt.label;
            mt.source = Models::TrackSource::Rekordbox;
            appendModelTrack(m_model->filteredTracks, mt, "R");
            auto& last = m_model->filteredTracks.back();
            last.cueCount = static_cast<int>(rt.hotCues.size() + rt.memCues.size());
        }
    }
    catch (const std::exception& e) { spdlog::warn("[Library] Rekordbox load: {}", e.what()); }
    catch (...) { spdlog::warn("[Library] Rekordbox load failed"); }

    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [Rekordbox]",
                               juce::dontSendNotification);
}

void LibraryView::loadSeratoTracksForNode(const juce::String& payload)
{
    m_model->filteredTracks.clear();
    try
    {
        Services::Serato::SeratoDatabase sdb;
        if (!sdb.open(""))
        {
            m_trackCountLabel->setText(BM_TJ("library.error.seratoDbMissing"), juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        std::vector<Models::SeratoTrack> seratoTracks;
        if (payload.isNotEmpty())
            seratoTracks = sdb.readCrateTracks(payload.toStdString());
        else
            seratoTracks = sdb.readAllTracks();

        for (const auto& st : seratoTracks)
        {
            Models::Track mt;
            mt.id = st.localTrackId;
            mt.filePath = st.externalPath;
            mt.source = Models::TrackSource::Serato;
            mt.title = st.title;
            mt.artist = st.artist;
            mt.album = st.album;
            mt.genre = st.genre;
            mt.label = st.label;
            mt.comment = st.comment;
            mt.grouping = st.grouping;
            mt.key = st.key;
            mt.camelotKey = st.camelotKey;
            mt.bpm = st.bpm;
            mt.duration = st.duration;
            mt.year = st.year;
            mt.rating = st.rating;
            mt.playCount = (st.playCount > 0 ? st.playCount : st.seratoPlayCount);
            mt.lastPlayed = st.lastPlayed;
            mt.color = st.seratoColor;
            appendModelTrack(m_model->filteredTracks, mt, "S");
        }
        sdb.close();
    }
    catch (const std::exception& e) { spdlog::warn("[Library] Serato load: {}", e.what()); }
    catch (...) { spdlog::warn("[Library] Serato load failed"); }

    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [Serato]",
                               juce::dontSendNotification);
}

void LibraryView::loadTraktorTracksForNode(const juce::String& payload)
{
    m_model->filteredTracks.clear();
    try
    {
        juce::Array<juce::File> nmls;
        juce::Array<juce::File> roots;
        roots.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                      .getChildFile("Native Instruments"));
        roots.add(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                      .getChildFile("Native Instruments"));
        for (const auto& r : roots) {
            if (r.isDirectory())
                r.findChildFiles(nmls, juce::File::findFiles, true, "collection.nml");
        }

        if (nmls.isEmpty())
        {
            m_trackCountLabel->setText(BM_TJ("library.error.traktorNmlMissing"),
                                       juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        Services::Traktor::TraktorCollectionParser parser;
        auto ttracks = parser.parse(nmls[0].getFullPathName().toStdString());

        for (const auto& tt : ttracks)
        {
            if (payload.isNotEmpty())
            {
                bool inPlaylist = false;
                for (const auto& pe : tt.playlistEntries)
                    if (juce::String(pe.playlistPath) == payload) { inPlaylist = true; break; }
                if (!inPlaylist) continue;
            }

            Models::Track mt;
            mt.id       = tt.localTrackId;
            mt.filePath = tt.externalPath;
            mt.title    = tt.title;
            mt.artist   = tt.artist;
            mt.album    = tt.album;
            mt.genre    = tt.genre;
            mt.label    = tt.label;
            mt.comment  = tt.comment;
            mt.year     = tt.year;
            mt.duration = tt.durationSec;
            mt.bpm      = tt.traktorBpm;
            mt.key      = tt.musicalKey;
            mt.playCount= tt.traktorPlayCount;
            mt.rating   = tt.traktorRating / 51;   // Traktor utilise 0-255
            mt.color    = tt.traktorColor;
            mt.source   = Models::TrackSource::Traktor;
            if (mt.title.empty())
                mt.title = juce::File(juce::String(tt.externalPath))
                               .getFileNameWithoutExtension().toStdString();
            appendModelTrack(m_model->filteredTracks, mt, "T");
            auto& last = m_model->filteredTracks.back();
            last.cueCount = static_cast<int>(tt.traktorCues.size());
        }
    }
    catch (const std::exception& e) { spdlog::warn("[Library] Traktor load: {}", e.what()); }
    catch (...) { spdlog::warn("[Library] Traktor load failed"); }

    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [Traktor]",
                               juce::dontSendNotification);
}

void LibraryView::loadVirtualDJTracksForNode(const juce::String& payload)
{
    m_model->filteredTracks.clear();
    try
    {
        Services::VirtualDJ::VirtualDJDatabase vdb;
        if (!vdb.open(""))
        {
            m_trackCountLabel->setText(BM_TJ("library.error.virtualdjDbMissing"), juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        auto vdjTracks = vdb.readAllTracks();
        for (const auto& vt : vdjTracks)
        {
            if (payload.isNotEmpty() && juce::String(vt.fileFolder) != payload)
                continue;

            auto tagGet = [&vt](const char* k) -> std::string {
                auto it = vt.tags.find(k);
                return it != vt.tags.end() ? it->second : std::string{};
            };

            Models::Track mt;
            mt.id        = vt.localTrackId;
            mt.filePath  = vt.externalPath;
            mt.title     = tagGet("Title");
            mt.artist    = tagGet("Author");
            mt.album     = tagGet("Album");
            mt.genre     = tagGet("Genre");
            if (auto y = tagGet("Year"); !y.empty())
                mt.year = std::atoi(y.c_str());
            if (auto b = vt.scanData.bpmScan; !b.empty())
                mt.bpm = std::atof(b.c_str());
            mt.playCount = vt.playCount;
            mt.source    = Models::TrackSource::VirtualDJ;
            if (mt.title.empty())
                mt.title = juce::File(juce::String(vt.externalPath))
                               .getFileNameWithoutExtension().toStdString();
            appendModelTrack(m_model->filteredTracks, mt, "V");
        }
        vdb.close();
    }
    catch (const std::exception& e) { spdlog::warn("[Library] VirtualDJ load: {}", e.what()); }
    catch (...) { spdlog::warn("[Library] VirtualDJ load failed"); }

    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [VirtualDJ]",
                               juce::dontSendNotification);
}

void LibraryView::loadEngineDJTracksForNode(const juce::String& payload)
{
    m_model->filteredTracks.clear();
    try
    {
        Services::EngineDJ::EngineDJService svc;
        if (!svc.initialize() || !svc.isAvailable())
        {
            m_trackCountLabel->setText(BM_TJ("library.error.engineDjDbMissing"), juce::dontSendNotification);
            m_trackList->updateContent();
            return;
        }

        auto playlists = svc.readPlaylists();
        // payload is the playlist engineId (as string), empty = aggregate all.
        const int64_t targetId = payload.isEmpty() ? 0 : payload.getLargeIntValue();

        for (const auto& p : playlists)
        {
            if (targetId != 0 && p.engineId != targetId) continue;
            for (const auto& path : p.trackPaths)
            {
                if (path.empty()) continue;
                Models::Track mt;
                mt.filePath = path;
                mt.source   = Models::TrackSource::EngineDJ;
                appendModelTrack(m_model->filteredTracks, mt, "E");
            }
        }
    }
    catch (const std::exception& e) { spdlog::warn("[Library] Engine DJ load: {}", e.what()); }
    catch (...) { spdlog::warn("[Library] Engine DJ load failed"); }

    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    m_trackList->updateContent();
    m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix") + " [Engine DJ]",
                               juce::dontSendNotification);
}

void LibraryView::importRekordboxXmlFlow()
{
    m_importXmlChooser = std::make_unique<juce::FileChooser>(
        BM_TJ("library.import.selectXml"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.xml");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    m_importXmlChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto xml = fc.getResult();
        if (xml == juce::File{}) return; // user cancelled

        extern BeatMate::ServiceLocator* g_serviceLocator;
        Services::Rekordbox::RekordboxService* rbSvc = nullptr;
        Services::Library::PlaylistManager*   plMgr = nullptr;
        if (g_serviceLocator)
        {
            rbSvc = g_serviceLocator->tryGet<Services::Rekordbox::RekordboxService>();
            plMgr = g_serviceLocator->tryGet<Services::Library::PlaylistManager>();
        }

        if (rbSvc == nullptr || plMgr == nullptr || m_provider == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("library.import.xmlTitle"),
                BM_TJ("library.import.servicesUnavailable"),
                "OK");
            return;
        }

        // The XML parse + track/playlist upsert can take many seconds on
        class ImportJob : public juce::Thread
        {
        public:
            ImportJob(Services::Rekordbox::RekordboxService* s,
                      Services::Library::PlaylistManager* p,
                      Services::Library::TrackDataProvider* dp,
                      juce::File f,
                      std::function<void(Services::Rekordbox::XmlImportSummary)> done)
                : juce::Thread("RekordboxXmlImport"),
                  svc_(s), plMgr_(p), provider_(dp), file_(std::move(f)),
                  onDone_(std::move(done)) {}
            void run() override {
                auto result = svc_->importFromXmlFile(file_, plMgr_, provider_);
                juce::MessageManager::callAsync(
                    [cb = onDone_, r = std::move(result)]() mutable {
                        if (cb) cb(std::move(r));
                    });
            }
        private:
            Services::Rekordbox::RekordboxService* svc_;
            Services::Library::PlaylistManager*   plMgr_;
            Services::Library::TrackDataProvider* provider_;
            juce::File file_;
            std::function<void(Services::Rekordbox::XmlImportSummary)> onDone_;
        };

        auto* progress = new juce::AlertWindow(
            BM_TJ("library.import.xmlTitle"),
            BM_TJ("library.import.ofPrefix") + xml.getFileName() +
                BM_TJ("library.import.inProgressXml"),
            juce::MessageBoxIconType::InfoIcon);
        progress->enterModalState(true);

        auto job = std::shared_ptr<ImportJob>(new ImportJob(
            rbSvc, plMgr, m_provider, xml,
            [this, progress](Services::Rekordbox::XmlImportSummary summary) {
                progress->exitModalState(0);
                delete progress;

                if (!summary.ok()) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        BM_TJ("library.import.xmlTitle"),
                        BM_TJ("library.import.failed") + juce::String(summary.error),
                        "OK");
                    return;
                }
                juce::String msg;
                msg << BM_TJ("library.import.done") << "\n\n"
                    << BM_TJ("library.import.tracksLabel") << summary.tracksImported << "\n"
                    << BM_TJ("library.import.playlistsLabel") << summary.playlistsImported << "\n"
                    << BM_TJ("library.import.skippedLabel") << summary.skipped;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    BM_TJ("library.import.xmlTitle"),
                    msg, "OK");
                rebuildCollectionTree();
                loadFromDatabase();
            }));
        job->startThread();
        // The captured shared_ptr keeps the job alive until the callback
    });
}

void LibraryView::browseFoldersFlow()
{
    m_browseFolderChooser = std::make_unique<juce::FileChooser>(
        BM_TJ("library.browse.selectFolder"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectDirectories;

    m_browseFolderChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
    {
        auto folder = fc.getResult();
        if (folder == juce::File{} || !folder.isDirectory()) return;

        extern BeatMate::ServiceLocator* g_serviceLocator;

        Services::Library::TrackDatabase* dbPtr = nullptr;
        Services::Library::TrackMetadata* metaPtr = nullptr;
        if (g_serviceLocator) {
            dbPtr   = g_serviceLocator->tryGet<Services::Library::TrackDatabase>();
            metaPtr = g_serviceLocator->tryGet<Services::Library::TrackMetadata>();
        }

        std::shared_ptr<Services::Library::TrackDatabase> dbShared;
        std::shared_ptr<Services::Library::TrackMetadata> metaShared;

        if (dbPtr) {
            dbShared = std::shared_ptr<Services::Library::TrackDatabase>(
                dbPtr, [](Services::Library::TrackDatabase*) {});
        } else {
            dbShared = std::make_shared<Services::Library::TrackDatabase>();
            auto dbFile = juce::File::getSpecialLocation(
                              juce::File::userApplicationDataDirectory)
                              .getChildFile("BeatMate").getChildFile("library.db");
            dbFile.getParentDirectory().createDirectory();
            dbShared->initialize(dbFile.getFullPathName().toStdString());
            spdlog::warn("[Library] browseFoldersFlow: TrackDatabase non injectee, fallback local {}",
                         dbFile.getFullPathName().toStdString());
        }
        if (metaPtr) {
            metaShared = std::shared_ptr<Services::Library::TrackMetadata>(
                metaPtr, [](Services::Library::TrackMetadata*) {});
        } else {
            metaShared = std::make_shared<Services::Library::TrackMetadata>();
        }

        const juce::String folderStr = folder.getFullPathName();

        // Progress dialog (modal). Import runs on background thread so the UI
        class ImportFolderJob : public juce::Thread
        {
        public:
            ImportFolderJob(std::shared_ptr<Services::Library::TrackDatabase> db,
                            std::shared_ptr<Services::Library::TrackMetadata> meta,
                            juce::String path,
                            std::function<void(int /*imported*/, int /*skipped*/, int /*errors*/)> onDone)
              : juce::Thread("BeatMate ImportFolder"),
                db_(std::move(db)), meta_(std::move(meta)),
                path_(std::move(path)), onDone_(std::move(onDone)) {}

            void run() override {
                Services::Library::TrackImporter importer(db_, meta_);
                auto ids = importer.importFolder(path_.toStdString(), true, nullptr);
                juce::MessageManager::callAsync([cb = onDone_, n = (int) ids.size()]() {
                    if (cb) cb(n, 0, 0);
                });
            }

        private:
            std::shared_ptr<Services::Library::TrackDatabase> db_;
            std::shared_ptr<Services::Library::TrackMetadata> meta_;
            juce::String path_;
            std::function<void(int, int, int)> onDone_;
        };

        auto* progress = new juce::AlertWindow(
            BM_TJ("library.browsePc"),
            BM_TJ("library.import.ofPrefix") + folderStr + BM_TJ("library.import.inProgress"),
            juce::MessageBoxIconType::InfoIcon);
        progress->enterModalState(false);

        auto job = std::shared_ptr<ImportFolderJob>(
            new ImportFolderJob(dbShared, metaShared, folderStr,
                [this, progress](int imported, int /*skipped*/, int /*errors*/) {
                    progress->exitModalState(0);
                    delete progress;
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        BM_TJ("library.browsePc"),
                        BM_TJ("library.import.doneTracksPrefix") + juce::String(imported) +
                        BM_TJ("library.import.addedToLibrary"),
                        "OK");
                    loadFromDatabase();
                }),
            [](ImportFolderJob* j) { j->stopThread(5000); delete j; });
        job->startThread();
    });
}

void LibraryView::refreshVisibleRowsInPlace()
{
    if (!m_provider || !m_model || !m_trackList)
        return;
    const int total = static_cast<int>(m_model->filteredTracks.size());
    if (total == 0)
        return;
    int first = m_trackList->getRowContainingPosition(4, 4);
    if (first < 0)
        first = 0;
    first = juce::jmax(0, first - 4);
    const int last = juce::jmin(total - 1, first + m_trackList->getNumRowsOnScreen() + 8);
    bool changed = false;
    for (int row = first; row <= last; ++row)
    {
        auto& trk = m_model->filteredTracks[static_cast<size_t>(row)];
        if (trk.trackId <= 0)
            continue;
        auto t = m_provider->getTrack(trk.trackId);
        if (t.id != trk.trackId)
            continue;
        const juce::String bpmStr = (t.bpm > 0.0) ? juce::String(t.bpm, 0) : "-";
        const juce::String keyStr = t.camelotKey.empty() ? juce::String(t.key)
                                                         : juce::String(t.camelotKey);
        const juce::String energyStr = t.energy > 0 ? juce::String(static_cast<int>(t.energy))
                                                    : juce::String("-");
        if (trk.bpm != bpmStr || trk.key != keyStr || trk.energy != energyStr
            || trk.analyzed != t.analyzed)
        {
            m_model->waveCache.erase(trk.trackId);
            m_wavePending.erase(trk.trackId);
            trk.bpm = bpmStr;
            trk.key = keyStr;
            trk.camelot = juce::String(Core::toCamelot(t.camelotKey.empty() ? t.key : t.camelotKey));
            trk.energy = energyStr;
            trk.analyzed = t.analyzed;
            changed = true;
        }
    }
    spdlog::info("[Library] inplace refresh rows {}..{} changed={}", first, last, changed);
    if (m_trackList)
        m_trackList->repaint();
}

void LibraryView::requestRowWaveform(int64_t trackId)
{
    if (trackId <= 0 || !m_wavePending.insert(trackId).second)
        return;

    if (m_model && m_model->waveCache.size() > 1200)
        m_model->waveCache.clear();

    juce::Component::SafePointer<LibraryView> self(this);
    auto* provider = m_provider;
    m_wavePool.addJob([self, trackId, provider] {
        constexpr int kBars = 56;
        auto cacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("waveform_cache")
            .getFullPathName().toStdString();
        Core::WaveformCacheService wfCache;
        Core::ColouredWaveformData data;
        bool loaded = wfCache.load(std::to_string(trackId), data, cacheDir)
            && !data.points.empty();

        TrackTableModel::RowWave wave;
        if (loaded) {
            wave.low.resize(kBars);
            wave.mid.resize(kBars);
            wave.high.resize(kBars);
            const size_t n = data.points.size();
            float peak = 0.0f;
            for (int b = 0; b < kBars; ++b) {
                const size_t idx = juce::jmin(n - 1,
                    static_cast<size_t>(static_cast<double>(b) / kBars * static_cast<double>(n)));
                const auto& p = data.points[idx];
                wave.low[static_cast<size_t>(b)]  = p.low  * p.amplitude;
                wave.mid[static_cast<size_t>(b)]  = p.mid  * p.amplitude;
                wave.high[static_cast<size_t>(b)] = p.high * p.amplitude;
                peak = juce::jmax(peak, wave.low[static_cast<size_t>(b)],
                                  wave.mid[static_cast<size_t>(b)],
                                  wave.high[static_cast<size_t>(b)]);
            }
            if (peak > 0.001f) {
                const float scale = 1.0f / peak;
                for (int b = 0; b < kBars; ++b) {
                    wave.low[static_cast<size_t>(b)]  *= scale;
                    wave.mid[static_cast<size_t>(b)]  *= scale;
                    wave.high[static_cast<size_t>(b)] *= scale;
                }
            }
        }
        else if (provider) {
            auto track = provider->getTrack(trackId);
            if (track.id == trackId && !track.filePath.empty()
                && Core::RgbPeaksGenerator::isCacheValid(track.filePath)) {
                Core::RgbPeaksData rgb;
                if (Core::RgbPeaksGenerator::read(
                        Core::RgbPeaksGenerator::cacheFileFor(track.filePath), rgb)
                    && rgb.valid() && !rgb.bass.empty()) {
                    loaded = true;
                    wave.low.resize(kBars);
                    wave.mid.resize(kBars);
                    wave.high.resize(kBars);
                    const size_t n = rgb.bass.size();
                    float peak = 0.0f;
                    for (int b = 0; b < kBars; ++b) {
                        const size_t i0 = static_cast<size_t>(
                            static_cast<double>(b) / kBars * static_cast<double>(n));
                        const size_t i1 = juce::jmax(i0 + 1, static_cast<size_t>(
                            static_cast<double>(b + 1) / kBars * static_cast<double>(n)));
                        float lo = 0.0f, mi = 0.0f, hi = 0.0f;
                        for (size_t k = i0; k < i1 && k < n; ++k) {
                            lo = juce::jmax(lo, rgb.bass[k]);
                            if (k < rgb.mid.size())    mi = juce::jmax(mi, rgb.mid[k]);
                            if (k < rgb.treble.size()) hi = juce::jmax(hi, rgb.treble[k]);
                        }
                        wave.low[static_cast<size_t>(b)]  = lo;
                        wave.mid[static_cast<size_t>(b)]  = mi;
                        wave.high[static_cast<size_t>(b)] = hi;
                        peak = juce::jmax(peak, lo, mi, hi);
                    }
                    if (peak > 0.001f) {
                        const float scale = 1.0f / peak;
                        for (int b = 0; b < kBars; ++b) {
                            wave.low[static_cast<size_t>(b)]  *= scale;
                            wave.mid[static_cast<size_t>(b)]  *= scale;
                            wave.high[static_cast<size_t>(b)] *= scale;
                        }
                    }
                }
            }
        }
        wave.exists = loaded;

        juce::MessageManager::callAsync([self, trackId, wave = std::move(wave)]() mutable {
            if (self == nullptr)
                return;
            const bool ok = wave.exists;
            wave.stampMs = juce::Time::getMillisecondCounter();
            self->m_wavePending.erase(trackId);
            if (self->m_model)
                self->m_model->waveCache[trackId] = std::move(wave);
            if (ok)
                self->scheduleListRepaint();
        });
    });
}

void LibraryView::requestRowArt(int64_t trackId, const juce::String& filePath)
{
    if (trackId <= 0 || !m_artPending.insert(trackId).second)
        return;

    if (m_model && m_model->artCache.size() > 2400)
        m_model->artCache.clear();

    juce::Component::SafePointer<LibraryView> self(this);
    m_artPool.addJob([self, trackId, filePath] {
        constexpr int kThumb = 64;
        auto cacheDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("artcache");
        cacheDir.createDirectory();
        auto cacheFile = cacheDir.getChildFile(juce::String(trackId) + ".png");

        juce::Image img;
        if (cacheFile.existsAsFile())
            img = juce::ImageFileFormat::loadFrom(cacheFile);

        if (!img.isValid() && filePath.isNotEmpty()) {
            Services::Library::TrackMetadata meta;
            auto bytes = meta.readAlbumArt(filePath.toStdString());
            if (!bytes.empty()) {
                auto full = juce::ImageFileFormat::loadFrom(bytes.data(), bytes.size());
                if (full.isValid()) {
                    const int srcW = full.getWidth(), srcH = full.getHeight();
                    const int side = juce::jmin(srcW, srcH);
                    auto cropped = full.getClippedImage({ (srcW - side) / 2, (srcH - side) / 2, side, side });
                    img = cropped.rescaled(kThumb, kThumb, juce::Graphics::highResamplingQuality);
                    juce::FileOutputStream os(cacheFile);
                    if (os.openedOk()) {
                        os.setPosition(0);
                        os.truncate();
                        juce::PNGImageFormat png;
                        png.writeImageToStream(img, os);
                    }
                }
            }
        }

        TrackTableModel::RowArt art;
        art.exists = img.isValid();
        art.img = img;

        juce::MessageManager::callAsync([self, trackId, art = std::move(art)]() mutable {
            if (self == nullptr)
                return;
            const bool ok = art.exists;
            art.stampMs = juce::Time::getMillisecondCounter();
            self->m_artPending.erase(trackId);
            if (self->m_model)
                self->m_model->artCache[trackId] = std::move(art);
            if (ok)
                self->scheduleListRepaint();
        });
    });
}

void LibraryView::scheduleListRepaint()
{
    // Coalesce the per-thumbnail/per-waveform repaints into one list repaint
    if (m_listRepaintPending)
        return;
    m_listRepaintPending = true;
    juce::Component::SafePointer<LibraryView> self(this);
    juce::Timer::callAfterDelay(140, [self] {
        if (self == nullptr)
            return;
        self->m_listRepaintPending = false;
        if (self->m_trackList)
            self->m_trackList->repaint();
    });
}

void LibraryView::showDetailForRow(int row)
{
    if (!m_detailPanel || !m_model)
        return;
    if (row < 0 || row >= static_cast<int>(m_model->filteredTracks.size()))
        return;

    const auto& t = m_model->filteredTracks[static_cast<size_t>(row)];

    auto openPanelWith = [this](const Models::Track& track) {
        m_detailPanel->setTrack(track);
        if (!m_detailVisible) {
            m_detailVisible = true;
            if (m_detailToggleBtn) m_detailToggleBtn->setToggleState(true, juce::dontSendNotification);
            resized();
        }
    };

    if (t.trackId > 0 && m_provider) {
        auto track = m_provider->getTrack(t.trackId);
        if (track.id != 0) {
            openPanelWith(track);
            return;
        }
    }

    // Rows coming from DJ-software playlists (rekordbox/Serato/…) have no
    if (t.title.isNotEmpty() || t.filePath.isNotEmpty()) {
        Models::Track track;
        track.title = t.title.toStdString();
        track.artist = t.artist.toStdString();
        track.album = t.album.toStdString();
        track.genre = t.genre.toStdString();
        track.filePath = t.filePath.toStdString();
        track.bpm = t.bpm.getDoubleValue();
        track.key = t.key.toStdString();
        track.camelotKey = t.camelot.toStdString();
        track.energy = static_cast<float>(t.energy.getIntValue());
        track.year = t.year.getIntValue();
        track.rating = t.rating.getIntValue();
        track.playCount = t.playCount;
        {
            auto parts = juce::StringArray::fromTokens(t.duration, ":", "");
            if (parts.size() == 2)
                track.duration = parts[0].getDoubleValue() * 60.0 + parts[1].getDoubleValue();
        }
        openPanelWith(track);
        return;
    }

    if (m_detailVisible)
        m_detailPanel->clearTrack();
}


namespace {

juce::Colour facetChipColour(int dim, const juce::String& value)
{
    switch (dim)
    {
        case 0: return Colors::primary();
        case 1: return Colors::accent();
        case 2:
            if (value.startsWith("1")) return Colors::success();
            if (value.startsWith("4")) return Colors::warning();
            if (value.startsWith("7")) return juce::Colour(0xfff97316);
            if (value.startsWith("9")) return Colors::error();
            return Colors::textMuted();
        case 3: return Colors::secondary();
        case 4:
        {
            const bool minor = value.endsWithIgnoreCase("A");
            const int num = value.retainCharacters("0123456789").getIntValue();
            if (num >= 1 && num <= 12) return Colors::camelot(num, minor);
            return Colors::textMuted();
        }
        case 5:
            if (value == "missing")      return juce::Colour(0xffe11d48);
            if (value == "corrupt")      return Colors::error();
            if (value == "dups")         return juce::Colour(0xfff97316);
            if (value == "not_analyzed") return Colors::accent();
            if (value == "never_played") return Colors::textMuted();
            return Colors::warning();   // no_bpm / no_key / no_genre / no_year
        default: break;
    }
    return Colors::primary();
}

juce::String dupKeyFor(const LibraryView::TrackTableModel::Track& t)
{
    return t.artist.trim().toLowerCase() + "|" + t.title.trim().toLowerCase();
}

bool trackMatchesEtat(const LibraryView::TrackTableModel::Track& t, const juce::String& v,
                      const std::set<juce::String>& dupKeys)
{
    if (v == "no_bpm")       return t.bpm.getDoubleValue() <= 0.0;
    if (v == "no_key")       return t.camelot.trim().isEmpty() && (t.key.trim().isEmpty() || t.key == "-");
    if (v == "no_genre")     return t.genre.trim().isEmpty();
    if (v == "no_year")      return t.year.getIntValue() < 1950;
    if (v == "not_analyzed") return ! t.analyzed;
    if (v == "never_played") return t.playCount <= 0;
    if (v == "dups")         return dupKeys.count(dupKeyFor(t)) > 0;
    if (v == "missing")      return fileHealthFor(t) == 2;
    if (v == "corrupt")      return fileHealthFor(t) == 3;
    return false;
}

} // namespace

juce::String LibraryView::facetValueForTrack(int dim, const TrackTableModel::Track& t)
{
    switch (dim)
    {
        case 0:
        {
            auto s = t.genre.trim();
            if (s.isEmpty()) return juce::String::fromUTF8("Sans genre");
            return s_groupGenreFacets ? genreFamilyOf(s) : s;
        }
        case 1:
        {
            const double b = t.bpm.getDoubleValue();
            if (b <= 0)  return "?";
            if (b < 95)  return "< 95";
            if (b < 105) return "95-105";
            if (b < 115) return "105-115";
            if (b < 122) return "115-122";
            if (b < 126) return "122-126";
            if (b < 130) return "126-130";
            if (b < 136) return "130-136";
            if (b < 142) return "136-142";
            return "142+";
        }
        case 2:
        {
            const int e = t.energy.getIntValue();
            if (e <= 0) return "?";
            if (e <= 3) return "1-3";
            if (e <= 6) return "4-6";
            if (e <= 8) return "7-8";
            return "9-10";
        }
        case 3:
        {
            const int y = t.year.getIntValue();
            if (y < 1950) return "?";
            if (y < 1980) return "70s-";
            if (y < 1990) return "80s";
            if (y < 2000) return "90s";
            if (y < 2010) return "2000s";
            if (y < 2020) return "2010s";
            return "2020s";
        }
        case 4:
        {
            auto c = t.camelot.trim().toUpperCase();
            return c.isEmpty() ? juce::String("?") : c;
        }
        default: break;
    }
    return {};
}

void LibraryView::applyFacetsOnly()
{
    // Un clic de chip filtre la liste EN MÉMOIRE depuis m_facetBase — zéro
    if (! m_model) return;
    if (m_facetBase.empty())
    {
        applyAllFilters();
        return;
    }
    m_model->filteredTracks = m_facetBase;
    applyFacetsAndRebuildChips();
    m_model->sortCol = m_sortColumn;
    m_model->sortAsc = m_sortAscending;
    m_model->sortTracks();
    if (m_trackList) m_trackList->updateContent();
    if (m_trackCountLabel)
        m_trackCountLabel->setText(juce::String(m_model->filteredTracks.size()) + BM_TJ("library.tracksSuffix"),
                                   juce::dontSendNotification);
    updateContextActionButtons();
}

void LibraryView::applyFacetsAndRebuildChips()
{
    if (! m_model) return;
    auto& v = m_model->filteredTracks;
    const int curDim = m_facetBar ? m_facetBar->dim : 0;

    // Clés des doublons, calculées sur la liste complète (avant toute facette)
    std::set<juce::String> dupKeys;
    {
        std::map<juce::String, int> keyCount;
        for (const auto& t : v)
            if (t.title.isNotEmpty()) keyCount[dupKeyFor(t)]++;
        for (const auto& kv : keyCount)
            if (kv.second > 1) dupKeys.insert(kv.first);
    }

    // La dimension État (5) est multi-appartenance : une piste peut être à la
    auto matchesDim = [&dupKeys](int dk, const std::set<juce::String>& vals,
                                 const TrackTableModel::Track& t) {
        if (dk == 5)
        {
            for (const auto& val : vals)
                if (trackMatchesEtat(t, val, dupKeys)) return true;
            return false;
        }
        return vals.count(facetValueForTrack(dk, t)) > 0;
    };

    for (const auto& entry : m_activeFacets)
    {
        const int dk = entry.first;
        const auto& vals = entry.second;
        if (dk == curDim || vals.empty()) continue;
        v.erase(std::remove_if(v.begin(), v.end(), [&matchesDim, dk, &vals](const TrackTableModel::Track& t) {
            return ! matchesDim(dk, vals, t);
        }), v.end());
    }

    // 2. Compteurs de la dimension affichée (avant sa propre facette,
    std::map<juce::String, int> counts;
    if (curDim == 5)
    {
        static const char* etats[] = { "no_bpm", "no_key", "no_genre", "no_year",
                                       "not_analyzed", "never_played", "dups",
                                       "missing", "corrupt" };
        for (const auto& t : v)
            for (const char* et : etats)
                if (trackMatchesEtat(t, et, dupKeys)) counts[et]++;
    }
    else
    {
        for (const auto& t : v) counts[facetValueForTrack(curDim, t)]++;
    }

    auto itCur = m_activeFacets.find(curDim);
    const bool curHas = itCur != m_activeFacets.end() && ! itCur->second.empty();
    if (curHas)
    {
        const auto& vals = itCur->second;
        v.erase(std::remove_if(v.begin(), v.end(), [&matchesDim, curDim, &vals](const TrackTableModel::Track& t) {
            return ! matchesDim(curDim, vals, t);
        }), v.end());
    }

    if (! m_facetBar) return;
    m_facetBar->chips.clear();

    auto push = [&](const juce::String& val, const juce::String& label) {
        auto itc = counts.find(val);
        if (itc == counts.end() || itc->second <= 0) return;
        FacetBar::Chip c;
        c.value = val;
        c.label = label;
        c.count = itc->second;
        c.active = curHas && itCur->second.count(val) > 0;
        m_facetBar->chips.push_back(std::move(c));
    };

    switch (curDim)
    {
        case 0:
        {
            std::vector<std::pair<juce::String, int>> byCount(counts.begin(), counts.end());
            std::sort(byCount.begin(), byCount.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            int shown = 0;
            for (const auto& p : byCount) { if (shown++ >= 28) break; push(p.first, p.first); }
            break;
        }
        case 1:
            for (const char* b : { "< 95", "95-105", "105-115", "115-122", "122-126",
                                    "126-130", "130-136", "136-142", "142+" })
                push(b, b);
            push("?", juce::String::fromUTF8("Sans BPM"));
            break;
        case 2:
            push("1-3",  juce::String::fromUTF8("Douce 1-3"));
            push("4-6",  juce::String::fromUTF8("Moyenne 4-6"));
            push("7-8",  juce::String::fromUTF8("Forte 7-8"));
            push("9-10", juce::String::fromUTF8("Max 9-10"));
            push("?",    juce::String::fromUTF8("Sans \xc3\xa9nergie"));
            break;
        case 3:
            for (const char* b : { "70s-", "80s", "90s", "2000s", "2010s", "2020s" })
                push(b, b);
            push("?", juce::String::fromUTF8("Sans ann\xc3\xa9""e"));
            break;
        case 4:
            for (int n = 1; n <= 12; ++n)
            {
                push(juce::String(n) + "A", juce::String(n) + "A");
                push(juce::String(n) + "B", juce::String(n) + "B");
            }
            push("?", juce::String::fromUTF8("Sans cl\xc3\xa9"));
            break;
        case 5:
            push("missing",      juce::String::fromUTF8("Fichiers manquants"));
            push("corrupt",      juce::String::fromUTF8("Corrompus"));
            push("dups",         juce::String::fromUTF8("Doublons"));
            push("no_bpm",       juce::String::fromUTF8("Sans BPM"));
            push("no_key",       juce::String::fromUTF8("Sans cl\xc3\xa9"));
            push("no_genre",     juce::String::fromUTF8("Sans genre"));
            push("no_year",      juce::String::fromUTF8("Sans ann\xc3\xa9""e"));
            push("not_analyzed", juce::String::fromUTF8("Non analys\xc3\xa9s"));
            push("never_played", juce::String::fromUTF8("Jamais jou\xc3\xa9s"));
            break;
        default: break;
    }

    m_facetBar->anyActive = ! m_activeFacets.empty();
    m_facetBar->repaint();
}

void LibraryView::FacetBar::paint(juce::Graphics& g)
{
    m_zones.clear();
    const int w = getWidth(), h = getHeight();
    g.fillAll(Colors::bgSurface());
    g.setColour(Colors::border());
    g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));

    static const char* dims[] = { "Genre", "BPM", "\xc3\x89nergie", "Ann\xc3\xa9""e", "Cl\xc3\xa9", "\xc3\x89tat" };
    const juce::Font dimFont(juce::FontOptions("Segoe UI", 12.0f, juce::Font::bold));
    g.setFont(dimFont);
    float x = 12.0f;
    for (int i = 0; i < 6; ++i)
    {
        const juce::String lbl = juce::String::fromUTF8(dims[i]);
        const float tw = dimFont.getStringWidthFloat(lbl);
        juce::Rectangle<float> r(x, 5.0f, tw + 18.0f, static_cast<float>(h) - 12.0f);
        const bool on = (i == dim);
        g.setColour(on ? Colors::primary() : Colors::bgLighter());
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(on ? juce::Colours::white : Colors::textSecondary());
        g.drawText(lbl, r.toNearestInt(), juce::Justification::centred);
        m_zones.push_back({ r, 0, i });
        x = r.getRight() + 4.0f;
    }

    if (dim == 0)
    {
        x += 6.0f;
        const juce::String lbl = groupGenres ? juce::String::fromUTF8("Group\xc3\xa9s")
                                             : juce::String::fromUTF8("D\xc3\xa9tail");
        const float tw = dimFont.getStringWidthFloat(lbl);
        juce::Rectangle<float> r(x, 5.0f, tw + 22.0f, static_cast<float>(h) - 12.0f);
        g.setColour(groupGenres ? Colors::secondary().withAlpha(0.85f) : Colors::bgLighter());
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(groupGenres ? juce::Colours::white : Colors::textSecondary());
        g.drawText(lbl, r.toNearestInt(), juce::Justification::centred);
        m_zones.push_back({ r, 5, 0 });
        x = r.getRight();
    }
    x += 8.0f;

    {
        juce::Rectangle<float> lr(x, 5.0f, 24.0f, static_cast<float>(h) - 12.0f);
        const bool canLeft = scrollX > 0.5f;
        g.setColour(canLeft ? Colors::bgLighter() : Colors::bgLighter().withAlpha(0.35f));
        g.fillRoundedRectangle(lr, 6.0f);
        g.setColour(canLeft ? Colors::textPrimary() : Colors::textMuted().withAlpha(0.5f));
        g.setFont(dimFont);
        g.drawText(juce::String::fromUTF8("\xe2\x97\x80"), lr.toNearestInt(), juce::Justification::centred);
        if (canLeft) m_zones.push_back({ lr, 3, 0 });
        x = lr.getRight() + 6.0f;
    }
    const float chipsStartX = x;

    float rightLimit = static_cast<float>(w) - 8.0f;
    if (anyActive)
    {
        juce::Rectangle<float> cr(static_cast<float>(w) - 84.0f, 5.0f, 74.0f, static_cast<float>(h) - 12.0f);
        g.setColour(Colors::error().withAlpha(0.16f));
        g.fillRoundedRectangle(cr, cr.getHeight() * 0.5f);
        g.setColour(Colors::error());
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", 11.5f, juce::Font::bold)));
        g.drawText(juce::String::fromUTF8("\xc3\x97 Effacer"), cr.toNearestInt(), juce::Justification::centred);
        m_zones.push_back({ cr, 2, 0 });
        rightLimit = cr.getX() - 8.0f;
    }

    {
        juce::Rectangle<float> rr(rightLimit - 24.0f, 5.0f, 24.0f, static_cast<float>(h) - 12.0f);
        rightLimit = rr.getX() - 6.0f;
        g.setColour(Colors::bgLighter());
        g.fillRoundedRectangle(rr, 6.0f);
        g.setColour(Colors::textPrimary());
        g.setFont(dimFont);
        g.drawText(juce::String::fromUTF8("\xe2\x96\xb6"), rr.toNearestInt(), juce::Justification::centred);
        m_zones.push_back({ rr, 4, 0 });
    }
    m_pageWidth = juce::jmax(120.0f, (rightLimit - chipsStartX) - 40.0f);

    const juce::Font chipFont(juce::FontOptions("Segoe UI", 11.5f, juce::Font::plain));
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(static_cast<int>(chipsStartX), 0,
                       juce::jmax(0, static_cast<int>(rightLimit - chipsStartX)), h);
    float cx = chipsStartX - scrollX;
    for (int i = 0; i < static_cast<int>(chips.size()); ++i)
    {
        auto& c = chips[static_cast<size_t>(i)];
        const juce::String lbl = c.label + "  " + juce::String(c.count);
        const float tw = chipFont.getStringWidthFloat(lbl);
        juce::Rectangle<float> r(cx, 5.0f, tw + 20.0f, static_cast<float>(h) - 12.0f);
        const juce::Colour base = facetChipColour(dim, c.value);
        if (c.active)
        {
            g.setColour(base);
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            g.setColour(juce::Colours::white);
        }
        else
        {
            g.setColour(base.withAlpha(0.13f));
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            g.setColour(base.withAlpha(0.5f));
            g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
            g.setColour(base.brighter(0.5f));
        }
        g.setFont(chipFont);
        g.drawText(lbl, r.toNearestInt(), juce::Justification::centred);
        if (r.getRight() > chipsStartX && r.getX() < rightLimit)
            m_zones.push_back({ r, 1, i });
        cx = r.getRight() + 6.0f;
    }
    const float contentW = (cx + scrollX) - chipsStartX;
    const float maxScroll = juce::jmax(0.0f, contentW - (rightLimit - chipsStartX));
    if (scrollX > maxScroll) scrollX = maxScroll;
}

void LibraryView::FacetBar::mouseDown(const juce::MouseEvent& e)
{
    for (const auto& z : m_zones)
    {
        if (! z.r.contains(e.position)) continue;
        if (z.kind == 0)
        {
            if (dim != z.index && onDimChanged) onDimChanged(z.index);
        }
        else if (z.kind == 1)
        {
            if (z.index < static_cast<int>(chips.size()) && onChipToggled)
                onChipToggled(chips[static_cast<size_t>(z.index)].value);
        }
        else if (z.kind == 2)
        {
            if (onClearAll) onClearAll();
        }
        else if (z.kind == 3)
        {
            scrollX = juce::jmax(0.0f, scrollX - m_pageWidth);
            repaint();
        }
        else if (z.kind == 4)
        {
            scrollX += m_pageWidth;
            repaint();
        }
        else if (z.kind == 5)
        {
            groupGenres = ! groupGenres;
            if (onGroupToggled) onGroupToggled(groupGenres);
        }
        return;
    }
}

void LibraryView::FacetBar::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scrollX = juce::jmax(0.0f, scrollX - (w.deltaY + w.deltaX) * 120.0f);
    repaint();
}


void LibraryView::toggleTagEditor(bool show)
{
    m_tagEditorVisible = show;
    if (show)
    {
        if (! m_tagEditor)
        {
            m_tagEditor = std::make_unique<TagEditorPanel>(m_provider);
            juce::Component::SafePointer<LibraryView> safe(this);
            m_tagEditor->onClose = [safe] {
                if (safe == nullptr) return;
                if (safe->m_tagEditorBtn)
                    safe->m_tagEditorBtn->setToggleState(false, juce::dontSendNotification);
                safe->toggleTagEditor(false);
                safe->refreshLibrary();
            };
            m_tagEditor->onTracksChanged = [safe] {
                if (safe == nullptr) return;
                safe->refreshLibrary();
            };
            m_tagEditor->onAnalyzeRequested = [safe](std::vector<int64_t> ids) {
                if (safe != nullptr && safe->onAnalyzeTracks)
                    safe->onAnalyzeTracks(std::move(ids));
            };
            addAndMakeVisible(*m_tagEditor);
        }

        // Morceaux cochés en priorité, sinon la sélection, sinon la liste affichée.
        std::vector<int64_t> ids;
        bool explicitChoice = false;
        if (m_model)
        {
            ids = m_model->checkedOrSelected(m_trackList.get());
            explicitChoice = ! ids.empty();
            if (ids.empty())
                for (const auto& t : m_model->filteredTracks)
                    if (t.trackId > 0) ids.push_back(t.trackId);
        }
        m_tagEditor->setTracks(ids, explicitChoice);
        m_tagEditor->setVisible(true);
        m_tagEditor->toFront(false);
    }
    else if (m_tagEditor)
    {
        m_tagEditor->setVisible(false);
    }
    resized();
    repaint();
}


void LibraryView::updateContextActionButtons()
{
    const auto itEtat = m_activeFacets.find(5);
    const bool corruptActive = itEtat != m_activeFacets.end() && itEtat->second.count("corrupt") > 0;
    const bool missingActive = itEtat != m_activeFacets.end() && itEtat->second.count("missing") > 0;

    if (m_repairCorruptBtn && m_repairCorruptBtn->isVisible() != corruptActive)
    {
        m_repairCorruptBtn->setVisible(corruptActive);
        resized();
    }
    if (m_relinkBtn)
    {
        m_relinkBtn->setColour(juce::TextButton::buttonColourId,
            missingActive ? Colors::error().darker(0.15f) : Colors::warning().darker(0.45f));
    }
}

void LibraryView::showIntegrityMenu()
{
    const int sel = m_model ? (int) m_model->checkedOrSelected(m_trackList.get()).size() : 0;
    const int shown = m_model ? (int) m_model->filteredTracks.size() : 0;

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Int\xc3\xa9grit\xc3\xa9 des fichiers"));
    m.addItem(1, juce::String::fromUTF8("Les morceaux coch\xc3\xa9s (") + juce::String(sel) + ")", sel > 0);
    m.addItem(2, juce::String::fromUTF8("Les morceaux affich\xc3\xa9s (") + juce::String(shown) + ")", shown > 0);
    m.addItem(3, juce::String::fromUTF8("Toute la biblioth\xc3\xa8que"), m_provider != nullptr);

    juce::Component::SafePointer<LibraryView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_integrityBtn.get()),
        [safe](int r) { if (safe != nullptr && r > 0) safe->openIntegrityDialog(r); });
}

void LibraryView::openIntegrityDialog(int scope)
{
    if (! m_model) return;
    std::vector<IntegrityDialog::Entry> entries;
    juce::String label;

    auto addFrom = [&entries](const juce::String& path, const juce::String& artist, const juce::String& title) {
        if (path.isEmpty()) return;
        IntegrityDialog::Entry e;
        e.path = path;
        e.title = artist.isNotEmpty() ? artist + juce::String::fromUTF8(" \xe2\x80\x93 ") + title : title;
        entries.push_back(std::move(e));
    };

    if (scope == 1 && m_trackList)
    {
        auto ids = m_model->checkedOrSelected(m_trackList.get());
        std::set<int64_t> want(ids.begin(), ids.end());
        for (const auto& t : m_model->filteredTracks)
            if (want.count(t.trackId) > 0)
                addFrom(t.filePath, t.artist, t.title);
        label = juce::String::fromUTF8("S\xc3\xa9lection");
    }
    else if (scope == 2)
    {
        for (const auto& t : m_model->filteredTracks) addFrom(t.filePath, t.artist, t.title);
        label = juce::String::fromUTF8("Morceaux affich\xc3\xa9s");
    }
    else if (scope == 3 && m_provider)
    {
        for (const auto& t : m_provider->getAllTracks())
            addFrom(juce::String(t.filePath), juce::String(t.artist), juce::String(t.title));
        label = juce::String::fromUTF8("Toute la biblioth\xc3\xa8que");
    }

    juce::Component::SafePointer<LibraryView> safe(this);
    IntegrityDialog::show(std::move(entries), label, [safe] {
        if (safe == nullptr) return;
        s_fileHealthCache.clear();
        facetIntegrityChecker().reload();
        safe->applyFacetsOnly();
    });
}

void LibraryView::quickIntegrity(int row, bool repair)
{
    if (! m_model || row < 0 || row >= (int) m_model->filteredTracks.size()) return;
    const auto& t = m_model->filteredTracks[(size_t) row];
    if (t.filePath.isEmpty()) return;
    std::vector<IntegrityDialog::Entry> entries;
    IntegrityDialog::Entry e;
    e.path = t.filePath;
    e.title = t.artist.isNotEmpty() ? t.artist + juce::String::fromUTF8(" \xe2\x80\x93 ") + t.title : t.title;
    entries.push_back(std::move(e));

    juce::Component::SafePointer<LibraryView> safe(this);
    IntegrityDialog::show(std::move(entries),
        repair ? juce::String::fromUTF8("R\xc3\xa9paration du fichier")
               : juce::String::fromUTF8("V\xc3\xa9rification du fichier"),
        [safe] {
            if (safe == nullptr) return;
            s_fileHealthCache.clear();
            facetIntegrityChecker().reload();
            safe->applyFacetsOnly();
        });
}

void LibraryView::repairCorruptShown()
{
    openIntegrityDialog(2);
}

void LibraryView::toggleAllChecked()
{
    if (! m_model) return;
    const size_t total = m_model->filteredTracks.size();
    if (m_model->checkedIds.size() >= total && total > 0)
        m_model->checkedIds.clear();
    else
        for (const auto& t : m_model->filteredTracks)
            if (t.trackId > 0) m_model->checkedIds.insert(t.trackId);
    m_model->lastCheckedRow = -1;
    if (m_trackList) m_trackList->repaint();
    updateCheckedUi();
    repaint();
}

void LibraryView::updateCheckedUi()
{
    if (! m_model) return;
    const int nb = (int) m_model->checkedIds.size();
    if (m_selectAllTableBtn)
        m_selectAllTableBtn->setButtonText(nb > 0
            ? juce::String::fromUTF8("D\xc3\xa9""cocher (") + juce::String(nb) + ")"
            : juce::String::fromUTF8("Tout cocher"));
    if (m_tagEditorBtn)
        m_tagEditorBtn->setButtonText(nb > 0
            ? juce::String::fromUTF8("\xc3\x89""diteur de tags (") + juce::String(nb) + ")"
            : juce::String::fromUTF8("\xc3\x89""diteur de tags"));
}


void LibraryView::showColorMenuForRow(int row)
{
    if (! m_model || row < 0 || row >= static_cast<int>(m_model->filteredTracks.size()))
        return;
    const auto& ft = m_model->filteredTracks[static_cast<size_t>(row)];
    if (ft.trackId <= 0 || ! m_provider)
        return;

    static const char* kHex[] = { "#e11d48", "#f97316", "#eab308", "#22c55e",
                                  "#06b6d4", "#3b82f6", "#a855f7", "#ec4899" };
    static const char* kNames[] = { "Rouge", "Orange", "Jaune", "Vert",
                                    "Cyan", "Bleu", "Violet", "Rose" };

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Couleur du morceau"));
    for (int i = 0; i < 8; ++i)
    {
        juce::PopupMenu::Item it;
        it.itemID = 1 + i;
        it.text = juce::String::fromUTF8(kNames[i]);
        it.colour = juce::Colour::fromString("FF" + juce::String(kHex[i]).removeCharacters("#"));
        m.addItem(std::move(it));
    }
    m.addSeparator();
    m.addItem(100, juce::String::fromUTF8("Aucune"));

    juce::Component::SafePointer<LibraryView> safe(this);
    const int64_t trackId = ft.trackId;
    m.showMenuAsync(juce::PopupMenu::Options(), [safe, trackId](int r)
    {
        if (safe == nullptr || r == 0 || ! safe->m_provider) return;
        juce::String hex;
        if (r >= 1 && r <= 8) hex = kHex[r - 1];
        auto track = safe->m_provider->getTrack(trackId);
        if (track.id == 0) return;
        track.color = hex.toStdString();
        safe->m_provider->updateTrack(track);
        if (safe->m_model)
            for (auto& t2 : safe->m_model->filteredTracks)
                if (t2.trackId == trackId) { t2.color = hex; break; }
        if (safe->m_trackList) safe->m_trackList->repaint();
    });
}


void LibraryView::saveCurrentViewAs(const juce::String& name)
{
    if (name.trim().isEmpty()) return;
    nlohmann::json views = nlohmann::json::array();
    try { views = nlohmann::json::parse(Prefs::getString("library.savedViews", "[]")); }
    catch (...) { views = nlohmann::json::array(); }

    nlohmann::json v;
    v["name"] = name.trim().toStdString();
    v["search"] = m_searchEdit ? m_searchEdit->getText().toStdString() : std::string();
    v["sortCol"] = static_cast<int>(m_sortColumn);
    v["sortAsc"] = m_sortAscending;
    {
        juce::StringArray w, vis;
        for (int i = 0; i < kNumColumns; ++i)
        {
            w.add(juce::String(kColWidths[i]));
            vis.add(kColVisible[i] ? "1" : "0");
        }
        v["colWidths"] = w.joinIntoString(",").toStdString();
        v["colVisible"] = vis.joinIntoString(",").toStdString();
    }
    nlohmann::json facets = nlohmann::json::object();
    for (const auto& entry : m_activeFacets)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& val : entry.second) arr.push_back(val.toStdString());
        facets[std::to_string(entry.first)] = arr;
    }
    v["facets"] = facets;

    // Remplace une vue existante du même nom
    for (auto it = views.begin(); it != views.end();)
    {
        if (it->value("name", "") == v["name"].get<std::string>()) it = views.erase(it);
        else ++it;
    }
    views.push_back(v);
    Prefs::setString("library.savedViews", views.dump());
}

void LibraryView::applySavedView(const juce::String& name)
{
    nlohmann::json views;
    try { views = nlohmann::json::parse(Prefs::getString("library.savedViews", "[]")); }
    catch (...) { return; }

    for (const auto& v : views)
    {
        if (juce::String(v.value("name", "")) != name) continue;

        if (m_searchEdit)
            m_searchEdit->setText(juce::String::fromUTF8(v.value("search", "").c_str()),
                                  juce::dontSendNotification);
        m_sortColumn = static_cast<SortColumn>(v.value("sortCol", static_cast<int>(SortColumn::Title)));
        m_sortAscending = v.value("sortAsc", true);

        auto wParts = juce::StringArray::fromTokens(juce::String(v.value("colWidths", "")), ",", "");
        for (int i = 0; i < juce::jmin(kNumColumns, wParts.size()); ++i)
        {
            const int w = wParts[i].getIntValue();
            if (w >= 16 && w <= 600) kColWidths[i] = w;
        }
        auto visParts = juce::StringArray::fromTokens(juce::String(v.value("colVisible", "")), ",", "");
        for (int i = 0; i < juce::jmin(kNumColumns, visParts.size()); ++i)
            kColVisible[i] = visParts[i].getIntValue() != 0;
        kColVisible[2] = true;

        m_activeFacets.clear();
        if (v.contains("facets") && v["facets"].is_object())
        {
            for (auto it = v["facets"].begin(); it != v["facets"].end(); ++it)
            {
                const int dim = juce::String(it.key()).getIntValue();
                for (const auto& val : it.value())
                    m_activeFacets[dim].insert(juce::String::fromUTF8(val.get<std::string>().c_str()));
            }
        }

        saveColWidths();
        saveColVisibility();
        updateTableContentWidth();
        applyAllFilters();
        repaint();
        return;
    }
}

void LibraryView::showViewsMenu()
{
    nlohmann::json views = nlohmann::json::array();
    try { views = nlohmann::json::parse(Prefs::getString("library.savedViews", "[]")); }
    catch (...) {}

    juce::PopupMenu m;
    m.addSectionHeader(juce::String::fromUTF8("Vues de la biblioth\xc3\xa8que"));
    std::vector<juce::String> names;
    int id = 1;
    for (const auto& v : views)
    {
        names.push_back(juce::String::fromUTF8(v.value("name", "").c_str()));
        m.addItem(id++, names.back());
    }
    if (! names.empty()) m.addSeparator();
    m.addItem(500, juce::String::fromUTF8("Enregistrer la vue actuelle\xe2\x80\xa6"));
    if (! names.empty())
    {
        juce::PopupMenu del;
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            del.addItem(600 + i, names[static_cast<size_t>(i)]);
        m.addSubMenu(juce::String::fromUTF8("Supprimer une vue"), del);
    }

    juce::Component::SafePointer<LibraryView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(m_viewsBtn.get()),
        [safe, names](int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r >= 1 && r <= static_cast<int>(names.size()))
            {
                safe->applySavedView(names[static_cast<size_t>(r - 1)]);
                return;
            }
            if (r == 500)
            {
                auto* win = new juce::AlertWindow(
                    juce::String::fromUTF8("Enregistrer la vue"),
                    juce::String::fromUTF8("Nom de la vue (recherche, facettes, tri et colonnes actuels) :"),
                    juce::MessageBoxIconType::NoIcon, safe.getComponent());
                win->addTextEditor("name", "", juce::String::fromUTF8("Nom"));
                win->addButton(juce::String::fromUTF8("Enregistrer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
                win->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                win->enterModalState(true, juce::ModalCallbackFunction::create([safe, win](int res)
                {
                    std::unique_ptr<juce::AlertWindow> w(win);
                    if (res != 1 || safe == nullptr) return;
                    safe->saveCurrentViewAs(w->getTextEditorContents("name"));
                }), false);
                return;
            }
            if (r >= 600)
            {
                const int idx = r - 600;
                nlohmann::json all = nlohmann::json::array();
                try { all = nlohmann::json::parse(Prefs::getString("library.savedViews", "[]")); }
                catch (...) { return; }
                if (idx >= 0 && idx < static_cast<int>(all.size()))
                {
                    all.erase(all.begin() + idx);
                    Prefs::setString("library.savedViews", all.dump());
                }
            }
        });
}

} // namespace BeatMate::UI
