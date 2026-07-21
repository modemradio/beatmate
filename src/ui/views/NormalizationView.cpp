#include "NormalizationView.h"
#include "../styles/ColorPalette.h"
#include "../widgets/ToastNotifier.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/library/PeakFileService.h"
#include "../../services/config/I18n.h"
#include "../../app/Application.h"
#include "../utils/ViewPrefs.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
namespace BeatMate::UI {

void NormalizationView::TrackModel::paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool sel){
    if(row<0||row>=(int)entries.size()) return;
    if(sel){g.setColour(Colors::primary().withAlpha(0.25f));g.fillRect(0,0,w,h);}
    else if(row%2==1){g.setColour(Colors::bgDark());g.fillRect(0,0,w,h);}
    auto& e=entries[row];
    g.setFont(juce::Font(12.0f));

    int cx = 8;
    g.setColour(Colors::border());
    g.drawRoundedRectangle((float)cx, (float)(h/2 - 7), 14.0f, 14.0f, 3.0f, 1.0f);
    if (e.selected) {
        g.setColour(Colors::primary());
        g.fillRoundedRectangle((float)(cx + 2), (float)(h/2 - 5), 10.0f, 10.0f, 2.0f);
        g.setColour(Colors::bgDarkest());
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), cx, h/2 - 7, 14, 14, juce::Justification::centred);
        g.setFont(juce::Font(12.0f));
    }
    cx += 22;

    g.setColour(sel?Colors::textPrimary():Colors::textSecondary());
    g.drawText(e.title, cx, 0, 200, h, juce::Justification::centredLeft); cx += 200;
    g.drawText(e.artist, cx, 0, 150, h, juce::Justification::centredLeft); cx += 150;
    g.setFont(juce::FontOptions("Consolas", 11.5f, juce::Font::plain));
    g.drawText(e.currentLUFS, cx, 0, 100, h, juce::Justification::centred); cx += 100;

    {
        juce::Colour pillText;
        bool pulse = false;
        if (e.status.containsIgnoreCase("termin") || e.status.containsIgnoreCase("normalis")) {
            pillText = Colors::success();
        } else if (e.status.containsIgnoreCase("en cours")) {
            pillText = Colors::primary();
            pulse = true;
        } else if (e.status.containsIgnoreCase("attente")) {
            pillText = Colors::warning();
        } else if (e.status.containsIgnoreCase("erreur") || e.status.containsIgnoreCase("error")) {
            pillText = Colors::error();
        } else {
            pillText = Colors::textMuted();
        }
        float pillX = (float)cx + 2.0f;
        float pillY = (float)(h / 2 - 10);
        float pillW = juce::jmin(120.0f, (float)(w - cx - 10));
        ProDraw::statusPill(g, e.status, { pillX, pillY, pillW, 20.0f }, pillText, pulse);
    }

    g.setColour(Colors::bgMedium());g.drawHorizontalLine(h-1,0.0f,(float)w);
}

void NormalizationView::TrackModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row >= 0 && row < (int)entries.size()) {
        entries[row].selected = !entries[row].selected;
        if (ownerList) ownerList->repaint();
    }
}

void NormalizationView::TrackModel::selectAll(bool select) {
    for (auto& e : entries) e.selected = select;
    if (ownerList) ownerList->repaint();
}

int NormalizationView::TrackModel::getSelectedCount() const {
    int count = 0;
    for (auto& e : entries) if (e.selected) ++count;
    return count;
}

NormalizationView::NormalizationView():m_provider(nullptr){setupUI();}

NormalizationView::NormalizationView(Services::Library::TrackDataProvider* provider):m_provider(provider){
    setupUI();
    if (m_provider) {
        loadTracksFromDatabase();
        populateGenreFilter();
        applyFilters(); // populate the table from the cache so search works immediately
        juce::Component::SafePointer<NormalizationView> self(this);
        m_provider->onDataChanged([self] {
            juce::MessageManager::callAsync([self] {
                if (!self) return;
                self->loadTracksFromDatabase();
                self->populateGenreFilter();
                self->applyFilters();
            });
        });
    }
}

void NormalizationView::loadTracksFromDatabase() {
    if (!m_provider) return;
    m_allDbTracks.clear();
    auto allTracks = m_provider->getAllTracks();
    for (auto& t : allTracks) {
        FullTrackEntry fe;
        fe.title = juce::String(t.title);
        fe.artist = juce::String(t.artist);
        fe.genre = juce::String(t.genre);
        fe.filePath = juce::String(t.filePath);
        fe.bpm = t.bpm;
        fe.analyzed = t.analyzed;
        m_allDbTracks.push_back(fe);
    }
}

void NormalizationView::populateGenreFilter() {
    if (!m_provider || !m_genreFilter) return;
    int currentId = m_genreFilter->getSelectedId();
    m_genreFilter->clear(juce::dontSendNotification);
    m_genreFilter->addItem(BM_TJ("common.all"), 1);

    auto genres = m_provider->getGenreDistribution();
    int id = 2;
    for (auto& gn : genres) {
        if (!gn.first.empty()) {
            m_genreFilter->addItem(juce::String(gn.first), id++);
        }
    }

    if (currentId > 0)
        m_genreFilter->setSelectedId(currentId, juce::dontSendNotification);
    else
        m_genreFilter->setSelectedId(1, juce::dontSendNotification);
}

void NormalizationView::applyFilters() {
    if (!m_trackModel || !m_trackTable) return;

    juce::String searchText = m_searchEditor ? m_searchEditor->getText().trim().toLowerCase() : juce::String();
    int genreId = m_genreFilter ? m_genreFilter->getSelectedId() : 1;
    juce::String genreText;
    if (genreId > 1 && m_genreFilter)
        genreText = m_genreFilter->getItemText(m_genreFilter->indexOfItemId(genreId));

    const juce::String targetSuffix =
        juce::String(m_targetLUFS ? m_targetLUFS->getValue() : -14.0, 1) + " LUFS";

    m_trackModel->entries.clear();
    for (const auto& t : m_allDbTracks) {
        if (searchText.isNotEmpty()) {
            if (!t.title.toLowerCase().contains(searchText) &&
                !t.artist.toLowerCase().contains(searchText))
                continue;
        }

        if (genreText.isNotEmpty()) {
            if (!t.genre.equalsIgnoreCase(genreText))
                continue;
        }

        m_trackModel->entries.push_back({
            t.title,
            t.artist,
            "-",
            targetSuffix,
            BM_TJ("norm.pending"),
            true,
            t.filePath
        });
    }
    m_trackTable->updateContent();
    m_statusLabel->setText(juce::String((int)m_trackModel->entries.size()) + " " + BM_TJ("analysis.stats.tracks"), juce::dontSendNotification);
}

