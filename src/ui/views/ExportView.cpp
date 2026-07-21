#include "ExportView.h"
#include "export/ExportReportDialog.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/export/ExportPresetStore.h"
#include "../../services/config/I18n.h"
#include "../../services/security/LicenseService.h"
#include "../../app/ServiceLocator.h"
#include "../utils/ViewPrefs.h"
#include <fstream>
#include <cmath>
#include <spdlog/spdlog.h>

namespace BeatMate::UI {

void ExportView::TrackModel::paintListBoxItem(
    int row, juce::Graphics& g, int w, int h, bool listSelected)
{
    if (!tracks || row < 0 || row >= (int)tracks->size()) return;
    auto& t = (*tracks)[row];

    if (listSelected)
        g.fillAll(juce::Colour(0xFF2563EB).withAlpha(0.2f));
    else if (row % 2 == 0)
        g.fillAll(Colors::bgDark().withAlpha(0.5f));
    else
        g.fillAll(juce::Colour(0xFF1E293B).withAlpha(0.3f));

    int cbX = 8, cbY = (h - 16) / 2, cbSz = 16;
    g.setColour(Colors::bgLightest());
    g.fillRoundedRectangle((float)cbX, (float)cbY, (float)cbSz, (float)cbSz, 3.0f);
    g.setColour(Colors::border());
    g.drawRoundedRectangle((float)cbX, (float)cbY, (float)cbSz, (float)cbSz, 3.0f, 1.0f);

    if (t.selected)
    {
        g.setColour(Colors::primary());
        g.fillRoundedRectangle((float)cbX + 1, (float)cbY + 1, (float)cbSz - 2, (float)cbSz - 2, 3.0f);
        g.setColour(Colors::textPrimary());
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("v", cbX, cbY, cbSz, cbSz, juce::Justification::centred);
    }

    int x = cbX + cbSz + 8;

    g.setColour(t.selected ? Colors::textPrimary() : Colors::textMuted());
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(t.title, x, 0, 220, h, juce::Justification::centredLeft);
    x += 224;

    g.setColour(t.selected ? Colors::textSecondary() : Colors::textMuted());
    g.setFont(juce::Font(12.0f));
    g.drawText(t.artist, x, 0, 160, h, juce::Justification::centredLeft);
    x += 164;

    g.setColour(Colors::primary());
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(t.bpm > 0 ? juce::String(t.bpm, 0) + " BPM" : "-", x, 0, 65, h,
               juce::Justification::centredLeft);
    x += 68;

    g.setColour(Colors::accent());
    g.drawText(t.key, x, 0, 40, h, juce::Justification::centred);
    x += 44;

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(11.0f));
    if (t.duration > 0)
    {
        int mins = (int)(t.duration / 60.0);
        int secs = (int)t.duration % 60;
        g.drawText(juce::String::formatted("%d:%02d", mins, secs), x, 0, 50, h,
                   juce::Justification::centred);
    }

    g.setColour(Colors::border().withAlpha(0.3f));
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void ExportView::TrackModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (!tracks || row < 0 || row >= (int)tracks->size()) return;

    if (e.x < 32)
    {
        (*tracks)[row].selected = !(*tracks)[row].selected;
        if (owner) owner->updateEstimatedSize();
    }
}

ExportView::ExportView() : m_provider(nullptr) { setupUI(); retranslateUi(); }

ExportView::ExportView(Services::Library::TrackDataProvider* provider)
    : m_provider(provider) { setupUI(); retranslateUi(); }

