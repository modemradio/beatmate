#include "HotCueView.h"
#include "../widgets/waveform/WaveformWidget.h"
#include "../dialogs/LibraryMirrorWindow.h"
#include "../styles/ColorPalette.h"
#include "analysis/AnalysisColumns.h"
#include "hotcue/CuePadGrid.h"
#include "hotcue/CueEditPopover.h"
#include "../../services/config/I18n.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/library/WaveformPrecacheService.h"
#include "../../app/ServiceLocator.h"
#include "../utils/ViewPrefs.h"
#include "../../services/security/LicenseService.h"
#include "../../core/audio/AudioFileReader.h"
#include "../../core/audio/AudioTrack.h"
#include "../../core/analysis/RgbPeaksGenerator.h"
#include "../../core/analysis/BeatGridGenerator.h"
#include "../../core/cue/IntelligentCueCreator.h"
#include "../../core/cue/UltraPreciseHotcueService.h"
#include "../../models/CuePoint.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <spdlog/spdlog.h>

namespace BeatMate::UI {


void HotCueView::TrackListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= static_cast<int>(entries.size()))
        return;

    auto& entry = entries[static_cast<size_t>(row)];

    const juce::Colour bpmBlue = Colors::bpmBadge();
    const int camelotNum = entry.key.initialSectionContainingOnly("0123456789").getIntValue();
    const bool looksCamelot = camelotNum >= 1 && camelotNum <= 12
        && (entry.key.endsWithIgnoreCase("A") || entry.key.endsWithIgnoreCase("B"));
    const juce::Colour keyViolet = looksCamelot
        ? AnalysisColumns::camelotColour(entry.key)
        : Colors::keyBadge();

    if (selected)
    {
        juce::ColourGradient selGrad(Colors::primary().withAlpha(0.30f), 0.0f, 0.0f,
                                      Colors::primary().withAlpha(0.10f), static_cast<float>(w), 0.0f, false);
        g.setGradientFill(selGrad);
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary());
        g.fillRect(0, 2, 3, h - 4);
    }
    else if (row % 2 == 0)
        g.fillAll(Colors::bgDarker().withAlpha(0.4f));
    else
        g.fillAll(Colors::bgLighter().withAlpha(0.2f));

    int colTitle  = static_cast<int>(w * 0.34f);
    int colArtist = static_cast<int>(w * 0.24f);
    int colBpm    = static_cast<int>(w * 0.14f);
    int colKey    = static_cast<int>(w * 0.12f);
    int colCues   = w - colTitle - colArtist - colBpm - colKey;

    int x = 0;
    int textH = h;
    int pad = 6;

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(selected ? juce::Colours::white : Colors::textPrimary());
    g.drawText(entry.title, x + pad, 0, colTitle - pad * 2, textH, juce::Justification::centredLeft, true);
    x += colTitle;

    g.setFont(juce::Font(11.0f));
    g.setColour(selected ? Colors::textPrimary() : Colors::textSecondary());
    g.drawText(entry.artist, x + pad, 0, colArtist - pad * 2, textH, juce::Justification::centredLeft, true);
    x += colArtist;

    {
        juce::String bpmStr = entry.bpm.isNotEmpty() ? entry.bpm : "-";
        float pillW = juce::jmax(44.0f, static_cast<float>(colBpm - 6));
        float pillH = 22.0f;
        float pillX = static_cast<float>(x) + (static_cast<float>(colBpm) - pillW) / 2.0f;
        float pillY = static_cast<float>(h / 2) - pillH / 2.0f;
        juce::Rectangle<float> bpmRect(pillX, pillY, pillW, pillH);
        g.setColour(bpmBlue.withAlpha(0.30f));
        g.fillRoundedRectangle(bpmRect, 11.0f);
        g.setColour(bpmBlue.withAlpha(0.65f));
        g.drawRoundedRectangle(bpmRect, 11.0f, 1.0f);
        g.setColour(bpmBlue);
        g.setFont(juce::Font("Consolas", 10.5f, juce::Font::bold));
        g.drawText(bpmStr, bpmRect, juce::Justification::centred);
    }
    x += colBpm;

    {
        juce::String keyStr = entry.key.isNotEmpty() ? entry.key : "-";
        float pillW = juce::jmax(38.0f, static_cast<float>(colKey - 6));
        float pillH = 22.0f;
        float pillX = static_cast<float>(x) + (static_cast<float>(colKey) - pillW) / 2.0f;
        float pillY = static_cast<float>(h / 2) - pillH / 2.0f;
        juce::Rectangle<float> keyRect(pillX, pillY, pillW, pillH);
        g.setColour(keyViolet.withAlpha(0.30f));
        g.fillRoundedRectangle(keyRect, 11.0f);
        g.setColour(keyViolet.withAlpha(0.65f));
        g.drawRoundedRectangle(keyRect, 11.0f, 1.0f);
        g.setColour(keyViolet);
        g.setFont(juce::Font("Consolas", 10.5f, juce::Font::bold));
        g.drawText(keyStr, keyRect, juce::Justification::centred);
    }
    x += colKey;

    {
        float pillW = static_cast<float>(colCues - 8);
        float pillH = 18.0f;
        float pillX = static_cast<float>(x) + 4.0f;
        float pillY = static_cast<float>(h / 2) - pillH / 2.0f;
        juce::Rectangle<float> cueRect(pillX, pillY, pillW, pillH);

        if (entry.hasCues)
        {
            g.setColour(Colors::success().withAlpha(0.18f));
            g.fillRoundedRectangle(cueRect, 9.0f);
            g.setColour(Colors::success().withAlpha(0.45f));
            g.drawRoundedRectangle(cueRect, 9.0f, 1.0f);
            g.setColour(Colors::success());
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            const juce::String label = entry.cueCount > 0
                ? juce::String(entry.cueCount) + (entry.cueCount > 1 ? " CUES" : " CUE")
                : juce::String(juce::CharPointer_UTF8("\xe2\x9c\x93")) + " CUES";
            g.drawText(label, cueRect, juce::Justification::centred);
        }
        else
        {
            g.setColour(Colors::textDim().withAlpha(0.1f));
            g.fillRoundedRectangle(cueRect, 9.0f);
            g.setColour(Colors::textDim());
            g.setFont(juce::Font(9.0f));
            g.drawText("--", cueRect, juce::Justification::centred);
        }
    }

    g.setColour(Colors::border().withAlpha(0.15f));
    g.drawHorizontalLine(h - 1, 0.0f, static_cast<float>(w));
}

void HotCueView::TrackListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (onRowSelected)
        onRowSelected(row);
}

void HotCueView::TrackListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (onRowSelected)
        onRowSelected(row);
}

struct HotCueView::WaveformClickForwarder : public WaveformWidget::Listener {
    HotCueView& owner;
    explicit WaveformClickForwarder(HotCueView& o) : owner(o) {}
    void positionClicked(double pos) override { owner.onWaveformClicked(pos); }
    void cuePointClicked(int number) override {
        if (number >= 1 && number <= 8)
            owner.selectCue(number - 1);
    }
    void cuePointRightClicked(int number, juce::Point<int> screenPos) override {
        if (number >= 1 && number <= 8)
            owner.showCueEditPopover(number - 1,
                                     juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1));
    }
    void cuePointMoved(int number, double newNormalizedPos) override {
        int idx = number - 1;
        if (idx >= 0 && idx < 8 && owner.m_trackDuration > 0.0)
        {
            double newSeconds = newNormalizedPos * owner.m_trackDuration;
            newSeconds = owner.snapToNearestBeat(newSeconds);
            owner.m_cues[static_cast<size_t>(idx)].positionSeconds = newSeconds;
            owner.updateCueButton(idx);
            owner.updateWaveformCueMarkers();
            owner.m_listeners.call([number, newSeconds](HotCueView::Listener& l) { l.cuePointSet(number, newSeconds); });
            owner.persistCuesToDatabase();

            if (owner.onSeek) owner.onSeek(newSeconds);
            if (owner.onPreviewCue) owner.onPreviewCue(newSeconds);
        }
    }
};


HotCueView::HotCueView()
    : m_provider(nullptr)
{
    initCueDefaults();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    setupUI();
    auto t1 = juce::Time::getMillisecondCounterHiRes();
    spdlog::info("[HotCueView] setupUI took {:.0f}ms", t1 - t0);
    retranslateUi();
}

HotCueView::HotCueView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider)
{
    initCueDefaults();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    setupUI();
    auto t1 = juce::Time::getMillisecondCounterHiRes();
    spdlog::info("[HotCueView] setupUI took {:.0f}ms (with provider)", t1 - t0);
    if (m_provider)
    {
        auto providerPtr = m_provider;
        auto aliveFlag = m_aliveFlag;
        std::thread([this, providerPtr, aliveFlag]() {
            auto tracks = providerPtr->getAllTracks();
            juce::MessageManager::callAsync([this, tracks = std::move(tracks), aliveFlag]() {
                if (!aliveFlag->load()) return;
                m_allTracksCache = std::move(tracks);
                std::set<std::string> genres;
                for (auto& t : m_allTracksCache)
                    if (!t.genre.empty())
                        genres.insert(t.genre);
                m_genreFilter->clear(juce::dontSendNotification);
                m_genreFilter->addItem(BM_TJ("common.all"), 1);
                int genreId = 2;
                for (auto& g : genres)
                    m_genreFilter->addItem(juce::String(g), genreId++);
                m_genreFilter->setSelectedId(1, juce::dontSendNotification);
                m_cueStatusCache.clear();
                m_cueStatusCacheLoaded = false;
                filterTrackList();

                if (m_provider)
                    m_provider->onDataChanged([this, aliveFlag] {
                        juce::MessageManager::callAsync([this, aliveFlag] {
                            if (!aliveFlag->load()) return;
                            populateTrackList();
                        });
                    });
            });
        }).detach();
    }
    retranslateUi();
}

HotCueView::~HotCueView()
{
    // Signal all async callbacks that this object is dead
    m_aliveFlag->store(false);

    m_libraryMirror.reset();

    if (m_folderTree) m_folderTree->setRootItem(nullptr);
    m_folderTree.reset();
    m_rootTreeItem.reset();
    if (m_autoGenThread)
        m_autoGenThread->stopThread(2000);
}


HotCueView::FolderTreeItem::FolderTreeItem(const juce::String& name, const juce::String& fullPath, HotCueView& owner)
    : m_name(name), m_fullPath(fullPath), m_owner(owner) {}

void HotCueView::FolderTreeItem::addSubFolder(std::unique_ptr<FolderTreeItem> child)
{
    // JUCE TreeViewItem::addSubItem takes ownership — release the unique_ptr so
    addSubItem(child.release());
}