void NormalizationView::addAllFilteredTracks() {
    if (!m_provider) return;

    juce::String searchText = m_searchEditor ? m_searchEditor->getText().trim().toLowerCase() : juce::String();
    int genreId = m_genreFilter ? m_genreFilter->getSelectedId() : 1;
    juce::String genreText;
    if (genreId > 1 && m_genreFilter)
        genreText = m_genreFilter->getItemText(m_genreFilter->indexOfItemId(genreId));

    auto allTracks = m_provider->getAllTracks();
    for (auto& t : allTracks) {
        juce::String title(t.title);
        juce::String artist(t.artist);
        juce::String genre(t.genre);

        if (searchText.isNotEmpty()) {
            if (!title.toLowerCase().contains(searchText) &&
                !artist.toLowerCase().contains(searchText))
                continue;
        }

        if (genreText.isNotEmpty()) {
            if (!genre.equalsIgnoreCase(genreText))
                continue;
        }

        bool alreadyAdded = false;
        for (auto& e : m_trackModel->entries) {
            if (e.title == title && e.artist == artist) {
                alreadyAdded = true;
                break;
            }
        }
        if (alreadyAdded) continue;

        m_trackModel->entries.push_back({
            title,
            artist,
            "-",
            juce::String(m_targetLUFS->getValue(), 1) + " LUFS",
            BM_TJ("norm.pending"),
            true,
            juce::String(t.filePath)
        });
    }
    m_trackTable->updateContent();
    m_statusLabel->setText(juce::String((int)m_trackModel->entries.size()) + " " + BM_TJ("analysis.stats.tracks"), juce::dontSendNotification);
}

void NormalizationView::queueTracks(const std::vector<int64_t>& trackIds) {
    if (!m_provider || !m_trackModel) return;
    int added = 0;
    for (auto id : trackIds) {
        auto t = m_provider->getTrack(id);
        if (t.id != id || t.filePath.empty()) continue;
        const juce::String path(t.filePath);
        bool alreadyAdded = false;
        for (auto& e : m_trackModel->entries) {
            if (e.filePath == path) { alreadyAdded = true; break; }
        }
        if (alreadyAdded) continue;
        m_trackModel->entries.push_back({
            juce::String(t.title),
            juce::String(t.artist),
            "-",
            juce::String(m_targetLUFS->getValue(), 1) + " LUFS",
            BM_TJ("norm.pending"),
            true,
            path
        });
        ++added;
    }
    if (added > 0 && m_trackTable) {
        m_trackTable->updateContent();
        m_statusLabel->setText(juce::String((int)m_trackModel->entries.size()) + " "
                               + BM_TJ("analysis.stats.tracks"), juce::dontSendNotification);
    }
}

void NormalizationView::removeAllTracks() {
    m_trackModel->entries.clear();
    m_trackTable->updateContent();
    m_progress = 0.0;
    m_completedCount = 0;
    m_totalCount = 0;
    m_normalizing = false;
    stopTimer();
    m_currentTrackName.clear();
    m_lufsBefore = "-";
    m_lufsAfter = "-";
    m_statusLabel->setText("0 " + BM_TJ("analysis.stats.tracks"), juce::dontSendNotification);
    m_normalizeBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    repaint();
}