void ExportView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("export.pro.title"));
    m_titleLabel->setFont(juce::Font(24.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    auto makeBtn = [this](const juce::String& t, juce::Colour bg = Colors::bgLighter())
    {
        auto b = std::make_unique<juce::TextButton>(t);
        b->setColour(juce::TextButton::buttonColourId, bg);
        b->setColour(juce::TextButton::buttonOnColourId, bg.brighter(0.2f));
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        b->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        addAndMakeVisible(*b);
        return b;
    };

    auto makeLabel = [this](const juce::String& text)
    {
        auto l = std::make_unique<juce::Label>("", text);
        l->setFont(juce::Font(11.0f));
        l->setColour(juce::Label::textColourId, Colors::textSecondary());
        addAndMakeVisible(*l);
        return l;
    };

    m_addTracksBtn    = makeBtn(BM_TJ("export.addTracks"), Colors::primary());
    m_addPlaylistBtn  = makeBtn(BM_TJ("export.addPlaylist"));
    m_selectAllBtn    = makeBtn(BM_TJ("export.selectAll"));
    m_deselectAllBtn  = makeBtn(BM_TJ("export.deselectAll"));
    m_removeSelBtn    = makeBtn(BM_TJ("export.removeSel"), Colors::error().withAlpha(0.5f));

    m_addTracksBtn->onClick   = [this] { spdlog::info("[ExportView] addTracks clicked"); onAddTracks(); };
    m_addPlaylistBtn->onClick = [this] { spdlog::info("[ExportView] addPlaylist clicked"); onAddPlaylist(); };
    m_selectAllBtn->onClick   = [this] { spdlog::info("[ExportView] selectAll clicked"); onSelectAll(); };
    m_deselectAllBtn->onClick = [this] { spdlog::info("[ExportView] deselectAll clicked"); onDeselectAll(); };
    m_removeSelBtn->onClick   = [this] { spdlog::info("[ExportView] removeSelected clicked"); onRemoveSelected(); };

    m_formatLabel = makeLabel(BM_TJ("export.format"));
    m_formatCombo = std::make_unique<juce::ComboBox>();
    juce::StringArray fmts = {"MP3", "WAV", "FLAC", "OGG", "AAC", "AIFF"};
    for (int i = 0; i < fmts.size(); ++i) m_formatCombo->addItem(fmts[i], i + 1);
    m_formatCombo->setSelectedId(1);
    m_formatCombo->setTooltip(BM_TJ("export.settings.formatTip"));
    m_formatCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_formatCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_formatCombo->onChange = [this] {
        Prefs::setInt("export.formatId", m_formatCombo->getSelectedId());
        updateEstimatedSize(); repaint();
    };
    addAndMakeVisible(*m_formatCombo);

    m_qualityLabel = makeLabel(BM_TJ("export.quality"));
    m_bitrateCombo = std::make_unique<juce::ComboBox>();
    juce::StringArray brs = {"128 kbps", "192 kbps", "256 kbps", "320 kbps", "VBR"};
    for (int i = 0; i < brs.size(); ++i) m_bitrateCombo->addItem(brs[i], i + 1);
    m_bitrateCombo->setSelectedId(4);
    m_bitrateCombo->setTooltip(BM_TJ("export.settings.bitrateTip"));
    m_bitrateCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_bitrateCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_bitrateCombo->onChange = [this] {
        Prefs::setInt("export.bitrateId", m_bitrateCombo->getSelectedId());
        updateEstimatedSize();
    };
    addAndMakeVisible(*m_bitrateCombo);

    m_sampleRateLabel = makeLabel(BM_TJ("export.sampleRate"));
    m_sampleRateCombo = std::make_unique<juce::ComboBox>();
    juce::StringArray srs = {"44100 Hz", "48000 Hz"};
    for (int i = 0; i < srs.size(); ++i) m_sampleRateCombo->addItem(srs[i], i + 1);
    m_sampleRateCombo->setSelectedId(1);
    m_sampleRateCombo->setTooltip(BM_TJ("export.settings.sampleRateTip"));
    m_sampleRateCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_sampleRateCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_sampleRateCombo->onChange = [this] { Prefs::setInt("export.sampleRateId", m_sampleRateCombo->getSelectedId()); };
    addAndMakeVisible(*m_sampleRateCombo);

    m_bitDepthLabel = makeLabel(BM_TJ("export.bitDepth"));
    m_bitDepthCombo = std::make_unique<juce::ComboBox>();
    m_bitDepthCombo->addItem(BM_TJ("export.settings.bitDepth16"), 1);
    m_bitDepthCombo->addItem(BM_TJ("export.settings.bitDepth24"), 2);
    m_bitDepthCombo->setSelectedId(1);
    m_bitDepthCombo->setTooltip(BM_TJ("export.settings.bitDepthTip"));
    m_bitDepthCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_bitDepthCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_bitDepthCombo->onChange = [this] { Prefs::setInt("export.bitDepthId", m_bitDepthCombo->getSelectedId()); };
    addAndMakeVisible(*m_bitDepthCombo);

    m_normalizeCheck = std::make_unique<juce::ToggleButton>(BM_TJ("export.normalize"));
    m_normalizeCheck->setToggleState(false, juce::dontSendNotification);
    m_normalizeCheck->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    m_normalizeCheck->onClick = [this] {
        m_lufsSlider->setEnabled(m_normalizeCheck->getToggleState());
        Prefs::setBool("export.normalize", m_normalizeCheck->getToggleState());
        repaint();
    };
    addAndMakeVisible(*m_normalizeCheck);

    m_lufsLabel = makeLabel("LUFS: -14");
    m_lufsSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    m_lufsSlider->setRange(-24.0, -6.0, 0.5);
    m_lufsSlider->setValue(-14.0, juce::dontSendNotification);
    m_lufsSlider->setEnabled(false);
    m_lufsSlider->setColour(juce::Slider::trackColourId, Colors::accent());
    m_lufsSlider->setColour(juce::Slider::backgroundColourId, Colors::bgLighter());
    m_lufsSlider->onValueChange = [this] {
        m_lufsLabel->setText("LUFS: " + juce::String(m_lufsSlider->getValue(), 1), juce::dontSendNotification);
        Prefs::setDouble("export.lufs", m_lufsSlider->getValue());
    };
    addAndMakeVisible(*m_lufsSlider);

    auto makePreset = [this](const juce::String& label, double lufs, const juce::String& tooltip) {
        auto b = std::make_unique<juce::TextButton>(label);
        b->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
        b->setColour(juce::TextButton::buttonOnColourId, Colors::primary());
        b->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        b->setColour(juce::TextButton::textColourOnId, Colors::textPrimary());
        b->setTooltip(tooltip);
        b->onClick = [this, lufs]() {
            if (m_normalizeCheck) m_normalizeCheck->setToggleState(true, juce::sendNotification);
            if (m_lufsSlider) {
                m_lufsSlider->setEnabled(true);
                m_lufsSlider->setValue(lufs, juce::sendNotificationSync);
            }
        };
        addAndMakeVisible(*b);
        return b;
    };
    m_presetSpotifyBtn = makePreset("Spotify -14",
        -14.0, "Spotify loudness normalization target (integrated LUFS).");
    m_presetAppleBtn = makePreset("Apple Music -16",
        -16.0, "Apple Music Sound Check target (integrated LUFS).");
    m_presetYouTubeBtn = makePreset("YouTube -14",
        -14.0, "YouTube loudness normalization target.");
    m_presetClubBtn = makePreset(juce::String::fromUTF8("Club −8"),
        -8.0, "Club-ready master, higher perceived loudness (streaming normalisation bypassed).");

    m_writeTagsCheck = std::make_unique<juce::ToggleButton>(BM_TJ("export.writeTags"));
    m_writeTagsCheck->setToggleState(true, juce::dontSendNotification);
    m_writeTagsCheck->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    m_writeTagsCheck->onClick = [this] { Prefs::setBool("export.writeTags", m_writeTagsCheck->getToggleState()); };
    addAndMakeVisible(*m_writeTagsCheck);

    m_writeM3UCheck = std::make_unique<juce::ToggleButton>(BM_TJ("export.writeM3U"));
    m_writeM3UCheck->setToggleState(Prefs::getBool("export.writeM3U", true), juce::dontSendNotification);
    m_writeM3UCheck->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    m_writeM3UCheck->setTooltip(BM_TJ("export.writeM3UTip"));
    m_writeM3UCheck->onClick = [this] { Prefs::setBool("export.writeM3U", m_writeM3UCheck->getToggleState()); };
    addAndMakeVisible(*m_writeM3UCheck);

    m_destLabel = makeLabel(BM_TJ("export.destination"));
    m_destEdit = std::make_unique<juce::TextEditor>();
    m_destEdit->setTextToShowWhenEmpty(BM_TJ("export.destinationPH"), juce::Colours::grey);
    m_destEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLight());
    m_destEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_destEdit->setColour(juce::TextEditor::outlineColourId, Colors::border());
    addAndMakeVisible(*m_destEdit);
    {
        juce::String lastDest = juce::String::fromUTF8(Prefs::getString("export.lastDest").c_str());
        if (lastDest.isNotEmpty() && juce::File{ lastDest }.isDirectory())
            m_destEdit->setText(lastDest, juce::dontSendNotification);
        else
            m_destEdit->setText(juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                    .getChildFile("BeatMate Export").getFullPathName(),
                                juce::dontSendNotification);
    }

    m_browseBtn = makeBtn(BM_TJ("export.browse"));
    m_browseBtn->onClick = [this] { onBrowseDestination(); };

    m_structLabel = makeLabel(BM_TJ("export.structure"));
    m_structureCombo = std::make_unique<juce::ComboBox>();
    m_structureCombo->addItem(BM_TJ("export.structure.generic"), 1);
    m_structureCombo->addItem(BM_TJ("export.structure.rekordbox"), 2);
    m_structureCombo->addItem(BM_TJ("export.structure.engineDj"), 3);
    m_structureCombo->setSelectedId(1);
    m_structureCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_structureCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_structureCombo->onChange = [this] { Prefs::setInt("export.structureId", m_structureCombo->getSelectedId()); };
    addAndMakeVisible(*m_structureCombo);

    m_sizeLabel = makeLabel(BM_TJ("export.sizeEst"));
    m_sizeLabel->setFont(juce::Font(12.0f, juce::Font::bold));
    m_sizeLabel->setColour(juce::Label::textColourId, Colors::accent());

    m_exportM3UBtn = makeBtn("M3U");
    m_exportPLSBtn = makeBtn("PLS");
    m_exportPDFBtn = makeBtn(BM_TJ("export.htmlSetlist"), Colors::primary());

    m_exportM3UBtn->onClick = [this] { spdlog::info("[ExportView] exportPlaylistM3U clicked"); onExportPlaylistM3U(); };
    m_exportPLSBtn->onClick = [this] { spdlog::info("[ExportView] exportPlaylistPLS clicked"); onExportPlaylistPLS(); };
    m_exportPDFBtn->onClick = [this] { spdlog::info("[ExportView] exportPlaylistPDF clicked"); onExportPlaylistPDF(); };

    m_exportBtn = makeBtn(BM_TJ("export.do"), Colors::primary());
    m_exportBtn->onClick = [this] { spdlog::info("[ExportView] startExport clicked"); onStartExport(); };

    m_cancelBtn = makeBtn(BM_TJ("export.cancel"), Colors::error().withAlpha(0.5f));
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->onClick = [this] { spdlog::info("[ExportView] cancel clicked"); onCancel(); };

    m_statusLabel = std::make_unique<juce::Label>("st", BM_TJ("export.ready"));
    m_statusLabel->setFont(juce::Font(12.0f));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_statusLabel);

    m_timeLabel = makeLabel("");

    m_trackModel = std::make_unique<TrackModel>();
    m_trackModel->tracks = &m_exportTracks;
    m_trackModel->owner = this;
    m_trackTable = std::make_unique<juce::ListBox>("tl", m_trackModel.get());
    m_trackTable->setRowHeight(30);
    m_trackTable->setColour(juce::ListBox::backgroundColourId, Colors::bgDarker());
    m_trackTable->setColour(juce::ListBox::outlineColourId, Colors::border());
    addAndMakeVisible(*m_trackTable);

    m_exportService = std::make_unique<Services::Export::BatchExportService>(m_provider);

    m_presetLabel = makeLabel(BM_TJ("export.presets.header"));
    m_presetCombo = std::make_unique<juce::ComboBox>();
    m_presetCombo->setTextWhenNothingSelected(BM_TJ("export.presets.none"));
    m_presetCombo->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    m_presetCombo->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_presetCombo->onChange = [this] {
        const int idx = m_presetCombo->getSelectedId() - 1;
        auto presets = Services::Export::ExportPresetStore::loadAll();
        if (idx >= 0 && idx < static_cast<int>(presets.size()))
            applySettings(presets[static_cast<size_t>(idx)].settings);
    };
    addAndMakeVisible(*m_presetCombo);

    m_presetSaveBtn = makeBtn(BM_TJ("export.presets.save"));
    m_presetSaveBtn->onClick = [this] { onSavePreset(); };
    m_presetDeleteBtn = makeBtn(BM_TJ("export.presets.delete"), Colors::error().withAlpha(0.35f));
    m_presetDeleteBtn->onClick = [this] { onDeletePreset(); };
    refreshPresetCombo();

    m_djRekordbox = Prefs::getBool("export.dj.rekordbox", false);
    m_djSerato    = Prefs::getBool("export.dj.serato", false);
    m_djTraktor   = Prefs::getBool("export.dj.traktor", false);
    m_djVirtualDJ = Prefs::getBool("export.dj.virtualdj", false);

    {
        auto applyId = [](juce::ComboBox* c, int id) {
            if (c && id >= 1 && id <= c->getNumItems())
                c->setSelectedId(id, juce::dontSendNotification);
        };
        applyId(m_formatCombo.get(),     Prefs::getInt("export.formatId",     1));
        applyId(m_bitrateCombo.get(),    Prefs::getInt("export.bitrateId",    4));
        applyId(m_sampleRateCombo.get(), Prefs::getInt("export.sampleRateId", 1));
        applyId(m_bitDepthCombo.get(),   Prefs::getInt("export.bitDepthId",   1));
        applyId(m_structureCombo.get(),  Prefs::getInt("export.structureId",  1));
        const bool norm = Prefs::getBool("export.normalize", false);
        if (m_normalizeCheck) m_normalizeCheck->setToggleState(norm, juce::dontSendNotification);
        if (m_lufsSlider) {
            m_lufsSlider->setValue(Prefs::getDouble("export.lufs", -14.0), juce::dontSendNotification);
            m_lufsSlider->setEnabled(norm);
        }
        if (m_lufsLabel && m_lufsSlider)
            m_lufsLabel->setText("LUFS: " + juce::String(m_lufsSlider->getValue(), 1),
                                 juce::dontSendNotification);
        if (m_writeTagsCheck)
            m_writeTagsCheck->setToggleState(Prefs::getBool("export.writeTags", true),
                                             juce::dontSendNotification);
    }
}