void HotCueView::FolderTreeItem::paintItem(juce::Graphics& g, int width, int height)
{
    bool selected = isSelected();
    if (selected)
    {
        g.setColour(Colors::primary().withAlpha(0.2f));
        g.fillRoundedRectangle(0.0f, 1.0f, static_cast<float>(width), static_cast<float>(height - 2), 3.0f);
    }

    int iconX = 2;
    g.setFont(juce::Font(12.0f));
    g.setColour(selected ? Colors::primary() : Colors::textSecondary());
    g.drawText(juce::CharPointer_UTF8("\xf0\x9f\x93\x81"), iconX, 0, 18, height, juce::Justification::centred);

    g.setFont(juce::Font("Segoe UI", 11.0f, selected ? juce::Font::bold : juce::Font::plain));
    g.setColour(selected ? Colors::textPrimary() : Colors::textSecondary());
    g.drawText(m_name, 22, 0, width - 60, height, juce::Justification::centredLeft, true);

    if (m_trackCount > 0)
    {
        juce::String countStr = juce::String(m_trackCount);
        int badgeW = std::max(20, static_cast<int>(countStr.length()) * 8 + 8);
        int badgeX = width - badgeW - 4;
        g.setColour(m_cueCount > 0 ? Colors::success().withAlpha(0.2f) : Colors::bgLighter());
        g.fillRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(height / 2 - 8), static_cast<float>(badgeW), 16.0f, 8.0f);
        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::bold));
        g.setColour(m_cueCount > 0 ? Colors::success() : Colors::textDim());
        g.drawText(countStr, badgeX, 0, badgeW, height, juce::Justification::centred);
    }
}

void HotCueView::FolderTreeItem::itemClicked(const juce::MouseEvent&)
{
    m_owner.filterByFolder(m_fullPath);
}


void HotCueView::buildFolderTree()
{
    if (!m_provider || !m_folderTree) return;

    struct FolderNode {
        std::string name;
        std::string fullPath;
        int trackCount = 0;
        int cueCount = 0;
        std::map<std::string, std::unique_ptr<FolderNode>> children;
    };

    auto root = std::make_unique<FolderNode>();
    root->name = "Collection";
    root->fullPath = "";

    for (auto& track : m_allTracksCache)
    {
        if (track.filePath.empty()) continue;

        juce::File f(juce::String(track.filePath));
        juce::File dir = f.getParentDirectory();
        std::string dirPath = dir.getFullPathName().toStdString();

        // Split path into components. Guard against paths whose parent never
        juce::StringArray parts;
        juce::File current = dir;
        int guard = 0;
        while (current.getFullPathName().length() > 3 && guard++ < 64)
        {
            parts.insert(0, current.getFileName());
            juce::File parent = current.getParentDirectory();
            if (parent.getFullPathName() == current.getFullPathName()) break;
            current = parent;
        }
        parts.insert(0, current.getFullPathName());

        FolderNode* node = root.get();
        std::string builtPath;
        for (int i = 0; i < parts.size(); ++i)
        {
            std::string part = parts[i].toStdString();
            builtPath = (i == 0) ? part : builtPath + "/" + part;

            if (node->children.find(part) == node->children.end())
            {
                auto child = std::make_unique<FolderNode>();
                child->name = part;
                child->fullPath = builtPath;
                node->children[part] = std::move(child);
            }
            node = node->children[part].get();
        }
        node->trackCount++;

        if (m_cueStatusCache.count(track.id) > 0)
            node->cueCount++;
    }

    std::function<std::unique_ptr<FolderTreeItem>(FolderNode&)> buildItem;
    buildItem = [this, &buildItem](FolderNode& node) -> std::unique_ptr<FolderTreeItem>
    {
        auto item = std::make_unique<FolderTreeItem>(
            juce::String(node.name), juce::String(node.fullPath), *this);
        item->m_trackCount = node.trackCount;
        item->m_cueCount = node.cueCount;

        for (auto& [name, child] : node.children)
        {
            auto childItem = buildItem(*child);
            item->m_trackCount += childItem->m_trackCount;
            item->m_cueCount += childItem->m_cueCount;
            item->addSubFolder(std::move(childItem));
        }

        return item;
    };

    m_folderTree->setRootItem(nullptr);
    m_rootTreeItem = buildItem(*root);
    m_rootTreeItem->setOpen(true);

    for (int i = 0; i < m_rootTreeItem->getNumSubItems(); ++i)
        m_rootTreeItem->getSubItem(i)->setOpen(true);

    m_folderTree->setRootItem(m_rootTreeItem.get());
    m_folderTree->setRootItemVisible(true);
}

void HotCueView::filterByFolder(const juce::String& folderPath)
{
    if (!m_trackListModel) return;

    m_trackListModel->entries.clear();

    for (auto& track : m_allTracksCache)
    {
        juce::String trackPath(track.filePath);

        if (folderPath.isEmpty() || (trackPath.startsWith(folderPath) &&
            (trackPath.length() == folderPath.length() ||
             trackPath[folderPath.length()] == '/' ||
             trackPath[folderPath.length()] == '\\')))
        {
            bool hasCues = m_cueStatusCache.count(track.id) > 0;
            TrackEntry entry;
            entry.id = track.id;
            if (auto it = m_cueCountCache.find(track.id); it != m_cueCountCache.end())
                entry.cueCount = it->second;
            entry.title = juce::String(track.title);
            entry.artist = juce::String(track.artist);
            entry.bpm = track.bpm > 0 ? juce::String(track.bpm, 1) : "-";
            entry.key = track.camelotKey.empty()
                ? (track.key.empty() ? juce::String("-") : juce::String(track.key))
                : juce::String(track.camelotKey);
            entry.genre = juce::String(track.genre);
            entry.hasCues = hasCues;
            entry.filePath = track.filePath;
            m_trackListModel->entries.push_back(entry);
        }
    }

    if (m_trackListBox)
        m_trackListBox->updateContent();
}


void HotCueView::initCueDefaults()
{
    for (int i = 0; i < 8; ++i)
    {
        m_cues[static_cast<size_t>(i)].active = false;
        m_cues[static_cast<size_t>(i)].positionSeconds = 0.0;
        m_cues[static_cast<size_t>(i)].color = CueColors::kPalette[i];
        m_cues[static_cast<size_t>(i)].name = CueColors::kDefaultCueNames[i];
        m_cues[static_cast<size_t>(i)].colorIndex = i;
    }
}


void HotCueView::populateTrackList()
{
    if (!m_provider) return;

    // Load tracks SYNCHRONOUSLY (fast — just a DB query, no file I/O)
    m_allTracksCache = m_provider->getAllTracks();

    std::set<std::string> genres;
    for (auto& t : m_allTracksCache)
        if (!t.genre.empty())
            genres.insert(t.genre);

    m_genreFilter->clear(juce::dontSendNotification);
    m_genreFilter->addItem(BM_TJ("common.all"), 1);
    int genreId = 2;
    for (auto& g : genres)
        m_genreFilter->addItem(juce::String(g), genreId++);
    m_genreFilter->setSelectedId(1, juce::dontSendNotification);

    // Don't check cue status per track at startup — only when filter needs it
    m_cueStatusCache.clear();
    m_cueStatusCacheLoaded = false;

    filterTrackList();
}

void HotCueView::filterTrackList()
{
    if (!m_trackListModel) return;

    m_trackListModel->entries.clear();

    juce::String searchQuery = m_searchEditor ? m_searchEditor->getText() : juce::String();
    juce::String selectedGenre = m_genreFilter ? m_genreFilter->getText() : juce::String();
    int cueStatusId = m_cueStatusFilter ? m_cueStatusFilter->getSelectedId() : 1;

    // One batched SQL query fills both the status set and the per-track counts.
    bool needsCueCheck = (cueStatusId == 2 || cueStatusId == 3);
    if (!m_cueStatusCacheLoaded && m_provider)
    {
        m_cueCountCache = m_provider->getCueCounts();
        m_cueStatusCache.clear();
        for (const auto& [id, count] : m_cueCountCache)
            if (count > 0) m_cueStatusCache.insert(id);
        m_cueStatusCacheLoaded = true;
    }

    for (auto& track : m_allTracksCache)
    {
        if (searchQuery.isNotEmpty())
        {
            juce::String display = juce::String(track.artist) + " " + juce::String(track.title);
            if (!display.containsIgnoreCase(searchQuery))
                continue;
        }

        if (selectedGenre.isNotEmpty() && selectedGenre != "Tous")
        {
            if (juce::String(track.genre) != selectedGenre)
                continue;
        }

        if (needsCueCheck)
        {
            bool hasCues = m_cueStatusCache.count(track.id) > 0;
            if (cueStatusId == 2 && hasCues) continue;
            if (cueStatusId == 3 && !hasCues) continue;
        }
        bool hasCues = m_cueStatusCache.count(track.id) > 0;

        TrackEntry entry;
        entry.id = track.id;
        entry.title = juce::String(track.title);
        entry.artist = juce::String(track.artist);
        entry.bpm = track.bpm > 0 ? juce::String(track.bpm, 1) : "-";
        entry.key = track.camelotKey.empty() ? (track.key.empty() ? juce::String("-") : juce::String(track.key)) : juce::String(track.camelotKey);
        entry.genre = juce::String(track.genre);
        entry.hasCues = hasCues;
        if (auto it = m_cueCountCache.find(track.id); it != m_cueCountCache.end())
            entry.cueCount = it->second;
        entry.filePath = track.filePath;

        m_trackListModel->entries.push_back(entry);
    }

    if (m_trackListBox)
        m_trackListBox->updateContent();
}


void HotCueView::loadTrackByIndex(int index)
{
    if (!m_trackListModel || index < 0 || index >= static_cast<int>(m_trackListModel->entries.size()))
        return;
    loadTrackEntry(m_trackListModel->entries[static_cast<size_t>(index)]);
}

void HotCueView::loadTrackById(int64_t id)
{
    if (id <= 0) return;
    for (const auto& t : m_allTracksCache) {
        if (t.id == id) {
            TrackEntry e;
            e.id = t.id;
            e.title = juce::String(t.title);
            e.artist = juce::String(t.artist);
            e.bpm = t.bpm > 0 ? juce::String(t.bpm, 1) : "-";
            e.key = t.camelotKey.empty() ? (t.key.empty() ? juce::String("-") : juce::String(t.key))
                                         : juce::String(t.camelotKey);
            e.genre = juce::String(t.genre);
            e.filePath = t.filePath;
            loadTrackEntry(e);
            return;
        }
    }
    if (!m_provider)
        return;
    auto track = m_provider->getTrack(id);
    if (track.id != id || track.filePath.empty())
        return;
    TrackEntry entry;
    entry.id = track.id;
    entry.title = juce::String(track.title);
    entry.artist = juce::String(track.artist);
    entry.bpm = track.bpm > 0 ? juce::String(track.bpm, 1) : "-";
    entry.key = track.camelotKey.empty()
                    ? (track.key.empty() ? juce::String("-") : juce::String(track.key))
                    : juce::String(track.camelotKey);
    entry.genre = juce::String(track.genre);
    entry.filePath = track.filePath;
    loadTrackEntry(entry);
}