void NormalizationView::setupUI(){
    m_titleLabel=std::make_unique<juce::Label>("t",BM_TJ("norm.title"));
    m_titleLabel->setFont(juce::Font(24.0f,juce::Font::bold));m_titleLabel->setColour(juce::Label::textColourId,Colors::textPrimary());addAndMakeVisible(*m_titleLabel);
    m_descLabel=std::make_unique<juce::Label>("d",BM_TJ("norm.desc"));
    m_descLabel->setFont(juce::Font(13.0f));m_descLabel->setColour(juce::Label::textColourId,Colors::textMuted());addAndMakeVisible(*m_descLabel);
    m_lufsValueLabel=std::make_unique<juce::Label>("lv","-14.0 LUFS");
    m_lufsValueLabel->setFont(juce::Font(20.0f,juce::Font::bold));m_lufsValueLabel->setColour(juce::Label::textColourId,Colors::success());addAndMakeVisible(*m_lufsValueLabel);
    m_targetLUFS=std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,juce::Slider::TextBoxRight);
    m_targetLUFS->setRange(-24,-6,0.5);m_targetLUFS->setValue(-14);
    m_targetLUFS->setColour(juce::Slider::thumbColourId,Colors::success());
    m_targetLUFS->setTextValueSuffix(" LUFS");
    m_targetLUFS->setTooltip(BM_TJ("norm.settings.targetLUFSTip"));
    m_targetLUFS->onValueChange=[this]{
        m_lufsValueLabel->setText(juce::String(m_targetLUFS->getValue(),1)+" LUFS",juce::dontSendNotification);
        Prefs::setDouble("normalize.targetLUFS", m_targetLUFS->getValue());
    };
    addAndMakeVisible(*m_targetLUFS);

    auto makeBtn=[this](const juce::String&t,juce::Colour bg=Colors::bgLighter()){
        auto b=std::make_unique<juce::TextButton>(t);b->setColour(juce::TextButton::buttonColourId,bg);
        b->setColour(juce::TextButton::buttonOnColourId,bg.brighter(0.25f));
        b->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
        b->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());addAndMakeVisible(*b);return b;};

    m_presetSpotify=makeBtn(BM_TJ("norm.preset.spotify"), juce::Colour(0xFF1DB954).darker(0.15f));
    m_presetSpotify->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_presetSpotify->setTooltip("Spotify : -14 LUFS");
    m_presetSpotify->onClick=[this]{spdlog::info("[NormalizationView] preset Spotify (-14 LUFS)");m_targetLUFS->setValue(-14); Prefs::setString("normalize.preset","spotify");};
    m_presetApple=makeBtn(BM_TJ("norm.preset.apple"), juce::Colour(0xFFFC3C44).darker(0.15f));
    m_presetApple->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_presetApple->setTooltip("Apple Music : -16 LUFS");
    m_presetApple->onClick=[this]{spdlog::info("[NormalizationView] preset Apple (-16 LUFS)");m_targetLUFS->setValue(-16); Prefs::setString("normalize.preset","apple");};
    m_presetYT=makeBtn(BM_TJ("norm.preset.youtube"), juce::Colour(0xFFCC0000));
    m_presetYT->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_presetYT->setTooltip("YouTube : -13 LUFS");
    m_presetYT->onClick=[this]{spdlog::info("[NormalizationView] preset YouTube (-13 LUFS)");m_targetLUFS->setValue(-13); Prefs::setString("normalize.preset","youtube");};
    m_presetClub=makeBtn(BM_TJ("norm.preset.club"), Colors::secondary().darker(0.1f));
    m_presetClub->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_presetClub->setTooltip("Club / sono : -9 LUFS");
    m_presetClub->onClick=[this]{spdlog::info("[NormalizationView] preset Club (-9 LUFS)");m_targetLUFS->setValue(-9); Prefs::setString("normalize.preset","club");};

    auto makeLbl=[this](const juce::String& text){
        auto lbl=std::make_unique<juce::Label>("",text);
        lbl->setFont(juce::Font(11.0f));
        lbl->setColour(juce::Label::textColourId,Colors::textMuted());
        addAndMakeVisible(*lbl);
        return lbl;
    };

    m_searchLabel = makeLbl(BM_TJ("norm.search"));
    m_searchEditor = std::make_unique<juce::TextEditor>("search");
    m_searchEditor->setTextToShowWhenEmpty(BM_TJ("norm.searchPH"), Colors::textMuted());
    m_searchEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgMedium());
    m_searchEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_searchEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_searchEditor->onTextChange = [this] { applyFilters(); };
    addAndMakeVisible(*m_searchEditor);

    m_genreLabel = makeLbl(BM_TJ("norm.genre"));
    m_genreFilter = std::make_unique<juce::ComboBox>("genreFilter");
    m_genreFilter->addItem(BM_TJ("common.all"), 1);
    m_genreFilter->setSelectedId(1, juce::dontSendNotification);
    m_genreFilter->setColour(juce::ComboBox::backgroundColourId, Colors::bgMedium());
    m_genreFilter->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    m_genreFilter->setColour(juce::ComboBox::outlineColourId, Colors::border());
    m_genreFilter->onChange = [this] { applyFilters(); };
    addAndMakeVisible(*m_genreFilter);

    m_addAllBtn = makeBtn(BM_TJ("norm.addAll"));
    m_addAllBtn->onClick = [this]{ spdlog::info("[NormalizationView] addAllBtn clicked"); addAllFilteredTracks(); };
    m_removeAllBtn = makeBtn(BM_TJ("norm.removeAll"));
    m_removeAllBtn->onClick = [this]{ spdlog::info("[NormalizationView] removeAllBtn clicked"); removeAllTracks(); };

    m_addTracksBtn=makeBtn(BM_TJ("norm.addTracks"));
    m_addTracksBtn->onClick=[this]{
        spdlog::info("[NormalizationView] addTracksBtn clicked");
        if(m_provider){
            addAllFilteredTracks();
        } else {
            juce::FileChooser c(BM_TJ("norm.selectFiles"),juce::File{},"*.mp3;*.wav;*.flac;*.aac;*.ogg");
            if(c.browseForMultipleFilesToOpen()){for(auto&f:c.getResults()){
                m_trackModel->entries.push_back({f.getFileNameWithoutExtension(),juce::String(),"-","-",BM_TJ("norm.pending"),true});
            }m_trackTable->updateContent();}
        }
    };
    m_removeBtn=makeBtn(BM_TJ("norm.remove"));
    m_removeBtn->onClick=[this]{
        int row = m_trackTable->getSelectedRow();
        if (row >= 0 && row < (int)m_trackModel->entries.size()) {
            m_trackModel->entries.erase(m_trackModel->entries.begin() + row);
            m_trackTable->updateContent();
            m_statusLabel->setText(juce::String((int)m_trackModel->entries.size()) + " " + BM_TJ("analysis.stats.tracks"), juce::dontSendNotification);
        }
    };
    m_editFileBtn=makeBtn(juce::String::fromUTF8("\xc3\x89""diter le fichier"));
    m_editFileBtn->setTooltip(juce::String::fromUTF8(
        "\xc3\x89""diteur audio fa\xc3\xa7on WavePad : couper, rogner, fondus, gain, silence, "
        "normaliser, inverser, annuler/r\xc3\xa9tablir, enregistrer"));
    m_editFileBtn->onClick=[this]{openAudioEditor();};
    m_previewBeforeBtn=makeBtn(BM_TJ("norm.measureBefore"));
    m_previewBeforeBtn->onClick=[this]{onPreview();};
    m_previewAfterBtn=makeBtn(BM_TJ("norm.measureAfter"));
    m_previewAfterBtn->onClick=[this]{onPreview();};
    m_normalizeBtn=makeBtn(BM_TJ("norm.normalize"),Colors::primary());m_normalizeBtn->onClick=[this]{spdlog::info("[NormalizationView] normalizeBtn clicked");onNormalize();};
    m_cancelBtn=makeBtn(BM_TJ("norm.cancel"));m_cancelBtn->setEnabled(false);m_cancelBtn->onClick=[this]{spdlog::info("[NormalizationView] cancelBtn clicked");onCancel();};
    m_statusLabel=std::make_unique<juce::Label>("st",BM_TJ("norm.ready"));
    m_statusLabel->setFont(juce::Font(12.0f));m_statusLabel->setColour(juce::Label::textColourId,Colors::textSecondary());addAndMakeVisible(*m_statusLabel);
    m_trackModel=std::make_unique<TrackModel>();
    m_trackTable=std::make_unique<juce::ListBox>("tl",m_trackModel.get());
    m_trackModel->ownerList = m_trackTable.get();
    m_trackTable->setRowHeight(28);m_trackTable->setColour(juce::ListBox::backgroundColourId,Colors::bgDarker());
    m_trackTable->setColour(juce::ListBox::outlineColourId,Colors::border());addAndMakeVisible(*m_trackTable);

    m_selectAllBtn=std::make_unique<juce::TextButton>(BM_TJ("norm.selectAll"));
    m_selectAllBtn->setColour(juce::TextButton::buttonColourId,Colors::bgLighter());
    m_selectAllBtn->setColour(juce::TextButton::buttonOnColourId,Colors::primary());
    m_selectAllBtn->setColour(juce::TextButton::textColourOffId,Colors::textPrimary());
    m_selectAllBtn->setColour(juce::TextButton::textColourOnId,Colors::textPrimary());
    m_selectAllBtn->onClick=[this]{
        bool allSelected = m_trackModel->getSelectedCount() == (int)m_trackModel->entries.size() && !m_trackModel->entries.empty();
        m_trackModel->selectAll(!allSelected);
        m_selectAllBtn->setButtonText(allSelected ? BM_TJ("norm.selectAll") : BM_TJ("norm.deselectAll"));
    };
    addAndMakeVisible(*m_selectAllBtn);

    {
        const double lufs = Prefs::getDouble("normalize.targetLUFS", -14.0);
        m_targetLUFS->setValue(lufs, juce::dontSendNotification);
        m_lufsValueLabel->setText(juce::String(lufs, 1) + " LUFS", juce::dontSendNotification);
    }

    retranslateUi();
}