void ExportView::retranslateUi()
{
    auto rebuild = [](juce::ComboBox* cb, std::initializer_list<const char*> keys) {
        if (!cb) return;
        int prev = cb->getSelectedId();
        cb->clear(juce::dontSendNotification);
        int id = 1;
        for (auto k : keys) cb->addItem(BM_TJ(k), id++);
        if (prev > 0)
            cb->setSelectedId(prev, juce::dontSendNotification);
    };

    if (m_titleLabel)      m_titleLabel->setText(BM_TJ("export.pro.title"), juce::dontSendNotification);

    if (m_addTracksBtn)    m_addTracksBtn->setButtonText(BM_TJ("export.addTracks"));
    if (m_addPlaylistBtn)  m_addPlaylistBtn->setButtonText(BM_TJ("export.addPlaylist"));
    if (m_selectAllBtn)    m_selectAllBtn->setButtonText(BM_TJ("export.selectAll"));
    if (m_deselectAllBtn)  m_deselectAllBtn->setButtonText(BM_TJ("export.deselectAll"));
    if (m_removeSelBtn)    m_removeSelBtn->setButtonText(BM_TJ("export.removeSel"));

    if (m_formatLabel)     m_formatLabel->setText(BM_TJ("export.format"), juce::dontSendNotification);
    if (m_formatCombo)     m_formatCombo->setTooltip(BM_TJ("export.settings.formatTip"));

    if (m_qualityLabel)    m_qualityLabel->setText(BM_TJ("export.quality"), juce::dontSendNotification);
    if (m_bitrateCombo)    m_bitrateCombo->setTooltip(BM_TJ("export.settings.bitrateTip"));

    if (m_sampleRateLabel) m_sampleRateLabel->setText(BM_TJ("export.sampleRate"), juce::dontSendNotification);
    if (m_sampleRateCombo) m_sampleRateCombo->setTooltip(BM_TJ("export.settings.sampleRateTip"));

    if (m_bitDepthLabel)   m_bitDepthLabel->setText(BM_TJ("export.bitDepth"), juce::dontSendNotification);
    rebuild(m_bitDepthCombo.get(), { "export.settings.bitDepth16", "export.settings.bitDepth24" });
    if (m_bitDepthCombo)   m_bitDepthCombo->setTooltip(BM_TJ("export.settings.bitDepthTip"));

    if (m_normalizeCheck)  m_normalizeCheck->setButtonText(BM_TJ("export.normalize"));
    if (m_writeTagsCheck)  m_writeTagsCheck->setButtonText(BM_TJ("export.writeTags"));
    if (m_writeM3UCheck) {
        m_writeM3UCheck->setButtonText(BM_TJ("export.writeM3U"));
        m_writeM3UCheck->setTooltip(BM_TJ("export.writeM3UTip"));
    }

    if (m_destLabel)       m_destLabel->setText(BM_TJ("export.destination"), juce::dontSendNotification);
    if (m_destEdit)        m_destEdit->setTextToShowWhenEmpty(BM_TJ("export.destinationPH"), juce::Colours::grey);
    if (m_browseBtn)       m_browseBtn->setButtonText(BM_TJ("export.browse"));

    if (m_structLabel)     m_structLabel->setText(BM_TJ("export.structure"), juce::dontSendNotification);
    rebuild(m_structureCombo.get(), { "export.structure.generic", "export.structure.rekordbox", "export.structure.engineDj" });

    if (m_sizeLabel)       m_sizeLabel->setText(BM_TJ("export.sizeEst"), juce::dontSendNotification);

    if (m_exportPDFBtn)    m_exportPDFBtn->setButtonText(BM_TJ("export.htmlSetlist"));
    if (m_exportBtn)       m_exportBtn->setButtonText(BM_TJ("export.do"));
    if (m_cancelBtn)       m_cancelBtn->setButtonText(BM_TJ("export.cancel"));

    if (m_presetLabel)     m_presetLabel->setText(BM_TJ("export.presets.header"), juce::dontSendNotification);
    if (m_presetCombo)     m_presetCombo->setTextWhenNothingSelected(BM_TJ("export.presets.none"));
    if (m_presetSaveBtn)   m_presetSaveBtn->setButtonText(BM_TJ("export.presets.save"));
    if (m_presetDeleteBtn) m_presetDeleteBtn->setButtonText(BM_TJ("export.presets.delete"));

    if (m_statusLabel)     m_statusLabel->setText(BM_TJ("export.ready"), juce::dontSendNotification);

    if (!m_exportTracks.empty())
        updateEstimatedSize();

    repaint();
}

void ExportView::onAddTracks()
{
    if (m_provider)
    {
        auto tracks = m_provider->getAllTracks();
        for (auto& t : tracks)
        {
            bool alreadyAdded = false;
            for (auto& et : m_exportTracks)
                if (et.filePath == juce::String(t.filePath)) { alreadyAdded = true; break; }
            if (!alreadyAdded)
            {
                ExportTrackInfo eti;
                eti.trackId = t.id;
                eti.title = juce::String(t.title);
                eti.artist = juce::String(t.artist);
                eti.filePath = juce::String(t.filePath);
                eti.key = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
                eti.duration = t.duration;
                eti.bpm = (float)t.bpm;
                eti.energy = (int)t.energy;
                eti.selected = true;
                eti.fileSize = t.fileSize;
                m_exportTracks.push_back(eti);
            }
        }
        m_trackTable->updateContent();
        updateEstimatedSize();
        m_statusLabel->setText(juce::String((int)m_exportTracks.size()) + " " + BM_TJ("export.tracksLoaded"),
                              juce::dontSendNotification);
    }
    else
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            BM_TJ("export.chooseAudio"), juce::File{},
            "*.mp3;*.wav;*.flac;*.aiff;*.ogg;*.m4a;*.aac;*.wma");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectMultipleItems,
            [this, chooser](const juce::FileChooser& fc) {
                auto results = fc.getResults();
                if (results.isEmpty()) return;
                for (auto& f : results) {
                    ExportTrackInfo eti;
                    eti.title = f.getFileNameWithoutExtension();
                    eti.filePath = f.getFullPathName();
                    eti.selected = true;
                    eti.fileSize = f.getSize();
                    m_exportTracks.push_back(eti);
                }
                m_trackTable->updateContent();
                updateEstimatedSize();
                m_statusLabel->setText(juce::String((int)m_exportTracks.size()) + " " + BM_TJ("analysis.stats.tracks"),
                                        juce::dontSendNotification);
            });
    }
}