void HotCueView::loadTrackEntry(const TrackEntry& entry)
{
    // Skip if same track already loaded (prevents crash from rapid double/triple click)
    if (entry.id == m_currentTrackId && !m_currentTrackPath.empty())
        return;

    m_currentTrackPath = entry.filePath;
    m_currentTrackId = entry.id;
    if (m_waveform)
        m_waveform->setVisible(true);
    repaint();

    if (m_playPauseBtn) {
        bool trialExpired = false;
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            if (auto* license = g_serviceLocator->tryGet<Services::Security::LicenseService>()) {
                trialExpired = (license->getState() == Services::Security::LicenseState::TrialExpired);
            }
        }
        m_playPauseBtn->setEnabled(!trialExpired);
        m_playPauseBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6  ")) + BM_TJ("hotcue.btn.play"));
        m_playPauseBtn->setColour(juce::TextButton::buttonColourId,
            trialExpired ? Colors::bgLighter() : Colors::success());
    }

    if (m_trackTitle)
        m_trackTitle->setText(entry.title, juce::dontSendNotification);
    if (m_trackArtist)
        m_trackArtist->setText(entry.artist, juce::dontSendNotification);

    if (!entry.filePath.empty())
    {
        auto filePath = entry.filePath;

        if (onLoadTrack)
            onLoadTrack(juce::String(filePath));

        bool waveformShown = false;
        if (m_waveform)
        {
            if (Core::RgbPeaksGenerator::isCacheValid(filePath))
            {
                Core::RgbPeaksData data;
                if (Core::RgbPeaksGenerator::read(Core::RgbPeaksGenerator::cacheFileFor(filePath), data))
                {
                    m_trackDuration = data.duration;
                    m_waveform->setWaveformData(data.peaks);
                    m_waveform->setColoredWaveformData(data.bass, data.mid, data.treble);
                    m_waveform->setDuration(data.duration);
                    m_waveform->setCuePoints({});
                    m_waveform->repaint();
                    waveformShown = true;
                }
            }

            if (!waveformShown)
            {
                const juce::String legacyKey = juce::String::toHexString(juce::String(filePath).hashCode64());
                for (const char* ext : { ".rgbpeaks2", ".rgbpeaks" })
                {
                    juce::File legacy = Core::RgbPeaksGenerator::cacheDirectory().getChildFile(legacyKey + ext);
                    if (!legacy.existsAsFile())
                        continue;
                    Core::RgbPeaksData legacyData;
                    if (Core::RgbPeaksGenerator::read(legacy, legacyData) && legacyData.valid())
                    {
                        m_trackDuration = legacyData.duration;
                        m_waveform->setWaveformData(legacyData.peaks);
                        m_waveform->setColoredWaveformData(legacyData.bass, legacyData.mid, legacyData.treble);
                        m_waveform->setDuration(legacyData.duration);
                        m_waveform->setCuePoints({});
                        m_waveform->repaint();
                        waveformShown = true;
                        break;
                    }
                }
            }

            if (!waveformShown)
            {
                juce::File monoCache = Core::RgbPeaksGenerator::cacheDirectory().getChildFile(
                    juce::String::toHexString(juce::String(filePath).hashCode64()) + ".peaks");
                if (monoCache.existsAsFile() && monoCache.getSize() > 20000)
                {
                    juce::FileInputStream fis(monoCache);
                    if (fis.openedOk()) {
                        double dur = 0.0;
                        fis.read(&dur, sizeof(double));
                        int numPeaks = static_cast<int>(
                            (fis.getTotalLength() - 8) / sizeof(float));
                        std::vector<float> peaks(static_cast<size_t>(numPeaks));
                        fis.read(peaks.data(), numPeaks * sizeof(float));
                        if (dur > 0.0) {
                            m_trackDuration = dur;
                            m_waveform->setDuration(dur);
                        }
                        m_waveform->setWaveformData(peaks);
                        m_waveform->setCuePoints({});
                        m_waveform->repaint();
                        waveformShown = true;
                    }
                }
            }

            if (!waveformShown) {
                m_waveform->setWaveformData(std::vector<float>(200, 0.02f));
                m_waveform->setCuePoints({});
                m_waveform->repaint();
            }
        }

        const bool needsGeneration = !Core::RgbPeaksGenerator::isCacheValid(filePath);
        const bool suppressPartials = waveformShown;
        auto alive = m_aliveFlag;
        juce::MessageManager::callAsync([this, filePath, alive, needsGeneration, suppressPartials]() {
            if (!alive || !alive->load()) return;

            if (needsGeneration)
                requestWaveformFromService(filePath, suppressPartials);
        });
    }

    if (m_provider)
    {
        auto cues = m_provider->getCuePoints(entry.id);

        for (int i = 0; i < 8; ++i)
        {
            m_cues[static_cast<size_t>(i)].active = false;
            m_cues[static_cast<size_t>(i)].positionSeconds = 0.0;
            updateCueButton(i);
        }

        for (auto& cue : cues)
        {
            if (cue.number >= 1 && cue.number <= 8)
            {
                int idx = cue.number - 1;
                auto& c = m_cues[static_cast<size_t>(idx)];
                c.active = true;
                c.positionSeconds = cue.position;
                c.name = juce::String(cue.name);
                c.color = CueColors::kPalette[idx % 16];
                if (!cue.color.empty())
                {
                    const juce::Colour parsed = juce::Colour::fromString(juce::String(cue.color));
                    if (parsed.getAlpha() > 0 && parsed.getBrightness() > 0.12f)
                        c.color = parsed;
                    else
                        spdlog::warn("[HotCue] cue {} colour invalide en DB ('{}') -> palette",
                                     cue.number, cue.color);
                }
                if (m_cueRows[static_cast<size_t>(idx)].nameEditor)
                    m_cueRows[static_cast<size_t>(idx)].nameEditor->setText(c.name, juce::dontSendNotification);
                updateCueButton(idx);
            }
        }
    }

    updateWaveformCueMarkers();
    loadBeatGridForCurrentTrack();

    m_isPlaying = false;
    m_previewingCue = -1;
    m_holdPreview = false;
    m_wasPlayingBeforeHold = false;
    m_lastPolledPos = -1.0;
    m_loadWallTime = juce::Time::getMillisecondCounterHiRes();
    m_advanceLogged = false;
    selectCue(-1);
    if (m_padGrid)
    {
        m_padGrid->setHoldIndex(-1);
        m_padGrid->setPreviewingIndex(-1);
    }
    m_playheadPosition = 0.0;
    if (m_playPauseBtn)
        m_playPauseBtn->setButtonText(BM_TJ("hotcue.btn.playShort"));
    if (m_waveform)
    {
        m_waveform->setPlayheadPosition(0.0);
        m_waveform->setPlaying(false);
    }
    if (m_positionLabel)
        m_positionLabel->setText("00:00.000", juce::dontSendNotification);

    startTimerHz(20);
}