void NormalizationView::retranslateUi(){
    if (m_titleLabel) m_titleLabel->setText(BM_TJ("norm.title"), juce::dontSendNotification);
    if (m_descLabel)  m_descLabel->setText(BM_TJ("norm.desc"), juce::dontSendNotification);

    if (m_targetLUFS) m_targetLUFS->setTooltip(BM_TJ("norm.settings.targetLUFSTip"));

    if (m_presetSpotify) m_presetSpotify->setButtonText(BM_TJ("norm.preset.spotify"));
    if (m_presetApple)   m_presetApple->setButtonText(BM_TJ("norm.preset.apple"));
    if (m_presetYT)      m_presetYT->setButtonText(BM_TJ("norm.preset.youtube"));
    if (m_presetClub)    m_presetClub->setButtonText(BM_TJ("norm.preset.club"));

    if (m_searchLabel)  m_searchLabel->setText(BM_TJ("norm.search"), juce::dontSendNotification);
    if (m_searchEditor) m_searchEditor->setTextToShowWhenEmpty(BM_TJ("norm.searchPH"), Colors::textMuted());
    if (m_genreLabel)   m_genreLabel->setText(BM_TJ("norm.genre"), juce::dontSendNotification);
    if (m_genreFilter)  m_genreFilter->changeItemText(1, BM_TJ("common.all"));

    if (m_addAllBtn)        m_addAllBtn->setButtonText(BM_TJ("norm.addAll"));
    if (m_removeAllBtn)     m_removeAllBtn->setButtonText(BM_TJ("norm.removeAll"));
    if (m_addTracksBtn)     m_addTracksBtn->setButtonText(BM_TJ("norm.addTracks"));
    if (m_removeBtn)        m_removeBtn->setButtonText(BM_TJ("norm.remove"));
    if (m_previewBeforeBtn) m_previewBeforeBtn->setButtonText(BM_TJ("norm.measureBefore"));
    if (m_previewAfterBtn)  m_previewAfterBtn->setButtonText(BM_TJ("norm.measureAfter"));
    if (m_normalizeBtn)     m_normalizeBtn->setButtonText(BM_TJ("norm.normalize"));
    if (m_cancelBtn)        m_cancelBtn->setButtonText(BM_TJ("norm.cancel"));

    if (m_selectAllBtn) {
        const bool allSelected = m_trackModel
            && !m_trackModel->entries.empty()
            && m_trackModel->getSelectedCount() == (int)m_trackModel->entries.size();
        m_selectAllBtn->setButtonText(allSelected ? BM_TJ("norm.deselectAll") : BM_TJ("norm.selectAll"));
    }

    repaint();
}

void NormalizationView::timerCallback() {
    m_pulsePhase += 0.08f;
    if (m_pulsePhase > juce::MathConstants<float>::twoPi) m_pulsePhase -= juce::MathConstants<float>::twoPi;
    m_spinnerAngle += 0.12f;
    if (m_spinnerAngle > juce::MathConstants<float>::twoPi) m_spinnerAngle -= juce::MathConstants<float>::twoPi;
    repaint();
}

void NormalizationView::onNormalize(){
    if(m_trackModel->entries.empty())return;
    m_normalizing = true;
    m_normStartTime = juce::Time::getMillisecondCounterHiRes();
    m_completedCount = 0;
    m_totalCount = (int)m_trackModel->entries.size();
    m_cancelBtn->setEnabled(true);
    m_normalizeBtn->setEnabled(false);
    m_statusLabel->setText(BM_TJ("norm.inProgress"),juce::dontSendNotification);
    startTimer(30);
    repaint();
    m_listeners.call(&Listener::normalizeRequested);
}

void NormalizationView::onPreview(){int row=m_trackTable->getSelectedRow();if(row>=0)m_listeners.call([row](Listener&l){l.previewRequested(row);});}

void NormalizationView::onCancel(){
    m_normalizing = false;
    stopTimer();
    m_cancelBtn->setEnabled(false);
    m_normalizeBtn->setEnabled(true);
    m_statusLabel->setText(BM_TJ("norm.cancelled"),juce::dontSendNotification);
    m_listeners.call(&Listener::cancelRequested);
    repaint();
}

std::vector<juce::String> NormalizationView::getTrackTitles() const {
    std::vector<juce::String> titles;
    if (!m_trackModel) return titles;

    int checkedCount = 0;
    for (auto& e : m_trackModel->entries) if (e.selected) ++checkedCount;

    if (checkedCount > 0) {
        for (auto& e : m_trackModel->entries)
            if (e.selected)
                titles.push_back(e.title);
    } else {
        for (auto& e : m_trackModel->entries)
            titles.push_back(e.title);
    }
    return titles;
}

void NormalizationView::setCurrentMeasurement(const juce::String& trackTitle,
                                              float measuredLUFSBefore,
                                              float finalLUFSAfter) {
    m_currentTrackName = trackTitle;
    m_lufsBefore = juce::String(measuredLUFSBefore, 1);
    m_lufsAfter  = juce::String(finalLUFSAfter,  1);
    m_wfGain = juce::Decibels::decibelsToGain(finalLUFSAfter - measuredLUFSBefore, -60.0f);
    juce::String path;
    if (m_trackModel) {
        for (auto& e : m_trackModel->entries) {
            if (e.title == trackTitle) {
                e.currentLUFS = m_lufsBefore + " LUFS";
                e.targetLUFS  = m_lufsAfter + " LUFS";
                e.status      = BM_TJ("norm.complete");
                path          = e.filePath;
                break;
            }
        }
        if (m_trackTable) m_trackTable->updateContent();
    }
    if (path.isNotEmpty())
        loadComparisonWaveform(path);
    repaint();
}