void ExportView::onAddPlaylist()
{
    if (!m_provider) return;

    auto playlists = m_provider->getAllPlaylists();
    if (playlists.empty()) return;

    juce::PopupMenu menu;
    for (int i = 0; i < (int)playlists.size(); ++i)
        menu.addItem(i + 1, juce::String(playlists[i].name));

    int result = menu.show();
    if (result > 0)
    {
        auto tracks = m_provider->getPlaylistTracks(playlists[result - 1].id);
        for (auto& t : tracks)
        {
            bool alreadyAdded = false;
            for (auto& et : m_exportTracks)
                if (et.filePath == juce::String(t.filePath)) { alreadyAdded = true; break; }
            if (!alreadyAdded)
            {
                ExportTrackInfo eti;
                eti.trackId = t.id;
                eti.title = juce::String(t.title);
                eti.artist = juce::String(t.artist);
                eti.filePath = juce::String(t.filePath);
                eti.key = juce::String(t.camelotKey.empty() ? t.key : t.camelotKey);
                eti.duration = t.duration;
                eti.bpm = (float)t.bpm;
                eti.energy = (int)t.energy;
                eti.selected = true;
                eti.fileSize = t.fileSize;
                m_exportTracks.push_back(eti);
            }
        }
        m_trackTable->updateContent();
        updateEstimatedSize();
        m_statusLabel->setText(BM_TJ("export.playlistAdded") + juce::String(playlists[result - 1].name),
                              juce::dontSendNotification);
    }
}

void ExportView::onSelectAll()
{
    for (auto& t : m_exportTracks) t.selected = true;
    m_trackTable->repaint();
    updateEstimatedSize();
}

void ExportView::onDeselectAll()
{
    for (auto& t : m_exportTracks) t.selected = false;
    m_trackTable->repaint();
    updateEstimatedSize();
}

void ExportView::onRemoveSelected()
{
    int sel = m_trackTable->getSelectedRow();
    if (sel >= 0 && sel < (int)m_exportTracks.size())
    {
        m_exportTracks.erase(m_exportTracks.begin() + sel);
        m_trackTable->updateContent();
        updateEstimatedSize();
        m_statusLabel->setText(juce::String((int)m_exportTracks.size()) + " " + BM_TJ("analysis.stats.tracks"),
                              juce::dontSendNotification);
    }
}

void ExportView::updateEstimatedSize()
{
    int selectedCount = 0;
    double totalDurationSec = 0;
    juce::int64 totalOrigSize = 0;

    for (auto& t : m_exportTracks)
    {
        if (t.selected)
        {
            selectedCount++;
            totalDurationSec += t.duration;
            totalOrigSize += t.fileSize;
        }
    }

    double sizeMB = 0;
    int formatId = m_formatCombo->getSelectedId();
    int bitrateId = m_bitrateCombo->getSelectedId();

    if (formatId == 1) // MP3
    {
        int kbps[] = {128, 192, 256, 320, 256}; // VBR ~256
        int br = (bitrateId >= 1 && bitrateId <= 5) ? kbps[bitrateId - 1] : 320;
        sizeMB = (totalDurationSec * br / 8.0) / (1024.0 * 1024.0);
    }
    else if (formatId == 2) // WAV
    {
        int bitDepth = m_bitDepthCombo->getSelectedId() == 2 ? 24 : 16;
        int sampleRate = m_sampleRateCombo->getSelectedId() == 2 ? 48000 : 44100;
        sizeMB = (totalDurationSec * sampleRate * 2.0 * (bitDepth / 8.0)) / (1024.0 * 1024.0);
    }
    else if (formatId == 3) // FLAC
    {
        int bitDepth = m_bitDepthCombo->getSelectedId() == 2 ? 24 : 16;
        int sampleRate = m_sampleRateCombo->getSelectedId() == 2 ? 48000 : 44100;
        sizeMB = (totalDurationSec * sampleRate * 2.0 * (bitDepth / 8.0) * 0.6) / (1024.0 * 1024.0);
    }
    else if (formatId == 4) // OGG
    {
        sizeMB = (totalDurationSec * 256 / 8.0) / (1024.0 * 1024.0);
    }
    else if (formatId == 5) // AAC
    {
        sizeMB = (totalDurationSec * 256 / 8.0) / (1024.0 * 1024.0);
    }
    else if (formatId == 6) // AIFF
    {
        int bitDepth = m_bitDepthCombo->getSelectedId() == 2 ? 24 : 16;
        int sampleRate = m_sampleRateCombo->getSelectedId() == 2 ? 48000 : 44100;
        sizeMB = (totalDurationSec * sampleRate * 2.0 * (bitDepth / 8.0)) / (1024.0 * 1024.0);
    }

    m_estimatedSizeMB = sizeMB;

    if (sizeMB >= 1024)
        m_sizeLabel->setText(juce::String::formatted(BM_T("export.size.gbFmt").c_str(), sizeMB / 1024.0, selectedCount), juce::dontSendNotification);
    else
        m_sizeLabel->setText(juce::String::formatted(BM_T("export.size.mbFmt").c_str(), (int)sizeMB, selectedCount), juce::dontSendNotification);
}

void ExportView::onBrowseDestination()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        BM_TJ("export.chooseDest"),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            auto dir = fc.getResult();
            if (dir == juce::File{}) return;
            m_destEdit->setText(dir.getFullPathName());
            Prefs::setString("export.lastDest", dir.getFullPathName().toStdString());
            updateEstimatedSize();
        });
}

Services::Export::BatchExportSettings ExportView::collectSettings() const
{
    Services::Export::BatchExportSettings s;
    s.formatId = m_formatCombo->getSelectedId();
    const int bitrateId = m_bitrateCombo->getSelectedId();
    static const int kbps[] = { 128, 192, 256, 320, 320 };
    s.bitRateKbps = (bitrateId >= 1 && bitrateId <= 5) ? kbps[bitrateId - 1] : 320;
    s.vbr = bitrateId == 5;
    s.sampleRate = m_sampleRateCombo->getSelectedId() == 2 ? 48000 : 44100;
    s.bitDepth = m_bitDepthCombo->getSelectedId() == 2 ? 24 : 16;
    s.normalize = m_normalizeCheck->getToggleState();
    s.targetLufs = static_cast<float>(m_lufsSlider->getValue());
    s.writeTags = m_writeTagsCheck->getToggleState();
    s.writeM3U = m_writeM3UCheck && m_writeM3UCheck->getToggleState();
    s.structureId = m_structureCombo->getSelectedId();
    s.destinationDir = m_destEdit->getText().toStdString();
    s.targetRekordbox = m_djRekordbox;
    s.targetSerato = m_djSerato;
    s.targetTraktor = m_djTraktor;
    s.targetVirtualDJ = m_djVirtualDJ;
    return s;
}

void ExportView::applySettings(const Services::Export::BatchExportSettings& s)
{
    auto applyId = [](juce::ComboBox* c, int id) {
        if (c && id >= 1 && id <= c->getNumItems())
            c->setSelectedId(id, juce::sendNotification);
    };
    applyId(m_formatCombo.get(), s.formatId);
    int bitrateId = 4;
    if (s.vbr) bitrateId = 5;
    else if (s.bitRateKbps <= 128) bitrateId = 1;
    else if (s.bitRateKbps <= 192) bitrateId = 2;
    else if (s.bitRateKbps <= 256) bitrateId = 3;
    applyId(m_bitrateCombo.get(), bitrateId);
    applyId(m_sampleRateCombo.get(), s.sampleRate == 48000 ? 2 : 1);
    applyId(m_bitDepthCombo.get(), s.bitDepth >= 24 ? 2 : 1);
    applyId(m_structureCombo.get(), s.structureId);

    m_normalizeCheck->setToggleState(s.normalize, juce::dontSendNotification);
    Prefs::setBool("export.normalize", s.normalize);
    m_lufsSlider->setEnabled(s.normalize);
    m_lufsSlider->setValue(s.targetLufs, juce::sendNotificationSync);
    m_writeTagsCheck->setToggleState(s.writeTags, juce::dontSendNotification);
    Prefs::setBool("export.writeTags", s.writeTags);
    if (m_writeM3UCheck)
    {
        m_writeM3UCheck->setToggleState(s.writeM3U, juce::dontSendNotification);
        Prefs::setBool("export.writeM3U", s.writeM3U);
    }

    m_djRekordbox = s.targetRekordbox;
    m_djSerato = s.targetSerato;
    m_djTraktor = s.targetTraktor;
    m_djVirtualDJ = s.targetVirtualDJ;
    Prefs::setBool("export.dj.rekordbox", m_djRekordbox);
    Prefs::setBool("export.dj.serato", m_djSerato);
    Prefs::setBool("export.dj.traktor", m_djTraktor);
    Prefs::setBool("export.dj.virtualdj", m_djVirtualDJ);

    updateEstimatedSize();
    repaint();
}