void HotCueView::setupUI()
{
    // Enable keyboard focus so key shortcuts (1-8, Delete, Space) work
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);

    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("hotcue.title"));
    m_titleLabel->setFont(juce::Font(20.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);


    m_searchEditor = std::make_unique<juce::TextEditor>("search");
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("hotcue.searchPH"), Colors::textMuted());
    m_searchEditor->setFont(juce::Font(12.0f));
    m_searchEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_searchEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_searchEditor->onTextChange = [this] {
        filterTrackList();
    };
    addAndMakeVisible(*m_searchEditor);

    m_genreFilter = std::make_unique<juce::ComboBox>();
    m_genreFilter->setTextWhenNothingSelected(BM_TJ("hotcue.genre.all"));
    m_genreFilter->addItem(BM_TJ("common.all"), 1);
    m_genreFilter->setSelectedId(1, juce::dontSendNotification);
    m_genreFilter->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_genreFilter->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_genreFilter->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_genreFilter->onChange = [this] {
        Prefs::setString("hotcue.genreFilter", m_genreFilter->getText().toStdString());
        filterTrackList();
    };
    addAndMakeVisible(*m_genreFilter);

    m_cueStatusFilter = std::make_unique<juce::ComboBox>();
    m_cueStatusFilter->addItem(BM_TJ("common.all"), 1);
    m_cueStatusFilter->addItem(BM_TJ("hotcue.status.withoutCues"), 2);
    m_cueStatusFilter->addItem(BM_TJ("hotcue.status.withCues"), 3);
    m_cueStatusFilter->setSelectedId(1, juce::dontSendNotification);
    m_cueStatusFilter->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_cueStatusFilter->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_cueStatusFilter->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_cueStatusFilter->onChange = [this] {
        Prefs::setInt("hotcue.cueStatusFilterId", m_cueStatusFilter->getSelectedId());
        filterTrackList();
    };
    addAndMakeVisible(*m_cueStatusFilter);

    m_trackListModel = std::make_unique<TrackListModel>();
    m_trackListModel->onRowSelected = [this](int row) {
        loadTrackByIndex(row);
    };

    m_trackListBox = std::make_unique<juce::ListBox>("trackList", m_trackListModel.get());
    m_trackListBox->setRowHeight(24);
    m_trackListBox->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_trackListBox->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_trackListBox->setOutlineThickness(1);
    addAndMakeVisible(*m_trackListBox);

    m_folderTree = std::make_unique<juce::TreeView>("folderTree");
    m_folderTree->setColour(juce::TreeView::backgroundColourId, Colors::bgDarker());
    m_folderTree->setColour(juce::TreeView::linesColourId, Colors::border().withAlpha(0.3f));
    m_folderTree->setDefaultOpenness(false);
    m_folderTree->setIndentSize(14);
    m_folderTree->setMultiSelectEnabled(false);
    addChildComponent(*m_folderTree); // hidden by default, not addAndMakeVisible

    m_tabListBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.tab.list"));
    m_tabListBtn->setColour(juce::TextButton::buttonColourId, Colors::primary().withAlpha(0.3f));
    m_tabListBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_tabListBtn->onClick = [this] { switchBrowserTab(false); };
    addAndMakeVisible(*m_tabListBtn);

    m_tabFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.tab.folders"));
    m_tabFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_tabFolderBtn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    m_tabFolderBtn->onClick = [this] { switchBrowserTab(true); };
    addAndMakeVisible(*m_tabFolderBtn);

    m_libraryPopupBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.libraryPopup"));
    m_libraryPopupBtn->setColour(juce::TextButton::buttonColourId, Colors::secondary().withAlpha(0.6f));
    m_libraryPopupBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_libraryPopupBtn->setTooltip(BM_TJ("hotcue.libraryPopupTip"));
    m_libraryPopupBtn->onClick = [this] { toggleLibraryMirror(); };
    addAndMakeVisible(*m_libraryPopupBtn);


    m_playPauseBtn = std::make_unique<juce::TextButton>(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6  ")) + BM_TJ("hotcue.btn.play"));
    m_playPauseBtn->setColour(juce::TextButton::buttonColourId, Colors::success());
    m_playPauseBtn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_playPauseBtn->onClick = [this] {
        if (m_currentTrackPath.empty()) return;
        if (m_trialLimitReached) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("hotcue.trialAlert"), BM_TJ("hotcue.trialAlertMsg"));
            return;
        }
        m_isPlaying = !m_isPlaying;
        spdlog::info("[HotCue] playPause -> {} (playhead={:.3f} lastPos={:.2f}s)",
                     m_isPlaying ? "PLAY" : "PAUSE", m_playheadPosition, m_lastPolledPos);
        m_playPauseBtn->setButtonText(m_isPlaying
            ? juce::String(juce::CharPointer_UTF8("\xe2\x8f\xb8  ")) + BM_TJ("hotcue.btn.pause")
            : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6  ")) + BM_TJ("hotcue.btn.play"));
        m_playPauseBtn->setColour(juce::TextButton::buttonColourId,
            m_isPlaying ? Colors::warning() : Colors::success());
        if (m_waveform)
            m_waveform->setPlaying(m_isPlaying);
        if (m_isPlaying)
            startTimerHz(20);
        else
            centerCueInView();
        if (onPlayPause)
            onPlayPause();
    };
    addAndMakeVisible(*m_playPauseBtn);

    m_trackTitle = std::make_unique<juce::Label>("tt", "");
    m_trackTitle->setFont(juce::Font(14.0f, juce::Font::bold));
    m_trackTitle->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_trackTitle);

    m_trackArtist = std::make_unique<juce::Label>("ta", "");
    m_trackArtist->setFont(juce::Font(12.0f));
    m_trackArtist->setColour(juce::Label::textColourId, Colors::textMuted());
    addAndMakeVisible(*m_trackArtist);

    m_waveform = std::make_unique<WaveformWidget>();
    m_waveformClickForwarder = std::make_unique<WaveformClickForwarder>(*this);
    m_waveform->addListener(m_waveformClickForwarder.get());
    addChildComponent(*m_waveform);

    m_zoomLabel = std::make_unique<juce::Label>("zl", BM_TJ("hotcue.zoom"));
    m_zoomLabel->setFont(juce::Font(12.0f));
    m_zoomLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_zoomLabel);

    m_zoomSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_zoomSlider->setRange(1.0, 100.0, 0.5);
    m_zoomSlider->setValue(50.0);
    m_zoomSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_zoomSlider->setTextValueSuffix("x");
    m_zoomSlider->setTooltip(BM_TJ("hotcue.settings.zoomTip"));
    m_zoomSlider->onValueChange = [this] {
        if (m_waveform)
            m_waveform->setZoom(m_zoomSlider->getValue());
        Prefs::setDouble("hotcue.zoom", m_zoomSlider->getValue());
    };
    addAndMakeVisible(*m_zoomSlider);

    m_waveformScrollBar = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::NoTextBox);
    m_waveformScrollBar->setRange(0.0, 1.0, 0.001);
    m_waveformScrollBar->setValue(0.0);
    m_waveformScrollBar->setColour(juce::Slider::trackColourId, Colors::primary());
    m_waveformScrollBar->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_waveformScrollBar->onValueChange = [this]() {
        if (onGetDuration && onSeek) {
            double duration = onGetDuration();
            if (duration > 0.0) {
                onSeek(m_waveformScrollBar->getValue() * duration);
            }
        }
    };
    addAndMakeVisible(*m_waveformScrollBar);

    m_positionLabel = std::make_unique<juce::Label>("pos", "00:00.000");
    m_positionLabel->setFont(juce::Font("Consolas", 22.0f, juce::Font::bold));
    m_positionLabel->setColour(juce::Label::textColourId, Colors::primary());
    m_positionLabel->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*m_positionLabel);

    m_quantizeBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.quantize"));
    m_quantizeBtn->setClickingTogglesState(true);
    m_quantizeBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_quantizeBtn->setColour(juce::TextButton::buttonOnColourId, Colors::primary().withAlpha(0.5f));
    m_quantizeBtn->setColour(juce::TextButton::textColourOffId, Colors::textSecondary());
    m_quantizeBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    m_quantizeBtn->setTooltip(BM_TJ("hotcue.quantizeTip"));
    m_quantizeBtn->setMouseClickGrabsKeyboardFocus(false);
    m_quantizeEnabled = Prefs::getBool("hotcue.quantize", true);
    m_quantizeBtn->setToggleState(m_quantizeEnabled, juce::dontSendNotification);
    m_quantizeBtn->onClick = [this] { setQuantizeEnabled(m_quantizeBtn->getToggleState()); };
    addAndMakeVisible(*m_quantizeBtn);

    m_padGrid = std::make_unique<CuePadGrid>();
    m_padGrid->onPadPress = [this](int idx) { onPadPress(idx); };
    m_padGrid->onPadRelease = [this](int idx) { onPadRelease(idx); };
    m_padGrid->onPadContextMenu = [this](int idx) { showCueEditPopover(idx); };
    addAndMakeVisible(*m_padGrid);

    const char* nudgeKeys[4] = { "hotcue.nudge.backBeat", "hotcue.nudge.back10",
                                 "hotcue.nudge.fwd10", "hotcue.nudge.fwdBeat" };
    for (int n = 0; n < 4; ++n)
    {
        auto& btn = m_nudgeBtns[static_cast<size_t>(n)];
        btn = std::make_unique<juce::TextButton>(BM_TJ(nudgeKeys[n]));
        btn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        btn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        btn->setTooltip(BM_TJ("hotcue.nudgeTip"));
        btn->setMouseClickGrabsKeyboardFocus(false);
        btn->setEnabled(false);
        const int nudgeIdx = n;
        btn->onClick = [this, nudgeIdx] {
            if (m_selectedCue < 0) return;
            double delta = 0.0;
            const double beat = m_trackBpm > 0.0 ? 60.0 / m_trackBpm : 0.0;
            if (nudgeIdx == 0) delta = -beat;
            else if (nudgeIdx == 1) delta = -0.010;
            else if (nudgeIdx == 2) delta = 0.010;
            else delta = beat;
            if (delta != 0.0)
                nudgeCue(m_selectedCue, delta);
        };
        addAndMakeVisible(*btn);
    }

    for (int i = 0; i < 8; ++i)
    {
        auto& row = m_cueRows[static_cast<size_t>(i)];
        auto& cue = m_cues[static_cast<size_t>(i)];
        int idx = i;

        row.nameEditor = std::make_unique<juce::TextEditor>("ne" + juce::String(i));
        row.nameEditor->setText(cue.name, juce::dontSendNotification);
        row.nameEditor->setFont(juce::Font(11.0f));
        row.nameEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
        row.nameEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
        row.nameEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
        row.nameEditor->setJustification(juce::Justification::centredLeft);
        row.nameEditor->onReturnKey = [this, idx] {
            renameCue(idx, m_cueRows[static_cast<size_t>(idx)].nameEditor->getText());
        };
        row.nameEditor->onFocusLost = [this, idx] {
            renameCue(idx, m_cueRows[static_cast<size_t>(idx)].nameEditor->getText());
        };
        addAndMakeVisible(*row.nameEditor);

        row.positionLabel = std::make_unique<juce::Label>("cp" + juce::String(i), "--:--.---");
        row.positionLabel->setFont(juce::Font("Consolas", 10.0f, juce::Font::plain));
        row.positionLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
        row.positionLabel->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*row.positionLabel);

        updateCueButton(i);
    }

    auto makeBtn = [this](const juce::String& t, juce::Colour bg = Colors::bgLighter()) {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*b);
        return b;
    };

    m_autoGenerateBtn = makeBtn(BM_TJ("hotcue.autoGenerate"), Colors::primary());
    m_autoGenerateBtn->onClick = [this] {
        spdlog::info("[HotCueView] autoGenerate clicked");
        if (m_trackDuration <= 0.0 || m_currentTrackPath.empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("hotcue.autoGen"), BM_TJ("hotcue.loadFirst"));
            return;
        }
        if (m_autoGenThread && m_autoGenThread->isThreadRunning())
            return;

        m_autoGenerateBtn->setButtonText(BM_TJ("hotcue.analyzing"));
        m_autoGenerateBtn->setEnabled(false);

        m_autoGenThread = std::make_unique<AutoGenerateThread>(*this, m_currentTrackPath);
        m_autoGenThread->startThread();
    };

    m_clearAllBtn = makeBtn(BM_TJ("hotcue.clearAll"), Colors::error().withAlpha(0.3f));
    m_clearAllBtn->onClick = [this] {
        spdlog::info("[HotCueView] clearAll clicked");
        for (int i = 0; i < 8; ++i)
            removeCue(i);
        m_listeners.call(&Listener::clearAllCuesRequested);
    };

    m_exportTargetCombo = std::make_unique<juce::ComboBox>();
    juce::StringArray targets = {"Rekordbox", "Serato", "Traktor", "VirtualDJ", "Engine DJ"};
    for (int i = 0; i < targets.size(); ++i)
        m_exportTargetCombo->addItem(targets[i], i + 1);
    m_exportTargetCombo->setSelectedId(1);
    m_exportTargetCombo->setTooltip(BM_TJ("hotcue.settings.exportTargetTip"));
    m_exportTargetCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_exportTargetCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_exportTargetCombo->onChange = [this] { Prefs::setInt("hotcue.exportTargetId", m_exportTargetCombo->getSelectedId()); };
    addAndMakeVisible(*m_exportTargetCombo);

    m_exportBtn = makeBtn(BM_TJ("hotcue.exportDj"), Colors::secondary());
    m_exportBtn->onClick = [this] {
        spdlog::info("[HotCueView] exportCues clicked: target={}", m_exportTargetCombo->getText().toStdString());
        // Single export path: the MainWindow listener uses the real exporters
        int activeCount = 0;
        for (int i = 0; i < 8; ++i) if (m_cues[static_cast<size_t>(i)].active) ++activeCount;
        if (activeCount == 0) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("hotcue.exportCues"),
                juce::String::fromUTF8("Aucun cue point actif à exporter."));
            return;
        }
        m_listeners.call([this](Listener& l) {
            l.exportCuesRequested(m_exportTargetCombo->getText());
        });
    };

    updateWaveformCueMarkers();

    // Zoom always opens at 50x so cues can be placed precisely right away
    {
        const double zoom = 50.0;
        if (m_zoomSlider) m_zoomSlider->setValue(zoom, juce::dontSendNotification);
        if (m_waveform) m_waveform->setZoom(zoom);
        const int targetId = Prefs::getInt("hotcue.exportTargetId", 1);
        if (m_exportTargetCombo && targetId >= 1 && targetId <= m_exportTargetCombo->getNumItems())
            m_exportTargetCombo->setSelectedId(targetId, juce::dontSendNotification);
        const int cueStatusId = Prefs::getInt("hotcue.cueStatusFilterId", 1);
        if (m_cueStatusFilter && cueStatusId >= 1 && cueStatusId <= m_cueStatusFilter->getNumItems())
            m_cueStatusFilter->setSelectedId(cueStatusId, juce::dontSendNotification);
    }
}