void NormalizationView::loadComparisonWaveform(const juce::String& filePath) {
    auto* pool = BeatMate::getBackgroundPool();
    if (!pool) return;
    juce::Component::SafePointer<NormalizationView> self(this);
    pool->addJob([self, filePath]() {
        Services::Library::PeakFileService svc;
        Services::Library::PeakConfig cfg;
        cfg.cacheDirectory = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
                .getChildFile("BeatMate").getChildFile("peaks_cache")
                .getFullPathName().toStdString();
        svc.initialize(cfg);
        auto data = svc.getPeaksByPath(filePath.toStdString());
        if (!data.has_value() || data->peaksPositive.empty()) return;

        const auto& src = data->peaksPositive;
        constexpr int kBars = 64;
        std::vector<float> bars((size_t) kBars, 0.0f);
        const size_t step = std::max<size_t>(1, src.size() / kBars);
        for (int b = 0; b < kBars; ++b) {
            float peak = 0.0f;
            const size_t start = (size_t) b * step;
            for (size_t i = start; i < std::min(src.size(), start + step); ++i)
                peak = std::max(peak, std::abs(src[i]));
            bars[(size_t) b] = peak;
        }
        juce::MessageManager::callAsync([self, bars = std::move(bars)]() mutable {
            if (!self) return;
            self->m_wfBars = std::move(bars);
            self->repaint();
        });
    });
}

juce::String NormalizationView::getTitleAtRow(int row) const {
    if (!m_trackModel) return {};
    if (row < 0 || row >= static_cast<int>(m_trackModel->entries.size())) return {};
    return m_trackModel->entries[static_cast<size_t>(row)].title;
}

void NormalizationView::updateProgress(int current,int total){
    m_completedCount = current;
    m_totalCount = total;
    m_progress=total>0?(double)current/total:0.0;
    if(current>=total && total > 0){
        m_normalizing = false;
        stopTimer();
        m_statusLabel->setText(BM_TJ("norm.complete"),juce::dontSendNotification);
        m_cancelBtn->setEnabled(false);
        m_normalizeBtn->setEnabled(true);
        m_listeners.call(&Listener::normalizationComplete);
    }
    repaint();
}