void ExportView::refreshPresetCombo()
{
    if (!m_presetCombo) return;
    m_presetCombo->clear(juce::dontSendNotification);
    auto presets = Services::Export::ExportPresetStore::loadAll();
    int id = 1;
    for (const auto& p : presets)
        m_presetCombo->addItem(juce::String::fromUTF8(p.name.c_str()), id++);
}

void ExportView::onSavePreset()
{
    auto window = std::make_shared<juce::AlertWindow>(BM_TJ("export.presets.save"),
                                                      BM_TJ("export.presets.namePrompt"),
                                                      juce::MessageBoxIconType::QuestionIcon);
    window->addTextEditor("name", m_presetCombo->getText(), {});
    window->addButton(BM_TJ("export.presets.save"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(BM_TJ("export.cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int result) {
        if (result != 1) return;
        const juce::String name = window->getTextEditorContents("name").trim();
        if (name.isEmpty()) return;
        Services::Export::ExportPreset preset;
        preset.name = name.toStdString();
        preset.settings = collectSettings();
        Services::Export::ExportPresetStore::upsert(preset);
        refreshPresetCombo();
        for (int i = 0; i < m_presetCombo->getNumItems(); ++i)
            if (m_presetCombo->getItemText(i) == name)
                m_presetCombo->setSelectedId(i + 1, juce::dontSendNotification);
        m_statusLabel->setText(BM_TJ("export.presets.saved"), juce::dontSendNotification);
    }), false);
}

void ExportView::onDeletePreset()
{
    const juce::String name = m_presetCombo->getText();
    if (name.isEmpty()) return;
    if (Services::Export::ExportPresetStore::remove(name.toStdString()))
    {
        refreshPresetCombo();
        m_presetCombo->setSelectedId(0, juce::dontSendNotification);
    }
}

void ExportView::setExportingState(bool exporting)
{
    m_isExporting = exporting;
    m_cancelBtn->setEnabled(exporting);
    m_exportBtn->setEnabled(!exporting);
    m_addTracksBtn->setEnabled(!exporting);
    m_addPlaylistBtn->setEnabled(!exporting);
    m_removeSelBtn->setEnabled(!exporting);
    if (exporting)
        startTimerHz(30);
    else
        stopTimer();
    repaint();
}

void ExportView::onStartExport()
{
    if (m_isExporting || !m_exportService)
        return;

    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            if (auto* lic = g_serviceLocator->tryGet<Services::Security::LicenseService>()) {
                if (! lic->canUseExport()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                        juce::String::fromUTF8("Export"),
                        juce::String::fromUTF8(
                            "L'export de fichiers audio fait partie des versions payantes "
                            "de BeatMate.\n\nActivez votre licence dans Param\xc3\xa8tres > Licence "
                            "pour exporter votre musique."),
                        BM_TJ("common.ok"));
                    return;
                }
            }
        }
    }

    if (m_destEdit->getText().isEmpty())
    {
        m_statusLabel->setText(BM_TJ("export.selectDest"), juce::dontSendNotification);
        auto chooser = std::make_shared<juce::FileChooser>(
            BM_TJ("export.chooseDest"),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory));
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto dir = fc.getResult();
                if (dir == juce::File{}) return;
                m_destEdit->setText(dir.getFullPathName());
                Prefs::setString("export.lastDest", dir.getFullPathName().toStdString());
                updateEstimatedSize();
                onStartExport();
            });
        return;
    }

    std::vector<Services::Export::BatchExportItem> items;
    for (const auto& t : m_exportTracks)
    {
        if (!t.selected) continue;
        Services::Export::BatchExportItem item;
        item.trackId = t.trackId;
        item.sourcePath = t.filePath.toStdString();
        item.title = t.title.toStdString();
        item.artist = t.artist.toStdString();
        item.durationSec = t.duration;
        items.push_back(std::move(item));
    }
    if (items.empty())
    {
        m_statusLabel->setText(BM_TJ("export.noTrack"), juce::dontSendNotification);
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("export.noTrack"), {}, Widgets::ToastNotifier::Kind::Warning, 6000);
        return;
    }

    auto settings = collectSettings();
    if ((settings.formatId == 1 || settings.formatId == 5)
        && !Services::Export::BatchExportService::isFfmpegAvailable())
    {
        const juce::String targetExt = juce::String::fromUTF8(
            Services::Export::BatchExportService::formatExtension(settings.formatId).c_str());
        bool needsEncode = settings.normalize;
        if (!needsEncode)
        {
            for (const auto& it : items)
            {
                if (!juce::String::fromUTF8(it.sourcePath.c_str()).endsWithIgnoreCase(targetExt))
                {
                    needsEncode = true;
                    break;
                }
            }
        }
        if (needsEncode)
        {
            m_statusLabel->setText(BM_TJ("export.ffmpegMissing"), juce::dontSendNotification);
            Widgets::ToastNotifier::getInstance().show(
                BM_TJ("export.ffmpegMissing"), {}, Widgets::ToastNotifier::Kind::Error, 8000);
            return;
        }
    }

    m_progress = 0.0;
    m_lastDone = 0;
    m_lastTotal = static_cast<int>(items.size());
    m_exportStartTime = juce::Time::getMillisecondCounter();

    Services::Export::BatchExportService::Callbacks callbacks;
    juce::Component::SafePointer<ExportView> self(this);
    callbacks.onProgress = [self](int done, int total, juce::String current) {
        auto* view = self.getComponent();
        if (!view) return;
        view->m_lastDone = done;
        view->m_lastTotal = total;
        view->m_progress = total > 0 ? static_cast<double>(done) / total : 0.0;
        view->m_statusLabel->setText("Export: " + current + " (" + juce::String(done)
                                     + "/" + juce::String(total) + ")",
                                     juce::dontSendNotification);
        const juce::int64 elapsed = juce::Time::getMillisecondCounter() - view->m_exportStartTime;
        if (done > 0)
        {
            const double msPerTrack = static_cast<double>(elapsed) / done;
            const int remaining = static_cast<int>((total - done) * msPerTrack / 1000.0);
            view->m_timeLabel->setText(BM_TJ("export.timeRemaining") + " "
                                       + juce::String(remaining / 60) + ":"
                                       + juce::String::formatted("%02d", remaining % 60),
                                       juce::dontSendNotification);
        }
    };
    callbacks.onFinished = [self](Services::Export::BatchExportReport report) {
        if (auto* view = self.getComponent())
            view->onExportFinished(report);
    };

    if (!m_exportService->start(std::move(items), std::move(settings), std::move(callbacks)))
    {
        m_statusLabel->setText(BM_TJ("export.selectDest"), juce::dontSendNotification);
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("export.selectDest"), m_destEdit->getText(),
            Widgets::ToastNotifier::Kind::Error, 8000);
        return;
    }

    Prefs::setString("export.lastDest", m_destEdit->getText().toStdString());
    setExportingState(true);
    m_listeners.call(&Listener::exportRequested);
    m_statusLabel->setText(BM_TJ("export.inProgress"), juce::dontSendNotification);
}

void ExportView::onExportFinished(const Services::Export::BatchExportReport& report)
{
    setExportingState(false);
    m_progress = 1.0;
    m_timeLabel->setText({}, juce::dontSendNotification);

    if (report.cancelled)
        m_statusLabel->setText(BM_TJ("export.cancelled") + juce::String(report.succeeded)
                               + " " + BM_TJ("export.filesExported"),
                               juce::dontSendNotification);
    else
        m_statusLabel->setText(BM_TJ("export.done") + juce::String(report.succeeded)
                               + " " + BM_TJ("export.filesExportedPlural"),
                               juce::dontSendNotification);

    m_listeners.call([&report](Listener& l) { l.exportCompleted(report.succeeded); });
    ExportReportDialog::show(report);
    repaint();
}