void HotCueView::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(BM_TJ("hotcue.title"), juce::dontSendNotification);

    if (m_searchEditor)
        m_searchEditor->setTextToShowWhenEmpty(BM_TJ("hotcue.searchPH"), Colors::textMuted());

    if (m_genreFilter)
    {
        m_genreFilter->setTextWhenNothingSelected(BM_TJ("hotcue.genre.all"));
        m_genreFilter->changeItemText(1, BM_TJ("common.all"));
    }

    if (m_cueStatusFilter)
    {
        int prev = m_cueStatusFilter->getSelectedId();
        m_cueStatusFilter->clear(juce::dontSendNotification);
        m_cueStatusFilter->addItem(BM_TJ("common.all"), 1);
        m_cueStatusFilter->addItem(BM_TJ("hotcue.status.withoutCues"), 2);
        m_cueStatusFilter->addItem(BM_TJ("hotcue.status.withCues"), 3);
        if (prev > 0)
            m_cueStatusFilter->setSelectedId(prev, juce::dontSendNotification);
    }

    if (m_tabListBtn)
        m_tabListBtn->setButtonText(BM_TJ("hotcue.tab.list"));
    if (m_tabFolderBtn)
        m_tabFolderBtn->setButtonText(BM_TJ("hotcue.tab.folders"));

    if (m_playPauseBtn)
    {
        if (m_trialLimitReached)
            m_playPauseBtn->setButtonText(BM_TJ("hotcue.btn.trialEnded"));
        else if (m_isPlaying)
            m_playPauseBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x8f\xb8  ")) + BM_TJ("hotcue.btn.pause"));
        else
            m_playPauseBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6  ")) + BM_TJ("hotcue.btn.play"));
    }

    if (m_zoomLabel)
        m_zoomLabel->setText(BM_TJ("hotcue.zoom"), juce::dontSendNotification);
    if (m_zoomSlider)
        m_zoomSlider->setTooltip(BM_TJ("hotcue.settings.zoomTip"));

    if (m_quantizeBtn)
    {
        m_quantizeBtn->setButtonText(BM_TJ("hotcue.quantize"));
        m_quantizeBtn->setTooltip(BM_TJ("hotcue.quantizeTip"));
    }

    {
        const char* nudgeKeys[4] = { "hotcue.nudge.backBeat", "hotcue.nudge.back10",
                                     "hotcue.nudge.fwd10", "hotcue.nudge.fwdBeat" };
        for (int n = 0; n < 4; ++n)
            if (m_nudgeBtns[static_cast<size_t>(n)])
            {
                m_nudgeBtns[static_cast<size_t>(n)]->setButtonText(BM_TJ(nudgeKeys[n]));
                m_nudgeBtns[static_cast<size_t>(n)]->setTooltip(BM_TJ("hotcue.nudgeTip"));
            }
    }

    if (m_autoGenerateBtn)
        m_autoGenerateBtn->setButtonText(
            (m_autoGenThread && m_autoGenThread->isThreadRunning())
                ? BM_TJ("hotcue.analyzing")
                : BM_TJ("hotcue.autoGenerate"));

    if (m_clearAllBtn)
        m_clearAllBtn->setButtonText(BM_TJ("hotcue.clearAll"));

    if (m_exportTargetCombo)
        m_exportTargetCombo->setTooltip(BM_TJ("hotcue.settings.exportTargetTip"));

    if (m_exportBtn)
        m_exportBtn->setButtonText(BM_TJ("hotcue.exportDj"));

    repaint();
}


void HotCueView::setCueAtPosition(int index, double positionSeconds)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    c.active = true;
    c.positionSeconds = snapToNearestBeat(positionSeconds);
    updateCueButton(index);
    updateWaveformCueMarkers();
    selectCue(index);
    m_listeners.call([index, &c](Listener& l) { l.cuePointSet(index + 1, c.positionSeconds); });
    persistCuesToDatabase();

    if (m_currentTrackId > 0)
        m_cueStatusCache.insert(m_currentTrackId);
    if (m_showFolderTree)
        buildFolderTree();
}

void HotCueView::removeCue(int index)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    c.active = false;
    c.positionSeconds = 0.0;
    updateCueButton(index);
    updateWaveformCueMarkers();
    if (m_previewingCue == index)
    {
        m_previewingCue = -1;
        m_holdPreview = false;
        if (m_padGrid)
        {
            m_padGrid->setHoldIndex(-1);
            m_padGrid->setPreviewingIndex(-1);
        }
    }
    updateNudgeButtons();
    m_listeners.call([index](Listener& l) { l.cuePointRemoved(index + 1); });
    persistCuesToDatabase();

    if (m_currentTrackId > 0) {
        bool anyActive = false;
        for (int i = 0; i < 8; ++i) {
            if (m_cues[static_cast<size_t>(i)].active) { anyActive = true; break; }
        }
        if (!anyActive)
            m_cueStatusCache.erase(m_currentTrackId);
    }
    if (m_showFolderTree)
        buildFolderTree();
}

void HotCueView::updateCueButton(int index)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    auto& row = m_cueRows[static_cast<size_t>(index)];

    PadInfo info;
    info.active = c.active;
    info.color = c.color;
    info.name = c.name;
    info.timeText = c.active
        ? formatTime(c.positionSeconds).upToFirstOccurrenceOf(".", false, false)
        : juce::String();
    if (m_padGrid)
        m_padGrid->refreshPad(index, info);

    if (row.positionLabel)
    {
        if (c.active)
        {
            row.positionLabel->setText(formatTime(c.positionSeconds), juce::dontSendNotification);
            row.positionLabel->setColour(juce::Label::textColourId, c.color.brighter(0.3f));
        }
        else
        {
            row.positionLabel->setText("--:--.---", juce::dontSendNotification);
            row.positionLabel->setColour(juce::Label::textColourId, Colors::textDim());
        }
    }
}

void HotCueView::selectCue(int index)
{
    m_selectedCue = (index >= 0 && index < 8) ? index : -1;
    if (m_padGrid)
        m_padGrid->setSelectedIndex(m_selectedCue);
    updateNudgeButtons();
}

void HotCueView::updateNudgeButtons()
{
    const bool hasSel = m_selectedCue >= 0 && m_selectedCue < 8
                        && m_cues[static_cast<size_t>(m_selectedCue)].active;
    const bool hasBeat = hasSel && m_trackBpm > 0.0;
    if (m_nudgeBtns[0]) m_nudgeBtns[0]->setEnabled(hasBeat);
    if (m_nudgeBtns[1]) m_nudgeBtns[1]->setEnabled(hasSel);
    if (m_nudgeBtns[2]) m_nudgeBtns[2]->setEnabled(hasSel);
    if (m_nudgeBtns[3]) m_nudgeBtns[3]->setEnabled(hasBeat);
}

void HotCueView::setQuantizeEnabled(bool enabled)
{
    m_quantizeEnabled = enabled;
    Prefs::setBool("hotcue.quantize", enabled);
    if (m_quantizeBtn && m_quantizeBtn->getToggleState() != enabled)
        m_quantizeBtn->setToggleState(enabled, juce::dontSendNotification);
}

void HotCueView::centerCueInView()
{
    if (!m_waveform || m_trackDuration <= 0.0 || m_waveform->zoom() <= 1.0)
        return;

    const double vis = 1.0 / m_waveform->zoom();
    const double playheadSec = m_playheadPosition * m_trackDuration;
    const double windowSec = vis * m_trackDuration;

    int idx = -1;
    double best = windowSec * 0.5;
    for (int i = 0; i < 8; ++i)
    {
        if (!m_cues[static_cast<size_t>(i)].active)
            continue;
        const double d = std::abs(m_cues[static_cast<size_t>(i)].positionSeconds - playheadSec);
        if (d < best)
        {
            best = d;
            idx = i;
        }
    }

    double centerSec = playheadSec;
    if (idx >= 0)
    {
        centerSec = m_cues[static_cast<size_t>(idx)].positionSeconds;
        selectCue(idx);
    }
    m_waveform->setScrollOffset(centerSec / m_trackDuration - vis * 0.5);
}

void HotCueView::nudgeCue(int index, double deltaSeconds)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    if (!c.active || m_trackDuration <= 0.0) return;

    c.positionSeconds = juce::jlimit(0.0, m_trackDuration, c.positionSeconds + deltaSeconds);
    updateCueButton(index);
    updateWaveformCueMarkers();
    m_listeners.call([index, &c](Listener& l) { l.cuePointSet(index + 1, c.positionSeconds); });
    persistCuesToDatabase();

    if (m_waveform && m_waveform->zoom() > 1.0)
    {
        const double vis = 1.0 / m_waveform->zoom();
        m_waveform->setScrollOffset(c.positionSeconds / m_trackDuration - vis * 0.5);
    }
}

void HotCueView::renameCue(int index, const juce::String& name)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    if (c.name == name) return;
    c.name = name;
    if (m_cueRows[static_cast<size_t>(index)].nameEditor
        && m_cueRows[static_cast<size_t>(index)].nameEditor->getText() != name)
        m_cueRows[static_cast<size_t>(index)].nameEditor->setText(name, juce::dontSendNotification);
    updateCueButton(index);
    updateWaveformCueMarkers();
    m_listeners.call([index, &c](Listener& l) { l.cuePointRenamed(index + 1, c.name); });
    persistCuesToDatabase();
}

void HotCueView::applyCueColor(int index, int paletteIndex)
{
    if (index < 0 || index >= 8 || paletteIndex < 0 || paletteIndex >= 16) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    c.color = CueColors::kPalette[paletteIndex];
    c.colorIndex = paletteIndex;
    updateCueButton(index);
    updateWaveformCueMarkers();
    m_listeners.call([index, &c](Listener& l) { l.cuePointColorChanged(index + 1, c.color); });
    persistCuesToDatabase();
}

void HotCueView::snapCueToTransient(int index)
{
    if (index < 0 || index >= 8) return;
    auto& c = m_cues[static_cast<size_t>(index)];
    if (!c.active || m_currentTrackPath.empty()) return;

    const std::string path = m_currentTrackPath;
    const double cuePos = c.positionSeconds;
    const double windowStart = std::max(0.0, cuePos - 0.2);
    const double windowLen = 0.4;
    auto alive = m_aliveFlag;

    std::thread([this, alive, path, index, cuePos, windowStart, windowLen]() {
        Core::AudioFileReader reader;
        auto range = reader.readRange(path, windowStart, windowLen);
        if (!range || !alive->load()) return;
        Core::UltraPreciseHotcueService service;
        auto precise = service.snapToTransient(*range, cuePos - windowStart, 60.0);
        const double newPos = windowStart + precise.sampleAccuratePosition;
        juce::MessageManager::callAsync([this, alive, index, newPos, path]() {
            if (!alive->load() || m_currentTrackPath != path) return;
            auto& cue = m_cues[static_cast<size_t>(index)];
            if (!cue.active) return;
            cue.positionSeconds = m_trackDuration > 0.0
                ? juce::jlimit(0.0, m_trackDuration, newPos)
                : std::max(0.0, newPos);
            updateCueButton(index);
            updateWaveformCueMarkers();
            m_listeners.call([index, p = cue.positionSeconds](Listener& l) { l.cuePointSet(index + 1, p); });
            persistCuesToDatabase();
        });
    }).detach();
}

void HotCueView::onPadPress(int index)
{
    if (index < 0 || index >= 8) return;
    selectCue(index);
    auto& c = m_cues[static_cast<size_t>(index)];

    if (c.active)
    {
        if (m_trialLimitReached || !onPreviewCue) return;
        onPreviewCue(c.positionSeconds);
        m_isPlaying = true;
        if (m_trackDuration > 0.0)
        {
            m_playheadPosition = c.positionSeconds / m_trackDuration;
            if (m_waveform)
                m_waveform->setPlayheadPosition(m_playheadPosition);
        }
        if (m_positionLabel)
            m_positionLabel->setText(formatTime(c.positionSeconds), juce::dontSendNotification);
        if (m_waveform)
            m_waveform->setPlaying(true);
        if (m_playPauseBtn)
        {
            m_playPauseBtn->setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x8f\xb8  ")) + BM_TJ("hotcue.btn.pause"));
            m_playPauseBtn->setColour(juce::TextButton::buttonColourId, Colors::warning());
        }
        if (m_padGrid)
            m_padGrid->setPreviewingIndex(index);
        startTimerHz(20);
    }
    else
    {
        double posSec = 0.0;
        if (onGetPosition)
            posSec = onGetPosition();
        else if (m_trackDuration > 0.0)
            posSec = m_playheadPosition * m_trackDuration;

        if (m_trackDuration > 0.0 && posSec >= 0.0)
            setCueAtPosition(index, posSec);
    }
}