void NormalizationView::paint(juce::Graphics& g){
    ProDraw::viewBackground(g, getWidth(), getHeight());

    int w = getWidth();
    int margin = 24;
    float fullW = (float)w;

    if (m_targetLUFS) {
        auto sliderBounds = m_targetLUFS->getBounds();
        float szX = (float)sliderBounds.getX();
        float szY = (float)sliderBounds.getY() + 12.0f;
        float szW = (float)sliderBounds.getWidth();
        float szH = 6.0f;

        float segW = szW / 18.0f;

        g.setColour(Colors::error().withAlpha(0.15f));
        g.fillRoundedRectangle(szX, szY, segW * 4.0f, szH, 2.0f);
        g.setColour(Colors::warning().withAlpha(0.15f));
        g.fillRect(szX + segW * 4.0f, szY, segW * 4.0f, szH);
        g.setColour(Colors::success().withAlpha(0.25f));
        g.fillRect(szX + segW * 8.0f, szY, segW * 4.0f, szH);
        g.setColour(Colors::secondary().withAlpha(0.20f));
        g.fillRect(szX + segW * 12.0f, szY, segW * 4.0f, szH);
        g.setColour(Colors::error().withAlpha(0.15f));
        g.fillRoundedRectangle(szX + segW * 16.0f, szY, segW * 2.0f, szH, 2.0f);

        g.setFont(juce::Font(8.0f));
        g.setColour(Colors::textDim());
        for (int db = -24; db <= -6; db += 3) {
            float xPos = szX + szW * (float)(db + 24) / 18.0f;
            g.drawText(juce::String(db), (int)(xPos - 12), (int)(szY + szH + 1), 24, 10, juce::Justification::centred);
        }
    }

    struct PresetCard { juce::TextButton* btn; juce::Colour color; const char* icon; };
    PresetCard presetCards[] = {
        { m_presetSpotify.get(), juce::Colour(0xFF1DB954), "S" },
        { m_presetApple.get(),   juce::Colour(0xFFFC3C44), "A" },
        { m_presetYT.get(),      juce::Colour(0xFFFF0000), "Y" },
        { m_presetClub.get(),    Colors::secondary(),       "C" }
    };
    for (auto& pc : presetCards) {
        if (pc.btn && pc.btn->isVisible()) {
            auto bounds = pc.btn->getBounds();
            float cx = (float)bounds.getX() - 1.0f;
            float cy = (float)bounds.getY() - 1.0f;
            float cw = (float)bounds.getWidth() + 2.0f;
            float ch = (float)bounds.getHeight() + 2.0f;

            g.setColour(pc.color.withAlpha(0.15f));
            g.fillRoundedRectangle(cx - 1, cy - 1, cw + 2, ch + 2, 6.0f);

            g.setColour(pc.color);
            g.fillRoundedRectangle(cx + 3, cy + 3, 14.0f, 14.0f, 3.0f);
            g.setColour(Colors::bgDarkest());
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText(pc.icon, (int)(cx + 3), (int)(cy + 3), 14, 14, juce::Justification::centred);
        }
    }

    int filterBarY = 130;
    ProDraw::glassPanel(g, { (float)margin, (float)filterBarY, fullW - margin * 2.0f, 40.0f }, 8.0f);

    int headerY = 218;
    g.setColour(Colors::bgMedium()); g.fillRect(margin, headerY, w - margin*2, 24);
    g.setFont(juce::Font(11.0f, juce::Font::bold)); g.setColour(Colors::textMuted());
    int hx = margin + 8;
    g.drawText(BM_TJ("norm.col.sel"), hx, headerY, 22, 24, juce::Justification::centred); hx += 22;
    g.drawText(BM_TJ("norm.col.title"), hx, headerY, 200, 24, juce::Justification::centredLeft); hx += 200;
    g.drawText(BM_TJ("norm.col.artist"), hx, headerY, 150, 24, juce::Justification::centredLeft); hx += 150;
    g.drawText(BM_TJ("norm.col.lufsCurrent"), hx, headerY, 100, 24, juce::Justification::centred); hx += 100;
    g.drawText(BM_TJ("norm.col.status"), hx, headerY, 150, 24, juce::Justification::centredLeft);

    int progressBoxY = getHeight() - 230;
    int progressBoxH = 150;
    if (progressBoxY < 300) progressBoxY = 300;
    float boxX = (float)margin;
    float boxW = (float)(w - margin * 2);

    ProDraw::glassPanel(g, { boxX, (float)progressBoxY, boxW, (float)progressBoxH }, 10.0f);

    int px = margin + 16;

    if (m_normalizing || m_progress > 0.0) {
        int percent = (m_totalCount > 0) ? (m_completedCount * 100 / m_totalCount) : 0;
        bool isComplete = (m_completedCount >= m_totalCount && m_totalCount > 0);

        {
            float ringCx = boxX + boxW - 65.0f;
            float ringCy = (float)progressBoxY + 34.0f;
            float ringR = 20.0f;
            float thickness = 4.0f;

            g.setColour(Colors::bgLight());
            juce::Path bgRing;
            bgRing.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                                  0.0f, juce::MathConstants<float>::twoPi, true);
            g.strokePath(bgRing, juce::PathStrokeType(thickness));

            if (isComplete) {
                g.setColour(Colors::success());
                juce::Path fullRing;
                fullRing.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                                        0.0f, juce::MathConstants<float>::twoPi, true);
                g.strokePath(fullRing, juce::PathStrokeType(thickness));
                g.setFont(juce::Font(16.0f, juce::Font::bold));
                g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), (int)(ringCx - 10), (int)(ringCy - 9), 20, 18, juce::Justification::centred);
            } else {
                float progressAngle = juce::MathConstants<float>::twoPi * (float)m_progress;
                juce::ColourGradient arcGrad(Colors::success(), ringCx - ringR, ringCy,
                                              Colors::primary(), ringCx + ringR, ringCy, false);
                g.setGradientFill(arcGrad);
                juce::Path progressArc;
                progressArc.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                                           -juce::MathConstants<float>::halfPi,
                                           -juce::MathConstants<float>::halfPi + progressAngle, true);
                g.strokePath(progressArc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                if (m_normalizing) {
                    float dotAngle = m_spinnerAngle - juce::MathConstants<float>::halfPi;
                    float dotX = ringCx + ringR * std::cos(dotAngle);
                    float dotY = ringCy + ringR * std::sin(dotAngle);
                    float pulseAlpha = 0.4f + 0.6f * std::sin(m_pulsePhase);
                    g.setColour(Colors::success().withAlpha(pulseAlpha));
                    g.fillEllipse(dotX - 3.5f, dotY - 3.5f, 7.0f, 7.0f);
                }

                g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.setColour(Colors::textPrimary());
                g.drawText(juce::String(percent) + "%",
                           (int)(ringCx - ringR), (int)(ringCy - 7), (int)(ringR * 2), 14,
                           juce::Justification::centred);
            }
        }

        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.setColour(isComplete ? Colors::success() : Colors::primary());
        juce::String titleText = isComplete ? BM_TJ("norm.normComplete") : BM_TJ("norm.normInProgress");

        if (m_normalizing && !isComplete) {
            float pulseDotAlpha = 0.3f + 0.7f * (0.5f + 0.5f * std::sin(m_pulsePhase * 1.5f));
            g.setColour(Colors::success().withAlpha(pulseDotAlpha));
            g.fillEllipse((float)px, (float)(progressBoxY + 17), 8.0f, 8.0f);
            g.setColour(Colors::primary());
            g.drawText(titleText, px + 14, progressBoxY + 10, (int)boxW - 150, 24, juce::Justification::centredLeft);
        } else {
            g.drawText(titleText, px, progressBoxY + 10, (int)boxW - 150, 24, juce::Justification::centredLeft);
        }

        float barX = (float)px;
        float barY = (float)(progressBoxY + 42);
        float barW = boxW - 110.0f;
        float barH = 20.0f;

        g.setColour(Colors::bgLight());
        g.fillRoundedRectangle(barX, barY, barW, barH, 6.0f);

        if (m_progress > 0.0) {
            juce::ColourGradient grad(
                juce::Colour(0xFF10B981), barX, barY,
                juce::Colour(0xFF3B82F6), barX + barW, barY, false);
            g.setGradientFill(grad);
            float fillW = barW * (float)m_progress;
            if (fillW < 12.0f) fillW = 12.0f;
            g.fillRoundedRectangle(barX, barY, fillW, barH, 6.0f);

            juce::ColourGradient shine(
                juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.12f), barX, barY,
                juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.0f), barX, barY + barH, false);
            g.setGradientFill(shine);
            g.fillRoundedRectangle(barX, barY, fillW, barH * 0.5f, 6.0f);
        }

        int infoY = progressBoxY + 70;
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.setColour(Colors::textPrimary());
        if (m_currentTrackName.isNotEmpty()) {
            g.drawText(BM_TJ("norm.track") + m_currentTrackName, px, infoY, (int)boxW - 32, 18,
                       juce::Justification::centredLeft);
        }

        int statsY = infoY + 24;
        g.setFont(juce::Font(12.0f));
        g.setColour(Colors::textSecondary());
        g.drawText(BM_TJ("norm.processed") + juce::String(m_completedCount) + " / " + juce::String(m_totalCount),
                   px, statsY, 200, 16, juce::Justification::centredLeft);

        if (m_lufsBefore != "-") {
            int meterX = px + 220;
            int meterY = statsY - 2;
            float meterW = 220.0f;
            float meterH = 20.0f;

            g.setColour(Colors::bgLight());
            g.fillRoundedRectangle((float)meterX, (float)meterY, meterW, meterH, 4.0f);

            float zoneSegW = meterW / 18.0f;
            g.setColour(Colors::error().withAlpha(0.3f));
            g.fillRect((float)meterX, (float)meterY, zoneSegW * 4.0f, meterH);
            g.setColour(Colors::warning().withAlpha(0.25f));
            g.fillRect((float)meterX + zoneSegW * 4.0f, (float)meterY, zoneSegW * 4.0f, meterH);
            g.setColour(Colors::success().withAlpha(0.3f));
            g.fillRect((float)meterX + zoneSegW * 8.0f, (float)meterY, zoneSegW * 4.0f, meterH);
            g.setColour(Colors::warning().withAlpha(0.25f));
            g.fillRect((float)meterX + zoneSegW * 12.0f, (float)meterY, zoneSegW * 3.0f, meterH);
            g.setColour(Colors::error().withAlpha(0.3f));
            g.fillRect((float)meterX + zoneSegW * 15.0f, (float)meterY, zoneSegW * 3.0f, meterH);

            float beforeVal = m_lufsBefore.getFloatValue();
            float beforePos = juce::jlimit(0.0f, 1.0f, (beforeVal + 24.0f) / 18.0f);
            float beforeX = (float)meterX + meterW * beforePos;
            g.setColour(Colors::warning());
            g.fillRect(beforeX - 1.5f, (float)meterY, 3.0f, meterH);
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText("AV", (int)(beforeX - 8), meterY - 10, 16, 10, juce::Justification::centred);

            float afterVal = m_lufsAfter != "-" ? m_lufsAfter.getFloatValue() : (float)m_targetLUFS->getValue();
            float afterPos = juce::jlimit(0.0f, 1.0f, (afterVal + 24.0f) / 18.0f);
            float afterX = (float)meterX + meterW * afterPos;
            g.setColour(Colors::success());
            g.fillRect(afterX - 1.5f, (float)meterY, 3.0f, meterH);
            g.drawText("AP", (int)(afterX - 8), meterY + (int)meterH, 16, 10, juce::Justification::centred);

            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(10.0f));
            g.drawText(m_lufsBefore + " -> " + m_lufsAfter + " LUFS",
                       meterX + (int)meterW + 8, meterY, 150, (int)meterH, juce::Justification::centredLeft);

            g.setFont(juce::Font(7.0f));
            g.setColour(Colors::textDim());
            for (int db = -24; db <= -6; db += 6) {
                float xPos = (float)meterX + meterW * (float)(db + 24) / 18.0f;
                g.drawText(juce::String(db), (int)(xPos - 10), meterY + (int)meterH + 1, 20, 9, juce::Justification::centred);
            }
        } else {
            g.setColour(Colors::textMuted());
            g.drawText(BM_TJ("norm.waiting"), px + 220, statsY, 250, 16, juce::Justification::centredLeft);
        }

        {
            int wfY = statsY + 24;
            float wfX = (float)px;
            float wfW = boxW - 110.0f;
            float wfH = 24.0f;
            float halfW = wfW * 0.5f;

            g.setFont(juce::Font(8.0f));
            g.setColour(Colors::textDim());
            g.drawText(BM_TJ("norm.before"), (int)wfX, wfY - 10, 40, 10, juce::Justification::centredLeft);
            g.drawText(BM_TJ("norm.after"), (int)(wfX + halfW + 4), wfY - 10, 40, 10, juce::Justification::centredLeft);

            if (!m_wfBars.empty()) {
                const int n = (int) m_wfBars.size();
                const float step = (halfW - 4.0f) / (float) n;
                g.setColour(Colors::warning().withAlpha(0.55f));
                for (int i = 0; i < n; ++i) {
                    float barHt = wfH * juce::jlimit(0.04f, 1.0f, m_wfBars[(size_t) i]);
                    g.fillRect(wfX + (float)i * step, (float)wfY + (wfH - barHt) * 0.5f,
                               std::max(1.0f, step - 1.0f), barHt);
                }
                g.setColour(Colors::textDim());
                g.fillRect(wfX + halfW - 1.0f, (float)wfY, 2.0f, wfH);
                g.setColour(Colors::success().withAlpha(0.6f));
                for (int i = 0; i < n; ++i) {
                    float barHt = wfH * juce::jlimit(0.04f, 1.0f, m_wfBars[(size_t) i] * m_wfGain);
                    g.fillRect(wfX + halfW + 2.0f + (float)i * step, (float)wfY + (wfH - barHt) * 0.5f,
                               std::max(1.0f, step - 1.0f), barHt);
                }
            } else {
                g.setColour(Colors::warning().withAlpha(0.25f));
                g.fillRect(wfX, (float)wfY + wfH * 0.25f, halfW - 4.0f, wfH * 0.5f);
                g.setColour(Colors::textDim());
                g.fillRect(wfX + halfW - 1.0f, (float)wfY, 2.0f, wfH);
                g.setColour(Colors::success().withAlpha(0.3f));
                g.fillRect(wfX + halfW + 2.0f, (float)wfY + wfH * 0.3f, halfW - 4.0f, wfH * 0.4f);
            }
        }

        if (m_normalizing && m_completedCount > 0 && m_completedCount < m_totalCount) {
            double elapsedMs = juce::Time::getMillisecondCounterHiRes() - m_normStartTime;
            double msPerTrack = elapsedMs / (double)m_completedCount;
            int remaining = m_totalCount - m_completedCount;
            double remainingMs = msPerTrack * remaining;
            int remainingSec = (int)(remainingMs / 1000.0);
            juce::String timeStr;
            if (remainingSec < 60) timeStr = "~" + juce::String(remainingSec) + " sec";
            else timeStr = "~" + juce::String(remainingSec / 60) + " min";
            g.setColour(Colors::textMuted());
            g.setFont(juce::Font(11.0f));
            g.drawText(BM_TJ("norm.timeRemaining") + timeStr,
                       (int)(boxX + boxW) - 200, progressBoxY + progressBoxH - 22, 180, 16, juce::Justification::centredRight);
        }
    } else {
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.setColour(Colors::textMuted());
        g.drawText(BM_TJ("norm.readyToNorm"), px, progressBoxY + 14, (int)boxW - 32, 24,
                   juce::Justification::centredLeft);

        g.setFont(juce::Font(12.0f));
        g.setColour(Colors::textMuted().withAlpha(0.6f));
        g.drawText(BM_TJ("norm.addThenNormalize"),
                   px, progressBoxY + 40, (int)boxW - 32, 20, juce::Justification::centredLeft);

        float barX = (float)px;
        float barY = (float)(progressBoxY + 68);
        float barW = boxW - 110.0f;
        float barH = 20.0f;
        g.setColour(Colors::bgLight());
        g.fillRoundedRectangle(barX, barY, barW, barH, 6.0f);

        float ringCx = boxX + boxW - 65.0f;
        float ringCy = (float)progressBoxY + 34.0f;
        float ringR = 20.0f;
        g.setColour(Colors::bgLight());
        juce::Path idleRing;
        idleRing.addCentredArc(ringCx, ringCy, ringR, ringR, 0.0f,
                                0.0f, juce::MathConstants<float>::twoPi, true);
        g.strokePath(idleRing, juce::PathStrokeType(4.0f));
        g.setFont(juce::Font(10.0f));
        g.setColour(Colors::textDim());
        g.drawText("0%", (int)(ringCx - ringR), (int)(ringCy - 7), (int)(ringR * 2), 14,
                   juce::Justification::centred);

        float meterX = barX;
        float meterY = barY + barH + 12.0f;
        float meterW = barW;
        float meterH = 14.0f;
        g.setColour(Colors::bgLight());
        g.fillRoundedRectangle(meterX, meterY, meterW, meterH, 3.0f);

        float zoneSegW = meterW / 18.0f;
        g.setColour(Colors::error().withAlpha(0.12f));
        g.fillRect(meterX, meterY, zoneSegW * 4.0f, meterH);
        g.setColour(Colors::warning().withAlpha(0.1f));
        g.fillRect(meterX + zoneSegW * 4.0f, meterY, zoneSegW * 4.0f, meterH);
        g.setColour(Colors::success().withAlpha(0.15f));
        g.fillRect(meterX + zoneSegW * 8.0f, meterY, zoneSegW * 4.0f, meterH);
        g.setColour(Colors::warning().withAlpha(0.1f));
        g.fillRect(meterX + zoneSegW * 12.0f, meterY, zoneSegW * 3.0f, meterH);
        g.setColour(Colors::error().withAlpha(0.12f));
        g.fillRect(meterX + zoneSegW * 15.0f, meterY, zoneSegW * 3.0f, meterH);

        if (m_targetLUFS) {
            float tgt = (float)m_targetLUFS->getValue();
            float tgtPos = juce::jlimit(0.0f, 1.0f, (tgt + 24.0f) / 18.0f);
            float tgtX = meterX + meterW * tgtPos;
            g.setColour(Colors::success());
            g.fillRect(tgtX - 1.0f, meterY, 2.0f, meterH);
            g.setFont(juce::Font(8.0f));
            g.drawText(BM_TJ("norm.target"), (int)(tgtX - 14), (int)(meterY + meterH + 1), 28, 9, juce::Justification::centred);
        }

        g.setFont(juce::Font(7.0f));
        g.setColour(Colors::textDim());
        for (int db = -24; db <= -6; db += 6) {
            float xPos = meterX + meterW * (float)(db + 24) / 18.0f;
            g.drawText(juce::String(db), (int)(xPos - 10), (int)(meterY - 10), 20, 9, juce::Justification::centred);
        }
    }

    ProDraw::vignette(g, fullW, (float)getHeight());
}