void ExportView::timerCallback()
{
    if (m_isExporting)
        repaint();
    else
        stopTimer();
}

bool ExportView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac"
            || ext == ".ogg" || ext == ".m4a" || ext == ".aiff" || ext == ".aif"
            || ext == ".wma" || ext == ".opus")
            return true;
        if (juce::File(f).isDirectory())
            return true;
    }
    return false;
}

void ExportView::fileDragEnter(const juce::StringArray&, int, int)
{
    m_dragHover = true;
    repaint();
}

void ExportView::fileDragExit(const juce::StringArray&)
{
    m_dragHover = false;
    repaint();
}

void ExportView::filesDropped(const juce::StringArray& files, int, int)
{
    m_dragHover = false;
    addDroppedFiles(files);
    repaint();
}

void ExportView::mouseUp(const juce::MouseEvent& e)
{
    if (m_isExporting)
        return;
    bool* flags[4] = { &m_djRekordbox, &m_djSerato, &m_djTraktor, &m_djVirtualDJ };
    static const char* keys[4] = { "export.dj.rekordbox", "export.dj.serato",
                                   "export.dj.traktor", "export.dj.virtualdj" };
    for (int i = 0; i < 4; ++i)
    {
        if (m_djCardRects[i].contains(e.getPosition()))
        {
            *flags[i] = !*flags[i];
            Prefs::setBool(keys[i], *flags[i]);
            repaint();
            return;
        }
    }
}

void ExportView::addDroppedFiles(const juce::StringArray& files)
{
    int added = 0;
    for (const auto& fp : files)
    {
        juce::File f(fp);
        juce::Array<juce::File> targets;
        if (f.isDirectory())
            targets = f.findChildFiles(juce::File::findFiles, true,
                                       "*.mp3;*.wav;*.flac;*.aac;*.ogg;*.m4a;*.aiff;*.aif;*.wma;*.opus");
        else if (f.existsAsFile())
            targets.add(f);

        for (const auto& t : targets)
        {
            bool already = false;
            for (const auto& e : m_exportTracks)
                if (e.filePath == t.getFullPathName()) { already = true; break; }
            if (already) continue;

            ExportTrackInfo eti;
            eti.title = t.getFileNameWithoutExtension();
            eti.filePath = t.getFullPathName();
            eti.fileSize = t.getSize();
            m_exportTracks.push_back(eti);
            ++added;
        }
    }
    if (added > 0)
    {
        m_trackTable->updateContent();
        updateEstimatedSize();
        m_statusLabel->setText(juce::String((int)m_exportTracks.size()) + " " + BM_TJ("analysis.stats.tracks"),
                              juce::dontSendNotification);
    }
}

void ExportView::onCancel()
{
    if (m_exportService)
        m_exportService->cancel();
}

void ExportView::updateProgress(int current, int total, const juce::String& fileName)
{
    m_progress = total > 0 ? (double)current / total : 0.0;
    m_statusLabel->setText("Export: " + fileName, juce::dontSendNotification);
    if (current >= total)
    {
        m_statusLabel->setText(juce::String::formatted(BM_T("export.completedFmt").c_str(), total),
                              juce::dontSendNotification);
        m_cancelBtn->setEnabled(false);
        m_exportBtn->setEnabled(true);
        m_listeners.call([total](Listener& l) { l.exportCompleted(total); });
    }
    repaint();
}

void ExportView::onExportPlaylistM3U()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Exporter en M3U", juce::File{}, "*.m3u");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto out = fc.getResult();
            if (out == juce::File{}) return;
            juce::String content = "#EXTM3U\n";
            for (auto& t : m_exportTracks) {
                if (!t.selected) continue;
                int dur = (int)t.duration;
                content += "#EXTINF:" + juce::String(dur) + "," + t.artist + " - " + t.title + "\n";
                content += t.filePath + "\n";
            }
            out.replaceWithText(content);
            m_statusLabel->setText(juce::String::fromUTF8("M3U exporté avec succès"),
                                    juce::dontSendNotification);
        });
}

void ExportView::onExportPlaylistPLS()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Exporter en PLS", juce::File{}, "*.pls");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto out = fc.getResult();
            if (out == juce::File{}) return;
            int count = 0;
            for (auto& t : m_exportTracks) if (t.selected) count++;

            juce::String content = "[playlist]\n";
            content += "NumberOfEntries=" + juce::String(count) + "\n\n";
            int idx = 1;
            for (auto& t : m_exportTracks) {
                if (!t.selected) continue;
                content += "File"   + juce::String(idx) + "=" + t.filePath + "\n";
                content += "Title"  + juce::String(idx) + "=" + t.artist + " - " + t.title + "\n";
                content += "Length" + juce::String(idx) + "=" + juce::String((int)t.duration) + "\n\n";
                idx++;
            }
            content += "Version=2\n";
            out.replaceWithText(content);
            m_statusLabel->setText(juce::String::fromUTF8("PLS exporté avec succès"),
                                    juce::dontSendNotification);
        });
}

void ExportView::onExportPlaylistPDF()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Exporter la setlist (HTML imprimable)"), juce::File{}, "*.html");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
        auto out = fc.getResult();
        if (out == juce::File{}) return;
        juce::String content;
        content += "<!DOCTYPE html>\n<html lang=\"fr\">\n<head>\n";
        content += "<meta charset=\"UTF-8\">\n";
        content += "<title>Setlist - BeatMate Export</title>\n";
        content += "<style>\n";
        content += "  body { font-family: 'Segoe UI', Arial, sans-serif; background: #0f172a; color: #e2e8f0; padding: 40px; }\n";
        content += "  h1 { color: #3b82f6; border-bottom: 2px solid #3b82f6; padding-bottom: 10px; }\n";
        content += "  h2 { color: #94a3b8; font-size: 14px; margin-top: 5px; }\n";
        content += "  table { width: 100%; border-collapse: collapse; margin-top: 20px; }\n";
        content += "  th { background: #1e293b; color: #94a3b8; text-align: left; padding: 10px 12px; font-size: 12px; text-transform: uppercase; }\n";
        content += "  td { padding: 8px 12px; border-bottom: 1px solid #1e293b; }\n";
        content += "  tr:nth-child(even) { background: #1e293b40; }\n";
        content += "  tr:hover { background: #3b82f620; }\n";
        content += "  .bpm { color: #3b82f6; font-weight: bold; }\n";
        content += "  .key { color: #8b5cf6; font-weight: bold; }\n";
        content += "  .energy { color: #f59e0b; }\n";
        content += "  .footer { margin-top: 20px; padding-top: 10px; border-top: 1px solid #334155; color: #64748b; font-size: 13px; }\n";
        content += "</style>\n</head>\n<body>\n";
        content += "<h1>SETLIST - BeatMate Export</h1>\n";
        content += "<h2>Genere par BeatMate V12 Professional</h2>\n";
        content += "<table>\n<thead><tr>";
        content += "<th>#</th><th>Titre</th><th>Artiste</th><th>BPM</th><th>Key</th><th>Energy</th><th>Duree</th>";
        content += "</tr></thead>\n<tbody>\n";

        int idx = 1;
        double totalDur = 0;
        for (auto& t : m_exportTracks)
        {
            if (!t.selected) continue;
            int mins = (int)(t.duration / 60.0);
            int secs = (int)t.duration % 60;
            totalDur += t.duration;

            content += "<tr>";
            content += "<td>" + juce::String(idx++) + "</td>";
            content += "<td>" + t.title + "</td>";
            content += "<td>" + t.artist + "</td>";
            content += "<td class=\"bpm\">" + juce::String(t.bpm, 0) + "</td>";
            content += "<td class=\"key\">" + t.key + "</td>";
            content += "<td class=\"energy\">" + juce::String(t.energy) + "</td>";
            content += "<td>" + juce::String(mins) + ":" + juce::String::formatted("%02d", secs) + "</td>";
            content += "</tr>\n";
        }

        content += "</tbody>\n</table>\n";

        int totalMin = (int)(totalDur / 60.0);
        int totalSec = (int)totalDur % 60;
        content += "<div class=\"footer\">";
        content += "<strong>Duree totale:</strong> " + juce::String(totalMin) + ":" +
                  juce::String::formatted("%02d", totalSec);
        content += " | <strong>Pistes:</strong> " + juce::String(idx - 1);
        content += " | BeatMate V12 Professional par Sebastien Sainte-Foi";
        content += "</div>\n";
        content += "</body>\n</html>\n";

        out.replaceWithText(content);
        m_statusLabel->setText(juce::String::fromUTF8("Setlist HTML exportée avec succès"),
                                juce::dontSendNotification);
        });
}