void HotCueView::onPadRelease(int index)
{
    juce::ignoreUnused(index);
    if (m_padGrid)
    {
        m_padGrid->setHoldIndex(-1);
        m_padGrid->setPreviewingIndex(-1);
    }
}

void HotCueView::showCueEditPopover(int index)
{
    if (index < 0 || index >= 8 || !m_padGrid) return;
    showCueEditPopover(index, m_padGrid->padScreenBounds(index));
}

void HotCueView::showCueEditPopover(int index, juce::Rectangle<int> screenAnchor)
{
    if (index < 0 || index >= 8) return;
    spdlog::info("[HotCue] edit popover open (cue {}, anchor {},{})",
                 index, screenAnchor.getX(), screenAnchor.getY());
    selectCue(index);
    auto& c = m_cues[static_cast<size_t>(index)];
    if (!c.active) return;

    auto content = std::make_unique<CueEditPopover>(index, c.name, c.colorIndex,
                                                    !m_currentTrackPath.empty());
    juce::Component::SafePointer<HotCueView> self(this);
    content->onRename = [self](int i, const juce::String& name) {
        if (auto* v = self.getComponent()) v->renameCue(i, name);
    };
    content->onColorPicked = [self](int i, int colorIdx) {
        if (auto* v = self.getComponent()) v->applyCueColor(i, colorIdx);
    };
    content->onSnapTransient = [self](int i) {
        if (auto* v = self.getComponent()) v->snapCueToTransient(i);
    };
    content->onDelete = [self](int i) {
        if (auto* v = self.getComponent()) v->removeCue(i);
    };

    juce::CallOutBox::launchAsynchronously(std::move(content), screenAnchor, nullptr);
}

void HotCueView::loadBeatGridForCurrentTrack()
{
    m_beatGridPositions.clear();
    m_trackBpm = 0.0;

    Models::Track track;
    bool found = false;
    for (const auto& t : m_allTracksCache)
    {
        if (t.id == m_currentTrackId)
        {
            track = t;
            found = true;
            break;
        }
    }
    if (!found && m_provider && m_currentTrackId > 0)
    {
        track = m_provider->getTrack(m_currentTrackId);
        found = track.id == m_currentTrackId;
    }

    if (found && track.bpm > 0.0)
    {
        m_trackBpm = track.bpm;
        if (track.duration <= 0.0 && m_trackDuration > 0.0)
            track.duration = m_trackDuration;
        Core::BeatGridGenerator generator;
        auto result = generator.generateForTrack(Core::BeatGridMode::Fixed, track);
        if (result.ok)
            m_beatGridPositions = result.grid.beatPositions;
    }

    pushBeatGridToWaveform();
    updateNudgeButtons();
}

void HotCueView::pushBeatGridToWaveform()
{
    if (!m_waveform) return;
    if (m_trackDuration > 0.0 && !m_beatGridPositions.empty())
    {
        std::vector<double> normalized;
        normalized.reserve(m_beatGridPositions.size());
        for (double b : m_beatGridPositions)
            normalized.push_back(b / m_trackDuration);
        m_waveform->setBeatGrid(normalized);
    }
    else
    {
        m_waveform->setBeatGrid({});
    }
}

void HotCueView::requestWaveformFromService(const std::string& filePath, bool suppressPartials)
{
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (!g_serviceLocator) return;
    auto* service = g_serviceLocator->tryGet<Services::Library::WaveformPrecacheService>();
    if (!service) return;

    auto alive = m_aliveFlag;
    Services::Library::WaveformPrecacheService::ProgressCallback progress;
    if (!suppressPartials)
    {
        progress = [this, alive, filePath](const Core::RgbPeaksData& d) {
            if (!alive->load() || m_currentTrackPath != filePath) return;
            if (d.duration > 0.0 && std::abs(m_trackDuration - d.duration) > 0.001)
            {
                m_trackDuration = d.duration;
                if (m_waveform)
                    m_waveform->setDuration(d.duration);
            }
            if (m_waveform && !d.peaks.empty())
            {
                m_waveform->setWaveformData(d.peaks);
                m_waveform->setColoredWaveformData(d.bass, d.mid, d.treble);
            }
        };
    }
    auto done = [this, alive, filePath](const Core::RgbPeaksData& d) {
        if (!alive->load() || m_currentTrackPath != filePath) return;
        if (!d.valid()) return;
        m_trackDuration = d.duration;
        if (m_waveform)
        {
            m_waveform->setWaveformData(d.peaks);
            m_waveform->setColoredWaveformData(d.bass, d.mid, d.treble);
            m_waveform->setDuration(d.duration);
        }
        updateWaveformCueMarkers();
        pushBeatGridToWaveform();
        repaint();
    };
    service->requestPriority(filePath, std::move(progress), std::move(done));
}

double HotCueView::snapToNearestBeat(double positionSeconds) const
{
    if (!m_quantizeEnabled || m_beatGridPositions.empty())
        return positionSeconds;

    auto it = std::lower_bound(m_beatGridPositions.begin(), m_beatGridPositions.end(), positionSeconds);

    double closest = positionSeconds;
    double minDist = std::numeric_limits<double>::max();

    if (it != m_beatGridPositions.end())
    {
        double dist = std::abs(*it - positionSeconds);
        if (dist < minDist) { minDist = dist; closest = *it; }
    }
    if (it != m_beatGridPositions.begin())
    {
        --it;
        double dist = std::abs(*it - positionSeconds);
        if (dist < minDist) { minDist = dist; closest = *it; }
    }

    return closest;
}

juce::String HotCueView::formatTime(double seconds) const
{
    int mins = static_cast<int>(seconds / 60.0);
    double secs = seconds - mins * 60.0;
    int wholeSecs = static_cast<int>(secs);
    int millis = static_cast<int>((secs - wholeSecs) * 1000.0);
    return juce::String::formatted("%02d:%02d.%03d", mins, wholeSecs, millis);
}


void HotCueView::onWaveformClicked(double normalizedPosition)
{
    if (onGetDuration)
    {
        const double realDuration = onGetDuration();
        if (realDuration > 0.0 && std::abs(realDuration - m_trackDuration) > 0.01)
        {
            m_trackDuration = realDuration;
            updateWaveformCueMarkers();
        }
    }
    if (m_trackDuration <= 0.0) return;
    double seconds = normalizedPosition * m_trackDuration;

    // Rekordbox behavior: click on waveform = SEEK (always)
    if (onSeek)
        onSeek(seconds);

    m_playheadPosition = normalizedPosition;
    if (m_waveform)
        m_waveform->setPlayheadPosition(normalizedPosition);

    if (m_positionLabel)
        m_positionLabel->setText(formatTime(seconds), juce::dontSendNotification);

    if (m_isPlaying && !isTimerRunning())
        startTimerHz(20);
}

void HotCueView::updateWaveformCueMarkers()
{
    if (!m_waveform) return;
    std::vector<BeatMate::UI::CuePoint> cueVec;
    for (int i = 0; i < 8; ++i)
    {
        auto& c = m_cues[static_cast<size_t>(i)];
        if (c.active && m_trackDuration > 0.0)
        {
            BeatMate::UI::CuePoint cp;
            cp.number = i + 1;
            cp.position = c.positionSeconds / m_trackDuration; // normalized 0-1
            cp.color = c.color;
            cp.label = c.name;
            cueVec.push_back(cp);
        }
    }
    m_waveform->setCuePoints(cueVec);
}


void HotCueView::setTrack(const juce::String& title, const juce::String& artist)
{
    m_trackTitle->setText(title, juce::dontSendNotification);
    m_trackArtist->setText(artist, juce::dontSendNotification);
}

void HotCueView::setTrackDuration(double durationSeconds)
{
    m_trackDuration = durationSeconds;
    if (m_waveform)
        m_waveform->setDuration(durationSeconds);
    updateWaveformCueMarkers();
    pushBeatGridToWaveform();
}

void HotCueView::setWaveformData(const std::vector<float>& peaks)
{
    if (m_waveform)
        m_waveform->setWaveformData(peaks);
}

void HotCueView::setBeatGrid(const std::vector<double>& beats)
{
    m_beatGridPositions = beats;
    pushBeatGridToWaveform();
}


void HotCueView::timerCallback()
{
    if (onGetPosition && onGetDuration)
    {
        double pos = onGetPosition();
        double duration = onGetDuration();
        if (duration > 0.0)
        {
            if (std::abs(duration - m_trackDuration) > 0.01)
            {
                m_trackDuration = duration;
                if (m_waveform)
                    m_waveform->setDuration(duration);
                updateWaveformCueMarkers();
                pushBeatGridToWaveform();
            }
            double newPlayhead = pos / duration;
            if (std::abs(newPlayhead - m_playheadPosition) > 0.0001) {
                m_playheadPosition = newPlayhead;
                if (m_waveform)
                    m_waveform->setPlayheadPosition(m_playheadPosition);
                if (m_waveformScrollBar)
                    m_waveformScrollBar->setValue(m_playheadPosition, juce::dontSendNotification);
            }

            if (m_positionLabel)
                m_positionLabel->setText(formatTime(pos), juce::dontSendNotification);

            if (m_lastPolledPos >= 0.0 && pos < m_lastPolledPos - 1.0)
                spdlog::warn("[HotCue] position jumped back {:.2f}s -> {:.2f}s (isPlaying={})",
                             m_lastPolledPos, pos, m_isPlaying);

            const bool advancing = m_lastPolledPos >= 0.0
                                   && pos > m_lastPolledPos + 0.02
                                   && pos < m_lastPolledPos + 0.5;
            if (advancing && !m_advanceLogged)
            {
                m_advanceLogged = true;
                spdlog::info("[HotCue] lecture detectee pos={:.2f}s (+{:.0f}ms apres load)",
                             pos, juce::Time::getMillisecondCounterHiRes() - m_loadWallTime);
            }
            if (advancing && !m_isPlaying && m_previewingCue < 0) {
                m_isPlaying = true;
                if (m_playPauseBtn)
                {
                    m_playPauseBtn->setButtonText(BM_TJ("hotcue.btn.pause"));
                    m_playPauseBtn->setColour(juce::TextButton::buttonColourId, Colors::warning());
                }
                if (m_waveform)
                    m_waveform->setPlaying(true);
            }
            m_lastPolledPos = pos;
        }
    }

    if (m_previewingCue >= 0 && !m_holdPreview)
    {
        double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        double elapsed = now - m_previewStartTime;

        if (elapsed >= kPreviewDuration)
        {
            if (onStopPreview)
                onStopPreview();
            m_previewingCue = -1;
            if (m_padGrid)
                m_padGrid->setPreviewingIndex(-1);
            m_isPlaying = false;
            if (m_playPauseBtn) m_playPauseBtn->setButtonText(BM_TJ("hotcue.btn.play"));
            stopTimer();
        }
    }

    if (m_isPlaying && !m_trialLimitReached)
    {
        m_trialPlaybackSeconds += 0.05; // 20fps timer = +50ms per tick

        if (m_trialPlaybackSeconds >= 300.0)
        {
            m_isPlaying = false;
            m_trialLimitReached = true;

            if (m_playPauseBtn) {
                m_playPauseBtn->setButtonText(BM_TJ("hotcue.btn.trialEnded"));
                m_playPauseBtn->setColour(juce::TextButton::buttonColourId, Colors::error());
                m_playPauseBtn->setEnabled(false);
            }
            if (m_waveform) m_waveform->setPlaying(false);
            if (onStopPreview) onStopPreview();

            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                BM_TJ("hotcue.trialAlert"),
                BM_TJ("hotcue.trialAlertMsg"),
                "OK");
        }
    }

}