void NormalizationView::resized(){
    int margin = 24;
    int w = getWidth();
    int y = 20;

    m_titleLabel->setBounds(margin, y, 400, 32); y += 36;
    m_descLabel->setBounds(margin, y, 500, 20); y += 28;

    m_lufsValueLabel->setBounds(margin, y, 120, 30);
    m_targetLUFS->setBounds(150, y, 250, 30);

    int presetX = 420;
    int presetW = 145;
    m_presetSpotify->setBounds(presetX, y, presetW, 28);
    m_presetApple->setBounds(presetX + presetW + 6, y, presetW, 28);
    m_presetYT->setBounds(presetX + (presetW + 6) * 2, y, presetW, 28);
    m_presetClub->setBounds(presetX + (presetW + 6) * 3, y, presetW, 28);
    y += 38;

    int filterBarY = y;
    int fx = margin + 12;

    m_searchLabel->setBounds(fx, filterBarY + 6, 70, 28);
    fx += 70;
    m_searchEditor->setBounds(fx, filterBarY + 6, 160, 28);
    fx += 168;

    m_genreLabel->setBounds(fx, filterBarY + 6, 42, 28);
    fx += 42;
    m_genreFilter->setBounds(fx, filterBarY + 6, 130, 28);
    fx += 140;

    m_addAllBtn->setBounds(fx, filterBarY + 6, 110, 28);
    fx += 118;
    m_removeAllBtn->setBounds(fx, filterBarY + 6, 100, 28);
    fx += 108;
    m_selectAllBtn->setBounds(fx, filterBarY + 6, 140, 28);
    fx += 148;
    m_addTracksBtn->setBounds(fx, filterBarY + 6, 150, 28);
    fx += 158;
    m_removeBtn->setBounds(fx, filterBarY + 6, 80, 28);
    fx += 88;
    if (m_editFileBtn) m_editFileBtn->setBounds(fx, filterBarY + 6, 130, 28);
    y = filterBarY + 48;

    if (m_audioEditor && m_editorVisible)
        m_audioEditor->setBounds(getLocalBounds());

    int headerY = y;
    y += 24;

    int progressBoxY = getHeight() - 230;
    int progressBoxH = 150;
    if (progressBoxY < 300) progressBoxY = 300;

    int tableBottom = progressBoxY - 8;
    m_trackTable->setBounds(margin, y, w - margin * 2, tableBottom - y);

    int bottomY = getHeight() - 72;
    m_statusLabel->setBounds(margin, bottomY + 10, 200, 20);
    m_previewBeforeBtn->setBounds(margin + 210, bottomY, 120, 36);
    m_previewAfterBtn->setBounds(margin + 340, bottomY, 120, 36);
    m_cancelBtn->setBounds(w - 240, bottomY, 100, 36);
    m_normalizeBtn->setBounds(w - 130, bottomY, 110, 36);
}