void ExportView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    if (m_dragHover)
    {
        auto r = getLocalBounds().toFloat().reduced(8.0f);
        g.setColour(Colors::primary().withAlpha(0.10f));
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(Colors::primary());
        float dashes[] = { 8.0f, 6.0f };
        juce::Path p;
        p.addRoundedRectangle(r, 12.0f);
        juce::PathStrokeType stroke(2.0f);
        juce::Path dashed;
        stroke.createDashedStroke(dashed, p, dashes, 2);
        g.fillPath(dashed);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(BM_TJ("export.dropOverlay"),
                   getLocalBounds(), juce::Justification::centred);
    }

    auto drawGlassPanel = [&](juce::Rectangle<int> r, float = 0.08f)
    {
        ProDraw::glassPanel(g, r.toFloat(), 12.0f);
    };

    int margin = 24;
    int rightX = getWidth() * 2 / 3;
    int topBarH = 40;
    int headerH = 40;
    int bottomH = 80;

    drawGlassPanel({margin, headerH + topBarH + 8, rightX - margin - 16, getHeight() - headerH - topBarH - bottomH - 16});
    drawGlassPanel({rightX, headerH + topBarH + 8, getWidth() - rightX - margin, getHeight() - headerH - topBarH - bottomH - 16});
    drawGlassPanel({margin, getHeight() - bottomH - 4, getWidth() - margin * 2, bottomH}, 0.05f);

    int colY = headerH + topBarH + 14;
    int cx = margin + 8;
    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    g.drawText(BM_TJ("norm.col.sel").toUpperCase(), cx, colY, 30, 14, juce::Justification::centredLeft);
    g.drawText(BM_TJ("analysis.col.title").toUpperCase(), cx + 32, colY, 200, 14, juce::Justification::centredLeft);
    g.drawText(BM_TJ("analysis.col.artist").toUpperCase(), cx + 256, colY, 140, 14, juce::Justification::centredLeft);
    g.drawText("BPM", cx + 420, colY, 60, 14, juce::Justification::centredLeft);
    g.drawText("KEY", cx + 488, colY, 40, 14, juce::Justification::centred);
    g.drawText(BM_TJ("export.col.duration").toUpperCase(), cx + 532, colY, 50, 14, juce::Justification::centred);

    ProDraw::sectionHeader(g, BM_TJ("export.optionsHeader"), rightX + 12, headerH + topBarH + 10,
                           Colors::secondary());

    int rx = rightX + 12;
    int rw = getWidth() - rightX - margin - 12;

    {
        struct PlatformInfo { const char* name; juce::uint32 color; bool enabled; };
        PlatformInfo platforms[] = {
            {"Rekordbox", 0xFF2563EB, m_djRekordbox},
            {"Serato",    0xFF06B6D4, m_djSerato},
            {"Traktor",   0xFFF97316, m_djTraktor},
            {"VirtualDJ", 0xFF8B5CF6, m_djVirtualDJ}
        };

        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::bold));
        g.setColour(Colors::textSecondary());
        int headerY = m_exportBtn->getBottom() + 8;
        g.drawText(BM_TJ("export.djTargets.header"), rx, headerY, rw, 12,
                   juce::Justification::centredLeft);

        int cardY = headerY + 14;
        int cardW = (rw - 12) / 4;
        int cardH = 30;

        for (int i = 0; i < 4; ++i) {
            int cx = rx + i * (cardW + 4);
            m_djCardRects[i] = { cx, cardY, cardW, cardH };
            juce::Colour c(platforms[i].color);
            const bool on = platforms[i].enabled;
            g.setColour(c.withAlpha(on ? 0.28f : 0.06f));
            g.fillRoundedRectangle((float)cx, (float)cardY, (float)cardW, (float)cardH, 5.0f);
            g.setColour(c.withAlpha(on ? 1.0f : 0.25f));
            g.drawRoundedRectangle((float)cx + 0.5f, (float)cardY + 0.5f,
                                   (float)cardW - 1.0f, (float)cardH - 1.0f, 5.0f, on ? 1.6f : 1.0f);
            g.setColour(on ? juce::Colours::white : c.withAlpha(0.65f));
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText(platforms[i].name, cx, cardY + (on ? 4 : 0), cardW, on ? 14 : cardH,
                       juce::Justification::centred);
            if (on)
            {
                g.setColour(c.brighter(0.4f));
                g.setFont(juce::Font(9.5f, juce::Font::bold));
                g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), cx, cardY + 15, cardW, 13,
                           juce::Justification::centred);
            }
        }
    }

    {
        int barY = m_sizeLabel->getBottom() + 4;
        int barX = rx;
        int barW = rw;
        int barH = 14;

        g.setColour(Colors::bgLightest().withAlpha(0.4f));
        g.fillRoundedRectangle((float)barX, (float)barY, (float)barW, (float)barH, 7.0f);

        double maxSizeMB = 10000.0; // 10 GB reference max
        float fillRatio = juce::jlimit(0.0f, 1.0f, (float)(m_estimatedSizeMB / maxSizeMB));
        juce::Colour sizeCol = (m_estimatedSizeMB < 1024) ? Colors::success()
                             : (m_estimatedSizeMB < 4096) ? Colors::warning()
                             : Colors::error();

        juce::ColourGradient sizeGrad(sizeCol.withAlpha(0.7f), (float)barX, (float)barY,
                                       sizeCol.withAlpha(0.3f), (float)(barX + barW * fillRatio), (float)barY, false);
        g.setGradientFill(sizeGrad);
        g.fillRoundedRectangle((float)barX, (float)barY, (float)(barW * fillRatio), (float)barH, 7.0f);

        float tick1GB = (float)(1024.0 / maxSizeMB) * barW;
        float tick4GB = (float)(4096.0 / maxSizeMB) * barW;
        g.setColour(Colors::textDim().withAlpha(0.5f));
        g.drawVerticalLine((int)(barX + tick1GB), (float)barY, (float)(barY + barH));
        g.drawVerticalLine((int)(barX + tick4GB), (float)barY, (float)(barY + barH));

        g.setFont(juce::Font(7.0f));
        g.setColour(Colors::textDim());
        g.drawText("1GB", (int)(barX + tick1GB - 10), barY + barH + 1, 20, 10, juce::Justification::centred);
        g.drawText("4GB", (int)(barX + tick4GB - 10), barY + barH + 1, 20, 10, juce::Justification::centred);

        int badgeY = m_exportBtn->getBottom() + 60;
        int formatId = m_formatCombo->getSelectedId();
        juce::String srcQuality = "Source";
        juce::String dstQuality;
        juce::Colour srcBadgeCol = Colors::success();
        juce::Colour dstBadgeCol;

        switch (formatId)
        {
            case 1: // MP3
            {
                int bitrateId = m_bitrateCombo->getSelectedId();
                juce::String brLabels[] = {"128k", "192k", "256k", "320k", "VBR"};
                dstQuality = "MP3 " + (bitrateId >= 1 && bitrateId <= 5 ? brLabels[bitrateId - 1] : juce::String("320k"));
                dstBadgeCol = (bitrateId >= 4) ? Colors::success() : (bitrateId >= 2) ? Colors::warning() : Colors::error();
                break;
            }
            case 2: dstQuality = "WAV Lossless"; dstBadgeCol = Colors::success(); break;
            case 3: dstQuality = "FLAC Lossless"; dstBadgeCol = Colors::success(); break;
            case 4: dstQuality = "OGG ~256k"; dstBadgeCol = Colors::warning(); break;
            case 5: dstQuality = "AAC ~256k"; dstBadgeCol = Colors::warning(); break;
            case 6: dstQuality = "AIFF Lossless"; dstBadgeCol = Colors::success(); break;
            default: dstQuality = "-"; dstBadgeCol = Colors::textDim(); break;
        }

        auto drawBadge = [&](int bx, const juce::String& label, juce::Colour col) {
            int bw = 80;
            g.setColour(col.withAlpha(0.12f));
            g.fillRoundedRectangle((float)bx, (float)badgeY, (float)bw, 20.0f, 6.0f);
            g.setColour(col);
            g.drawRoundedRectangle((float)bx, (float)badgeY, (float)bw, 20.0f, 6.0f, 1.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText(label, bx, badgeY, bw, 20, juce::Justification::centred);
        };

        drawBadge(rx, srcQuality, srcBadgeCol);

        g.setColour(Colors::textDim());
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText("->", rx + 82, badgeY, 20, 20, juce::Justification::centred);

        drawBadge(rx + 104, dstQuality, dstBadgeCol);
    }

    int pbY = getHeight() - bottomH + 16;
    int pbX = margin + 12;
    int pbW = getWidth() - margin * 2 - 24;
    int pbH = 18;

    g.setColour(Colors::bgLightest().withAlpha(0.5f));
    g.fillRoundedRectangle((float)pbX, (float)pbY, (float)pbW, (float)pbH, 9.0f);
    g.setColour(Colors::bgDarkest().withAlpha(0.3f));
    g.fillRoundedRectangle((float)pbX, (float)pbY, (float)pbW, 3.0f, 1.5f);

    if (m_progress > 0)
    {
        float fillW = (float)(pbW * m_progress);

        float animPhase = (float)(juce::Time::getMillisecondCounter() % 2000) / 2000.0f;
        juce::ColourGradient progGrad(Colors::primary(), (float)pbX, (float)pbY,
                                       Colors::accent(), (float)(pbX + fillW), (float)pbY, false);
        progGrad.addColour(0.3 + animPhase * 0.2, Colors::primaryHover());
        progGrad.addColour(0.6 + animPhase * 0.15, Colors::secondary().withAlpha(0.6f));
        g.setGradientFill(progGrad);
        g.fillRoundedRectangle((float)pbX, (float)pbY, fillW, (float)pbH, 9.0f);

        juce::ColourGradient shimmer(juce::Colours::white.withAlpha(0.2f), (float)pbX, (float)pbY,
                                      juce::Colours::white.withAlpha(0.0f), (float)pbX, (float)(pbY + pbH), false);
        g.setGradientFill(shimmer);
        g.fillRoundedRectangle((float)pbX, (float)pbY, fillW, (float)(pbH / 2), 9.0f);

        if (fillW > 4)
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.fillRoundedRectangle((float)(pbX + fillW - 6), (float)pbY, 6.0f, (float)pbH, 3.0f);
        }

        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawText(juce::String((int)(m_progress * 100)) + "%",
                   pbX + 1, pbY + 1, pbW, pbH, juce::Justification::centred);
        g.setColour(Colors::textPrimary());
        g.drawText(juce::String((int)(m_progress * 100)) + "%",
                   pbX, pbY, pbW, pbH, juce::Justification::centred);

        if (m_isExporting)
            repaint();
    }

    g.setFont(juce::Font("Segoe UI", 10.0f, juce::Font::bold));
    g.setColour(Colors::textSecondary());
    int plLabelY = getHeight() - bottomH + 42;
    g.drawText(BM_TJ("export.playlistLabel"), margin + 12, plLabelY, 140, 14, juce::Justification::centredLeft);

    if (m_isExporting)
        ProDraw::statusPill(g, BM_TJ("export.inProgress"),
                            { (float)(getWidth() - margin - 180), (float)(getHeight() - bottomH + 40), 168.0f, 22.0f },
                            Colors::primary(), true);

    ProDraw::vignette(g, (float)getWidth(), (float)getHeight());
}