void HotCueView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (m_waveform && m_waveform->getBounds().contains(e.getPosition()))
    {
        double newZoom = m_zoomSlider->getValue() + w.deltaY * 3.0;
        newZoom = juce::jlimit(1.0, 100.0, newZoom);
        m_zoomSlider->setValue(newZoom);
    }
    else
    {
        Component::mouseWheelMove(e, w);
    }
}


bool HotCueView::keyPressed(const juce::KeyPress& key)
{
    int keyChar = key.getTextCharacter();
    if (keyChar >= '1' && keyChar <= '8')
    {
        int idx = keyChar - '1';
        auto& c = m_cues[static_cast<size_t>(idx)];
        selectCue(idx);
        if (c.active)
        {
            if (onPreviewCue)
            {
                m_previewingCue = idx;
                m_previewStartTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
                if (m_padGrid)
                    m_padGrid->setPreviewingIndex(idx);
                onPreviewCue(c.positionSeconds);
                if (!isTimerRunning())
                    startTimerHz(20);
            }
        }
        else
        {
            double posSec = 0.0;
            if (onGetPosition)
                posSec = onGetPosition();
            else if (m_trackDuration > 0.0)
                posSec = m_playheadPosition * m_trackDuration;

            if (m_trackDuration > 0.0)
                setCueAtPosition(idx, posSec);
        }
        return true;
    }

    if (keyChar == 'q' || keyChar == 'Q')
    {
        setQuantizeEnabled(!m_quantizeEnabled);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::leftKey
        || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        if (m_selectedCue >= 0 && m_cues[static_cast<size_t>(m_selectedCue)].active)
        {
            const bool forward = key.getKeyCode() == juce::KeyPress::rightKey;
            double delta = 0.010;
            if (key.getModifiers().isShiftDown())
            {
                if (m_trackBpm <= 0.0)
                    return true;
                delta = 60.0 / m_trackBpm;
            }
            nudgeCue(m_selectedCue, forward ? delta : -delta);
            return true;
        }
        return false;
    }

    if (key == juce::KeyPress::deleteKey)
    {
        const int target = (m_selectedCue >= 0 && m_cues[static_cast<size_t>(m_selectedCue)].active)
                               ? m_selectedCue
                               : m_previewingCue;
        if (target >= 0)
            removeCue(target);
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (m_playPauseBtn)
            m_playPauseBtn->triggerClick();
        return true;
    }

    return false;
}


void HotCueView::toggleLibraryMirror()
{
    if (m_libraryMirror) {
        m_libraryMirror.reset();
        return;
    }
    juce::Component::SafePointer<HotCueView> self(this);
    m_libraryMirror = std::make_unique<LibraryMirrorWindow>(
        m_provider,
        [self](int64_t trackId) {
            if (auto* v = self.getComponent()) v->loadTrackById(trackId);
        });
    m_libraryMirror->onClosed = [self]() {
        if (auto* v = self.getComponent())
            juce::MessageManager::callAsync([v]() { v->m_libraryMirror.reset(); });
    };
}

void HotCueView::switchBrowserTab(bool showFolders)
{
    m_showFolderTree = showFolders;

    m_tabListBtn->setColour(juce::TextButton::buttonColourId,
        showFolders ? Colors::bgLighter() : Colors::primary().withAlpha(0.3f));
    m_tabListBtn->setColour(juce::TextButton::textColourOffId,
        showFolders ? Colors::textSecondary() : Colors::textPrimary());
    m_tabFolderBtn->setColour(juce::TextButton::buttonColourId,
        showFolders ? Colors::primary().withAlpha(0.3f) : Colors::bgLighter());
    m_tabFolderBtn->setColour(juce::TextButton::textColourOffId,
        showFolders ? Colors::textPrimary() : Colors::textSecondary());

    if (showFolders && !m_rootTreeItem)
        buildFolderTree();

    resized();
}


void HotCueView::paint(juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();
    int M = 12;
    float wf = static_cast<float>(w);
    float hf = static_cast<float>(h);

    ProDraw::viewBackground(g, w, h);

    int topH = 44;
    {
        juce::ColourGradient topGrad(
            Colors::bgElevated().interpolatedWith(Colors::secondary(), 0.10f), 0.0f, 0.0f,
            Colors::bgSurface(), 0.0f, static_cast<float>(topH), false);
        g.setGradientFill(topGrad);
        g.fillRect(0, 0, w, topH);

        g.setColour(Colors::primary().withAlpha(0.10f));
        g.fillRect(0, 0, w, topH / 2);

        g.setColour(Colors::accent());
        g.fillRect(0.0f, static_cast<float>(topH - 2), wf, 2.0f);

        g.setColour(Colors::primary().withAlpha(0.08f));
        g.fillRoundedRectangle(static_cast<float>(M - 2), 6.0f, 164.0f, 32.0f, 4.0f);

        juce::String loadedName = m_trackTitle->getText();
        if (loadedName.isNotEmpty())
        {
            juce::String loadedArtist = m_trackArtist->getText();
            juce::String displayStr = loadedName;
            if (loadedArtist.isNotEmpty())
                displayStr += "  -  " + loadedArtist;
            g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
            g.setColour(Colors::textSecondary().withAlpha(0.7f));
            int trackInfoX = w - M - 620;
            int trackInfoW = 240;
            if (trackInfoX > 400)
                g.drawText(displayStr, trackInfoX, 14, trackInfoW, 16, juce::Justification::centredRight, true);
        }

        if (m_isPlaying && w > 1020)
            ProDraw::statusPill(g, BM_TJ("hotcue.status.playing"),
                                { static_cast<float>(w - M - 760), 11.0f, 120.0f, 22.0f },
                                Colors::success(), true);
    }

    int deckY = topH + 4;

    int wfH = h * 34 / 100;
    if (wfH < 100) wfH = 100;
    if (wfH > 320) wfH = 320;

    {
        float wx = static_cast<float>(M - 2);
        float wy = static_cast<float>(deckY - 2);
        float ww = static_cast<float>(w - M * 2 + 4);
        float wh = static_cast<float>(wfH + 4);

        g.setColour(Colors::bgDarkest());
        g.fillRoundedRectangle(wx, wy, ww, wh, 10.0f);

        g.setColour(Colors::primary().withAlpha(0.3f));
        g.drawRoundedRectangle(wx, wy, ww, wh, 10.0f, 1.0f);

        g.setColour(juce::Colour(0x0CFFFFFF));
        g.fillRoundedRectangle(wx + 2.0f, wy, ww - 4.0f, 1.5f, 1.0f);

        if (m_trackTitle->getText().isEmpty())
        {
            g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
            g.setColour(Colors::textDim().withAlpha(0.4f));
            g.drawText(BM_TJ("hotcue.section.waveform"), M + 8, deckY + 4, 140, 16, juce::Justification::centredLeft);
            ProDraw::emptyState(g, juce::Rectangle<int>(M, deckY, w - M * 2, wfH),
                                juce::CharPointer_UTF8("\xe2\x99\xab"),
                                BM_TJ("hotcue.empty.title"),
                                BM_TJ("hotcue.empty.body"),
                                Colors::primary());
        }
    }

    int padHeaderY = deckY + wfH + 6;
    int padY = padHeaderY + 18;
    int padGridH = 108;
    int nudgeY = padY + padGridH + 6;
    {
        float px = static_cast<float>(M - 4);
        float py = static_cast<float>(padHeaderY - 2);
        float pw = static_cast<float>(w - M * 2 + 8);
        float ph = static_cast<float>((nudgeY + 24) - padHeaderY + 8);

        juce::ColourGradient padGrad(
            Colors::bgSurface(), px, py,
            Colors::bg(), px, py + ph, false);
        g.setGradientFill(padGrad);
        g.fillRoundedRectangle(px, py, pw, ph, 8.0f);

        g.setColour(Colors::border().withAlpha(0.15f));
        g.drawRoundedRectangle(px, py, pw, ph, 8.0f, 0.8f);

        g.setColour(Colors::primary());
        g.fillRoundedRectangle(px + 6.0f, py + 3.0f, 4.0f, 12.0f, 2.0f);
        g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        g.drawText(BM_TJ("hotcue.section.hotcues"), static_cast<int>(px + 14), static_cast<int>(py + 2), 100, 14, juce::Justification::centredLeft);

        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::plain));
        g.setColour(Colors::textMuted());
        g.drawText(BM_TJ("hotcue.hint.holdPreview"), static_cast<int>(px + 120), static_cast<int>(py + 2),
                   static_cast<int>(pw) - 130, 14, juce::Justification::centredLeft, true);
    }

    int bottomY = nudgeY + 24 + 10;
    int bottomH = h - bottomY - M;
    int libW = (w - M * 2) * 40 / 100;
    int detX = M + libW + 10;
    int detW = w - M - detX;

    {
        float sepX = static_cast<float>(M + libW + 4);
        float sepY = static_cast<float>(bottomY);
        float sepH = static_cast<float>(bottomH);
        juce::ColourGradient sepGrad(
            Colors::border().withAlpha(0.0f), sepX, sepY,
            Colors::border().withAlpha(0.25f), sepX, sepY + sepH * 0.3f, false);
        g.setGradientFill(sepGrad);
        g.fillRect(sepX, sepY, 1.0f, sepH * 0.3f);
        juce::ColourGradient sepGrad2(
            Colors::border().withAlpha(0.25f), sepX, sepY + sepH * 0.3f,
            Colors::border().withAlpha(0.0f), sepX, sepY + sepH, false);
        g.setGradientFill(sepGrad2);
        g.fillRect(sepX, sepY + sepH * 0.3f, 1.0f, sepH * 0.7f);
    }


    {
        int tabY = bottomY + 2;
        int tabBtnW = libW / 2 - 1;
        if (!m_showFolderTree)
        {
            g.setColour(Colors::primary());
            g.fillRoundedRectangle(static_cast<float>(M), static_cast<float>(tabY + 22), static_cast<float>(tabBtnW), 2.0f, 1.0f);
        }
        else
        {
            g.setColour(Colors::primary());
            g.fillRoundedRectangle(static_cast<float>(M + tabBtnW + 2), static_cast<float>(tabY + 22), static_cast<float>(tabBtnW), 2.0f, 1.0f);
        }
    }

    if (!m_showFolderTree)
    {
        int filterY = bottomY + 2 + 26;
        g.setColour(Colors::bgDarkest().withAlpha(0.4f));
        g.fillRoundedRectangle(static_cast<float>(M - 2), static_cast<float>(filterY - 2),
                                static_cast<float>(libW + 4), 28.0f, 4.0f);
    }

    g.setColour(Colors::secondary());
    g.fillRoundedRectangle(static_cast<float>(detX), static_cast<float>(bottomY + 1), 5.0f, 16.0f, 2.0f);
    g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::bold));
    g.setColour(Colors::textPrimary());
    g.drawText(BM_TJ("hotcue.section.details"), detX + 12, bottomY - 2, 160, 18, juce::Justification::centredLeft);

    {
        int detailY = bottomY + 16;
        float dpx = static_cast<float>(detX - 4);
        float dpy = static_cast<float>(detailY - 4);
        float dpw = static_cast<float>(detW + 8);
        float dph = static_cast<float>(8 * 26 + 8); // 8 rows with spacing
        ProDraw::glassPanel(g, juce::Rectangle<float>(dpx, dpy, dpw, dph), 8.0f);
    }

    int detailY = bottomY + 16;
    int rowH = 24;
    for (int i = 0; i < 8; ++i)
    {
        int ry = detailY + i * rowH;
        auto& c = m_cues[static_cast<size_t>(i)];

        if (i % 2 == 0)
        {
            g.setColour(juce::Colour(0x06FFFFFF));
            g.fillRoundedRectangle(static_cast<float>(detX - 2), static_cast<float>(ry),
                                    static_cast<float>(detW + 4), static_cast<float>(rowH), 3.0f);
        }

        g.setColour(c.active ? c.color : c.color.withAlpha(0.15f));
        g.fillRoundedRectangle(static_cast<float>(detX), static_cast<float>(ry + 2),
                                4.0f, static_cast<float>(rowH - 4), 2.0f);

        char label = static_cast<char>('A' + i);
        g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
        g.setColour(c.active ? c.color : Colors::textDim());
        g.drawText(juce::String::charToString(label), detX + 8, ry, 16, rowH, juce::Justification::centred);
    }

    {
        const float hintY = hf - static_cast<float>(M) - 18.0f;
        juce::StringArray hints;
        hints.addTokens(BM_TJ("hotcue.hint.shortcuts"), "|", "");
        float hx = static_cast<float>(detX);
        for (auto& hint : hints)
        {
            const juce::String txt = hint.trim();
            if (txt.isEmpty())
                continue;
            const float cw = juce::GlyphArrangement::getStringWidth(Type::caption(), txt) + 18.0f;
            if (hx + cw > wf - static_cast<float>(M))
                break;
            ProDraw::badge(g, txt, hx, hintY, cw, 16.0f, Colors::textMuted());
            hx += cw + 6.0f;
        }
    }

    ProDraw::vignette(g, wf, hf, 10.0f);
}