void NormalizationView::openAudioEditor()
{
    if (! m_trackModel || m_trackModel->entries.empty())
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("\xc3\x89""diteur audio"),
            juce::String::fromUTF8("Ajoutez d'abord des morceaux \xc3\xa0 la liste."),
            Widgets::ToastNotifier::Kind::Info, 4000);
        return;
    }

    int row = m_trackTable ? m_trackTable->getSelectedRow() : -1;
    if (row < 0)
        for (int i = 0; i < (int) m_trackModel->entries.size(); ++i)
            if (m_trackModel->entries[(size_t) i].selected) { row = i; break; }
    if (row < 0) row = 0;
    if (row >= (int) m_trackModel->entries.size()) return;

    const juce::String path = m_trackModel->entries[(size_t) row].filePath;
    if (path.isEmpty() || ! juce::File(path).existsAsFile())
    {
        Widgets::ToastNotifier::getInstance().show(
            juce::String::fromUTF8("\xc3\x89""diteur audio"),
            juce::String::fromUTF8("Fichier introuvable : ")
                + juce::File(path).getFileName(),
            Widgets::ToastNotifier::Kind::Error, 5000);
        return;
    }

    if (! m_audioEditor)
    {
        m_audioEditor = std::make_unique<AudioEditorPanel>();
        juce::Component::SafePointer<NormalizationView> safe(this);
        m_audioEditor->onClose = [safe] {
            if (safe == nullptr) return;
            safe->m_editorVisible = false;
            if (safe->m_audioEditor) safe->m_audioEditor->setVisible(false);
            safe->resized();
            safe->repaint();
        };
        m_audioEditor->onFileSaved = [safe](const juce::String&) {
            if (safe != nullptr) safe->loadTracksFromDatabase();
        };
        addAndMakeVisible(*m_audioEditor);
    }

    m_editorVisible = true;
    m_audioEditor->setVisible(true);
    m_audioEditor->toFront(false);
    m_audioEditor->loadFile(path);
    resized();
}

} // namespace BeatMate::UI