void ExportView::resized()
{
    int margin = 24;
    int rightX = getWidth() * 2 / 3;
    int topBarH = 40;
    int headerH = 40;
    int bottomH = 80;

    m_titleLabel->setBounds(margin, 8, 200, 28);

    int bx = margin;
    int by = headerH + 4;
    int bh = 28;
    m_addTracksBtn->setBounds(bx, by, 120, bh); bx += 126;
    m_addPlaylistBtn->setBounds(bx, by, 120, bh); bx += 126;
    m_selectAllBtn->setBounds(bx, by, 130, bh); bx += 136;
    m_deselectAllBtn->setBounds(bx, by, 140, bh); bx += 146;
    m_removeSelBtn->setBounds(bx, by, 130, bh);

    m_trackTable->setBounds(margin + 4, headerH + topBarH + 30,
                           rightX - margin - 20, getHeight() - headerH - topBarH - bottomH - 40);

    int rx = rightX + 12;
    int rw = getWidth() - rightX - margin - 12;
    int ry = headerH + topBarH + 34;
    int rowH = 32;

    if (m_presetLabel && m_presetCombo && m_presetSaveBtn && m_presetDeleteBtn)
    {
        m_presetLabel->setBounds(rx, ry, 58, 20);
        m_presetCombo->setBounds(rx + 60, ry, rw - 60 - 132, 24);
        m_presetSaveBtn->setBounds(rx + rw - 128, ry, 62, 24);
        m_presetDeleteBtn->setBounds(rx + rw - 62, ry, 62, 24);
        ry += rowH;
    }

    m_formatLabel->setBounds(rx, ry, 80, 20);
    m_formatCombo->setBounds(rx + 85, ry, rw - 90, 26); ry += rowH;

    m_qualityLabel->setBounds(rx, ry, 80, 20);
    m_bitrateCombo->setBounds(rx + 85, ry, rw - 90, 26); ry += rowH;

    m_sampleRateLabel->setBounds(rx, ry, 80, 20);
    m_sampleRateCombo->setBounds(rx + 85, ry, rw - 90, 26); ry += rowH;

    m_bitDepthLabel->setBounds(rx, ry, 80, 20);
    m_bitDepthCombo->setBounds(rx + 85, ry, rw - 90, 26); ry += rowH;

    m_normalizeCheck->setBounds(rx, ry, 120, 24);
    m_lufsLabel->setBounds(rx + 120, ry, 70, 20);
    m_lufsSlider->setBounds(rx + 190, ry + 4, rw - 195, 16); ry += rowH;

    if (m_presetSpotifyBtn && m_presetAppleBtn && m_presetYouTubeBtn && m_presetClubBtn) {
        const int colW = (rw - 24) / 4;
        m_presetSpotifyBtn->setBounds(rx + 0 * (colW + 8), ry, colW, 22);
        m_presetAppleBtn  ->setBounds(rx + 1 * (colW + 8), ry, colW, 22);
        m_presetYouTubeBtn->setBounds(rx + 2 * (colW + 8), ry, colW, 22);
        m_presetClubBtn   ->setBounds(rx + 3 * (colW + 8), ry, colW, 22);
        ry += 22 + 6;
    }

    m_writeTagsCheck->setBounds(rx, ry, rw / 2 - 4, 24);
    if (m_writeM3UCheck) m_writeM3UCheck->setBounds(rx + rw / 2, ry, rw / 2, 24);
    ry += rowH + 8;

    m_destLabel->setBounds(rx, ry, 80, 20);
    m_destEdit->setBounds(rx, ry + 22, rw - 100, 26);
    m_browseBtn->setBounds(rx + rw - 95, ry + 22, 90, 26); ry += 56;

    m_structLabel->setBounds(rx, ry, 80, 20);
    m_structureCombo->setBounds(rx + 85, ry, rw - 90, 26); ry += rowH;

    m_sizeLabel->setBounds(rx, ry, rw, 20); ry += 24;

    // Reserve space for platform cards (44px) + size bar (30px) + badges (30px)
    ry += 44 + 30 + 30 + 8;

    m_exportBtn->setBounds(rx, ry, (rw - 8) / 2, 34);
    m_cancelBtn->setBounds(rx + (rw - 8) / 2 + 8, ry, (rw - 8) / 2, 34);

    int bbY = getHeight() - bottomH + 42;
    m_statusLabel->setBounds(margin + 12, getHeight() - 28, 350, 18);
    m_timeLabel->setBounds(getWidth() - margin - 200, getHeight() - 28, 190, 18);

    m_exportM3UBtn->setBounds(margin + 140, bbY, 60, 26);
    m_exportPLSBtn->setBounds(margin + 206, bbY, 60, 26);
    m_exportPDFBtn->setBounds(margin + 272, bbY, 100, 26);
}

} // namespace BeatMate::UI