void HotCueView::resized()
{
    int w = getWidth();
    int h = getHeight();
    int M = 12;
    int topH = 44;

    m_titleLabel->setBounds(M, 6, 220, 32);

    int playX = M + 144;
    m_playPauseBtn->setBounds(playX, 4, 100, 36);
    m_positionLabel->setBounds(playX + 106, 4, 130, 36);

    int infoX = playX + 242;
    int infoW = w - infoX - 260;
    if (infoW > 0)
    {
        m_trackTitle->setBounds(infoX, 6, infoW / 2, 16);
        m_trackArtist->setBounds(infoX, 24, infoW / 2, 14);
    }

    if (m_quantizeBtn)
        m_quantizeBtn->setBounds(w - M - 340, 12, 90, 20);
    m_zoomLabel->setBounds(w - M - 240, 14, 36, 16);
    m_zoomSlider->setBounds(w - M - 200, 12, 194, 20);

    int deckY = topH + 4;
    int wfH = h * 34 / 100;
    if (wfH < 100) wfH = 100;
    if (wfH > 320) wfH = 320;
    int scrollH = 16;
    m_waveform->setBounds(M, deckY, w - M * 2, wfH - scrollH - 2);
    if (m_waveformScrollBar)
        m_waveformScrollBar->setBounds(M, deckY + wfH - scrollH, w - M * 2, scrollH);

    int padHeaderY = deckY + wfH + 6;
    int padY = padHeaderY + 18;
    int padGridH = 108;
    if (m_padGrid)
        m_padGrid->setBounds(M, padY, w - M * 2, padGridH);

    int nudgeY = padY + padGridH + 6;
    {
        int nudgeW = 92;
        int nx = M;
        for (int n = 0; n < 4; ++n)
        {
            if (m_nudgeBtns[static_cast<size_t>(n)])
                m_nudgeBtns[static_cast<size_t>(n)]->setBounds(nx, nudgeY, nudgeW, 22);
            nx += nudgeW + 6;
        }
    }

    int bottomY = nudgeY + 24 + 10;
    int bottomH = h - bottomY - M;
    int libW = (w - M * 2) * 40 / 100;
    int detX = M + libW + 10;
    int detW = w - M - detX;

    int tabY = bottomY + 2;
    int tabBtnW = (libW - 4) / 3;
    m_tabListBtn->setBounds(M, tabY, tabBtnW, 22);
    m_tabFolderBtn->setBounds(M + tabBtnW + 2, tabY, tabBtnW, 22);
    if (m_libraryPopupBtn)
        m_libraryPopupBtn->setBounds(M + 2 * (tabBtnW + 2), tabY, libW - 2 * (tabBtnW + 2), 22);

    int filterY = tabY + 26;
    int filterH = 24;
    int listBottom = h - M - 32;

    m_searchEditor->setVisible(true);
    m_genreFilter->setVisible(true);
    m_cueStatusFilter->setVisible(true);
    m_trackListBox->setVisible(true);
    m_folderTree->setVisible(m_showFolderTree);

    m_searchEditor->setBounds(M, filterY, libW, filterH);
    m_genreFilter->setBounds(M, filterY + filterH + 2, libW / 2 - 2, filterH);
    m_cueStatusFilter->setBounds(M + libW / 2 + 2, filterY + filterH + 2, libW / 2 - 2, filterH);
    int listY = filterY + filterH * 2 + 4;
    int avail = std::max(listBottom - listY, 120);
    if (m_showFolderTree)
    {
        int treeH = avail * 45 / 100;
        m_folderTree->setBounds(M, listY, libW, treeH);
        m_trackListBox->setBounds(M, listY + treeH + 4, libW, avail - treeH - 4);
    }
    else
    {
        m_trackListBox->setBounds(M, listY, libW, avail);
    }

    int actY = listBottom + 2;
    int actBtnW = (libW - 4) / 2;
    m_autoGenerateBtn->setBounds(M, actY, actBtnW, 26);
    m_clearAllBtn->setBounds(M + actBtnW + 4, actY, actBtnW, 26);

    int detailY = bottomY + 16;
    int rowH = 24;
    int nameW = std::min(detW - 130, 250);
    if (nameW < 80) nameW = 80;

    for (int i = 0; i < 8; ++i)
    {
        auto& row = m_cueRows[static_cast<size_t>(i)];
        int ry = detailY + i * rowH;

        row.nameEditor->setBounds(detX + 24, ry + 2, nameW, rowH - 4);
        row.positionLabel->setBounds(detX + 28 + nameW, ry + 2, 80, rowH - 4);
    }

    int exportY = h - M - 26;
    int expAvail = detW;
    m_exportTargetCombo->setBounds(detX + expAvail - 310, exportY, 140, 24);
    m_exportBtn->setBounds(detX + expAvail - 164, exportY, 164, 24);
}


void HotCueView::AutoGenerateThread::run()
{
    Core::AudioFileReader reader;
    auto audioTrack = reader.readFile(m_path);
    auto* ownerPtr = &m_owner;
    auto alive = m_owner.m_aliveFlag;
    if (!audioTrack || threadShouldExit())
    {
        juce::MessageManager::callAsync([ownerPtr, alive]() {
            if (!alive || !alive->load()) return;
            ownerPtr->m_autoGenerateBtn->setButtonText(BM_TJ("hotcue.autoGenerate"));
            ownerPtr->m_autoGenerateBtn->setEnabled(true);
        });
        return;
    }

    Core::IntelligentCueCreator cueCreator;
    cueCreator.setPrioritizeDrops(true);
    cueCreator.setSnapToDownbeat(true);
    auto result = cueCreator.generateCues(*audioTrack, 8);

    if (threadShouldExit()) return;

    juce::MessageManager::callAsync([ownerPtr, alive, result = std::move(result)]() {
        if (!alive || !alive->load()) return;
        ownerPtr->applyAutoGenerateResult(result);
    });
}

void HotCueView::applyAutoGenerateResult(const Core::IntelligentCueResult& result)
{
    m_autoGenerateBtn->setButtonText(BM_TJ("hotcue.autoGenerate"));
    m_autoGenerateBtn->setEnabled(true);

    for (int i = 0; i < 8; ++i) {
        m_cues[static_cast<size_t>(i)].active = false;
        m_cues[static_cast<size_t>(i)].positionSeconds = 0.0;
        updateCueButton(i);
    }

    for (int i = 0; i < static_cast<int>(result.cues.size()) && i < 8; ++i)
    {
        auto& ic = result.cues[static_cast<size_t>(i)];
        auto& c = m_cues[static_cast<size_t>(i)];
        c.active = true;
        c.positionSeconds = ic.cue.position;
        c.color = juce::Colour(ic.cue.color);
        c.name = juce::String(ic.sectionType.empty() ? ic.reason : ic.sectionType);

        m_cueRows[static_cast<size_t>(i)].nameEditor->setText(c.name, juce::dontSendNotification);
        updateCueButton(i);
    }

    updateWaveformCueMarkers();
    persistCuesToDatabase();
    updateNudgeButtons();
    m_listeners.call(&Listener::autoGenerateRequested);

    juce::String msg = juce::String(result.cues.size()) + " cue points places (confiance: "
        + juce::String(static_cast<int>(result.overallConfidence * 100)) + "%)\n\n";
    for (size_t i = 0; i < result.cues.size(); ++i) {
        auto& ic = result.cues[i];
        msg += "Cue " + juce::String(i + 1) + ": " + juce::String(ic.reason)
            + " @ " + formatTime(ic.cue.position) + "\n";
    }
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Auto-generate (IA)", msg);
}

void HotCueView::persistCuesToDatabase()
{
    if (!m_provider || m_currentTrackId <= 0) return;

    // Drop the existing cues for this track so our 8-slot model is authoritative.
    auto existing = m_provider->getCuePoints(m_currentTrackId);
    for (const auto& c : existing)
        m_provider->deleteCuePoint(c.id);

    int savedCount = 0;
    for (int i = 0; i < 8; ++i) {
        const auto& ui = m_cues[static_cast<size_t>(i)];
        if (!ui.active) continue;
        Models::CuePoint cp;
        cp.id       = 0; // 0 → new row, provider assigns
        cp.trackId  = m_currentTrackId;
        cp.type     = Models::CuePointType::HotCue;
        cp.position = ui.positionSeconds;
        cp.length   = 0.0;
        cp.number   = i + 1;
        cp.name     = ui.name.toStdString();
        cp.color    = ("#" + ui.color.toDisplayString(false)).toStdString();
        m_provider->saveCuePoint(cp);
        ++savedCount;
    }

    spdlog::info("[HotCueView] Persisted {} cues for trackId={}", savedCount, m_currentTrackId);

    // Keep the lightweight cue-status cache consistent with the DB.
    if (savedCount > 0) {
        m_cueStatusCache.insert(m_currentTrackId);
        m_cueCountCache[m_currentTrackId] = savedCount;
    } else {
        m_cueStatusCache.erase(m_currentTrackId);
        m_cueCountCache.erase(m_currentTrackId);
    }
    filterTrackList();
}

} // namespace BeatMate::UI
