#include "SettingsView.h"
#include "../styles/ColorPalette.h"
#include "../styles/ThemeEngine.h"
#include "../../app/ServiceLocator.h"
#include "../../services/security/LicenseService.h"
#include "../../services/network/HttpClient.h"
#include "../../services/wordpress/WordPressLicenseClient.h"
#include "../../services/config/SettingsManager.h"
#include "../dialogs/LicenseDialog.h"
#include "../../core/audio/AudioPlayer.h"
#include "../../core/audio/AudioEngine.h"
#include "../../core/audio/AudioPreview.h"
#include "../../core/audio/AudioTrack.h"
#include "../../core/audio/AudioFileWriter.h"
#include "../widgets/ToastNotifier.h"
#include "../../core/analysis/AudioAnalysisPipeline.h"
#include "../../services/library/TrackScanner.h"
#include "../../services/library/AutoImport.h"
#include "../../services/config/BackupService.h"
#include "../../services/config/LocalizationService.h"
#include "../../services/config/I18n.h"
#include "../../services/djsoftware/rekordbox/RekordboxService.h"
#include "../../services/djsoftware/rekordbox/RekordboxCipher.h"
#include <sqlite3.h>
#include <thread>
#include "../../services/djsoftware/CollectionSyncService.h"
#include "../../services/djsoftware/DJSoftwareDetector.h"
#include "../../services/library/TrackDataProvider.h"
#include "../../services/library/DuplicateDetector.h"
#include "../../services/update/UpdateService.h"

namespace BeatMate::UI {

#ifndef BEATMATE_WP_BASE_URL
 #define BEATMATE_WP_BASE_URL "https://beatmate.fr"
#endif
#ifndef BEATMATE_WP_API_KEY
 #define BEATMATE_WP_API_KEY ""
#endif

namespace {
void resolveWpCredentials(std::string& url, std::string& apiKey)
{
    url    = BEATMATE_WP_BASE_URL;
    apiKey = BEATMATE_WP_API_KEY;
    const juce::File f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("BeatMate").getChildFile("wp.json");
    if (f.existsAsFile())
    {
        juce::var parsed;
        if (juce::JSON::parse(f.loadFileAsString(), parsed).wasOk() && parsed.isObject())
        {
            auto u = parsed.getProperty("base_url", juce::var()).toString().trim();
            auto k = parsed.getProperty("api_key",  juce::var()).toString().trim();
            if (u.isNotEmpty()) url    = u.toStdString();
            if (k.isNotEmpty()) apiKey = k.toStdString();
        }
    }
    while (!url.empty() && url.back() == '/') url.pop_back();
}
} // namespace


void SettingsView::WatchFoldersListModel::paintListBoxItem(
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

void SettingsView::ShortcutsTableModel::paintRowBackground(
    juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(Colors::primary().withAlpha(0.2f));
    else if (row % 2 == 0)
        g.fillAll(Colors::bgDark());
    else
        g.fillAll(Colors::bgSurface());
}

void SettingsView::ShortcutsTableModel::paintCell(
    juce::Graphics& g, int row, int col, int w, int h, bool /*selected*/)
{
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(13.0f));
    if (row < (int)shortcuts.size())
    {
        juce::String text = (col == 0) ? shortcuts[(size_t)row].action : shortcuts[(size_t)row].key;
        g.drawText("  " + text, 0, 0, w, h, juce::Justification::centredLeft);
    }
    g.setColour(Colors::border());
    g.drawLine((float)w, 0.0f, (float)w, (float)h, 0.5f);
}

void SettingsView::BackupsListModel::paintListBoxItem(
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
    if (row < backupNames.size())
        g.drawText("  " + backupNames[row], 0, 0, w, h, juce::Justification::centredLeft);
}


juce::Label* SettingsView::makeLabel(juce::Component* parent, const juce::String& text,
                                      int x, int y, int w, int h, bool isHeader)
{
    auto* lbl = new juce::Label("", text);
    lbl->setFont(juce::Font(isHeader ? 16.0f : 13.0f, isHeader ? juce::Font::bold : juce::Font::plain));
    lbl->setColour(juce::Label::textColourId, isHeader ? Colors::textPrimary() : Colors::textSecondary());
    lbl->setBounds(x, y, w, h);
    parent->addAndMakeVisible(lbl);
    return lbl;
}

juce::ComboBox* SettingsView::makeCombo(juce::Component* parent, int x, int y, int w, int h)
{
    auto* cb = new juce::ComboBox();
    cb->setColour(juce::ComboBox::backgroundColourId, Colors::bgLighter());
    cb->setColour(juce::ComboBox::textColourId, Colors::textPrimary());
    cb->setColour(juce::ComboBox::outlineColourId, Colors::border());
    cb->setBounds(x, y, w, h);
    parent->addAndMakeVisible(cb);
    return cb;
}

juce::ToggleButton* SettingsView::makeToggle(juce::Component* parent, const juce::String& text,
                                               int x, int y, int w, int h, bool defaultOn)
{
    auto* tb = new juce::ToggleButton(text);
    tb->setColour(juce::ToggleButton::textColourId, Colors::textSecondary());
    tb->setColour(juce::ToggleButton::tickColourId, Colors::primary());
    tb->setToggleState(defaultOn, juce::dontSendNotification);
    tb->setBounds(x, y, w, h);
    parent->addAndMakeVisible(tb);
    return tb;
}

juce::TextButton* SettingsView::makeButton(juce::Component* parent, const juce::String& text,
                                             int x, int y, int w, int h, juce::Colour bg)
{
    auto* btn = new juce::TextButton(text);
    btn->setColour(juce::TextButton::buttonColourId, bg);
    btn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    btn->setBounds(x, y, w, h);
    parent->addAndMakeVisible(btn);
    return btn;
}


juce::File SettingsView::getSettingsFile() const
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate");
    appData.createDirectory();
    return appData.getChildFile("appsettings.json");
}


void SettingsView::saveSettingsToJSON()
{
    auto settingsFile = getSettingsFile();

    juce::DynamicObject::Ptr root = new juce::DynamicObject();

    {
        juce::DynamicObject::Ptr general = new juce::DynamicObject();
        general->setProperty("language",          m_languageCombo ? m_languageCombo->getSelectedId() : 1);
        general->setProperty("theme",             m_themeCombo ? m_themeCombo->getSelectedId() : 1);
        general->setProperty("startup",           m_startupCombo ? m_startupCombo->getSelectedId() : 1);
        general->setProperty("autoSave",          m_autoSaveCheck ? m_autoSaveCheck->getToggleState() : true);
        general->setProperty("autoSaveInterval",  m_autoSaveIntervalCombo ? m_autoSaveIntervalCombo->getSelectedId() : 2);
        general->setProperty("checkUpdates",      m_checkUpdatesCheck ? m_checkUpdatesCheck->getToggleState() : true);
        general->setProperty("dataFolder",        m_dataFolderLabel ? m_dataFolderLabel->getText() : juce::String());
        root->setProperty("general", juce::var(general.get()));
    }

    {
        juce::DynamicObject::Ptr audio = new juce::DynamicObject();
        audio->setProperty("deviceId",     m_audioDeviceCombo ? m_audioDeviceCombo->getSelectedId() : 1);
        audio->setProperty("deviceName",   m_audioDeviceCombo ? m_audioDeviceCombo->getText() : juce::String());
        audio->setProperty("inputDeviceId", m_audioInputCombo ? m_audioInputCombo->getSelectedId() : 1);
        audio->setProperty("sampleRate",   m_sampleRateCombo ? m_sampleRateCombo->getSelectedId() : 1);
        audio->setProperty("bufferSize",   m_bufferSizeCombo ? m_bufferSizeCombo->getSelectedId() : 3);
        root->setProperty("audio", juce::var(audio.get()));
    }

    {
        juce::DynamicObject::Ptr library = new juce::DynamicObject();
        juce::Array<juce::var> foldersArr;
        for (const auto& f : m_watchFolders)
            foldersArr.add(juce::var(f));
        library->setProperty("watchFolders",       juce::var(foldersArr));
        library->setProperty("autoImport",         m_autoImportCheck ? m_autoImportCheck->getToggleState() : true);
        library->setProperty("analyzeOnImport",    m_analyzeOnImportCheck ? m_analyzeOnImportCheck->getToggleState() : true);
        library->setProperty("detectDuplicates",   m_detectDuplicatesCheck ? m_detectDuplicatesCheck->getToggleState() : true);
        root->setProperty("library", juce::var(library.get()));
    }

    {
        juce::DynamicObject::Ptr analysis = new juce::DynamicObject();
        analysis->setProperty("quality",           m_analysisQualityCombo ? m_analysisQualityCombo->getSelectedId() : 2);
        analysis->setProperty("bpmMin",            m_bpmMinSlider ? m_bpmMinSlider->getValue() : 70.0);
        analysis->setProperty("bpmMax",            m_bpmMaxSlider ? m_bpmMaxSlider->getValue() : 180.0);
        analysis->setProperty("bpmMode",           m_bpmModeCombo ? m_bpmModeCombo->getSelectedId() : 1);
        analysis->setProperty("keyMethod",         m_keyMethodCombo ? m_keyMethodCombo->getSelectedId() : 1);
        analysis->setProperty("autoAnalyze",       m_autoAnalyzeCheck ? m_autoAnalyzeCheck->getToggleState() : true);
        analysis->setProperty("threads",           m_analysisThreadsCombo ? m_analysisThreadsCombo->getSelectedId() : 2);
        analysis->setProperty("enableStems",       m_enableStemsCheck ? m_enableStemsCheck->getToggleState() : false);
        analysis->setProperty("generateWaveform",  m_generateWaveformCheck ? m_generateWaveformCheck->getToggleState() : true);
        root->setProperty("analysis", juce::var(analysis.get()));
    }

    {
        juce::DynamicObject::Ptr dj = new juce::DynamicObject();
        dj->setProperty("autoSync",       m_autoSyncDJCheck ? m_autoSyncDJCheck->getToggleState() : false);
        dj->setProperty("syncInterval",   m_syncIntervalCombo ? m_syncIntervalCombo->getSelectedId() : 3);
        root->setProperty("djSoftware", juce::var(dj.get()));
    }

    {
        juce::DynamicObject::Ptr licence = new juce::DynamicObject();
        licence->setProperty("key",    m_licenceKeyEdit ? m_licenceKeyEdit->getText() : juce::String());
        licence->setProperty("status", m_licenceStatusLbl ? m_licenceStatusLbl->getText() : juce::String("Essai"));
        licence->setProperty("type",   m_licenceTypeLbl ? m_licenceTypeLbl->getText() : juce::String());
        root->setProperty("licence", juce::var(licence.get()));
    }

    {
        juce::DynamicObject::Ptr backup = new juce::DynamicObject();
        backup->setProperty("autoBackup",      m_autoBackupCheck ? m_autoBackupCheck->getToggleState() : true);
        backup->setProperty("backupInterval",  m_backupIntervalCombo ? m_backupIntervalCombo->getSelectedId() : 2);
        backup->setProperty("maxBackups",      m_maxBackupsSpin ? (int)m_maxBackupsSpin->getValue() : 10);
        root->setProperty("backup", juce::var(backup.get()));
    }

    {
        juce::DynamicObject::Ptr shortcuts = new juce::DynamicObject();
        for (const auto& s : m_shortcutsModel.shortcuts)
            shortcuts->setProperty(s.action, s.key);
        root->setProperty("shortcuts", juce::var(shortcuts.get()));
    }

    {
        juce::DynamicObject::Ptr appearance = new juce::DynamicObject();
        appearance->setProperty("accentColor",    m_accentColorCombo ? m_accentColorCombo->getSelectedId() : 1);
        appearance->setProperty("fontSize",       m_fontSizeSlider ? m_fontSizeSlider->getValue() : 13.0);
        appearance->setProperty("density",        m_densityCombo ? m_densityCombo->getSelectedId() : 2);
        appearance->setProperty("waveformStyle",  m_waveformStyleCombo ? m_waveformStyleCombo->getSelectedId() : 1);
        root->setProperty("appearance", juce::var(appearance.get()));
    }

    juce::String jsonText = juce::JSON::toString(juce::var(root.get()), true);
    // Ecriture atomique : un crash ne corrompt pas le fichier
    juce::TemporaryFile tmp(settingsFile);
    if (tmp.getFile().replaceWithText(jsonText))
    {
        if (!tmp.overwriteTargetFileWithTemporary())
            spdlog::warn("[Settings] atomic overwrite failed for {}", settingsFile.getFullPathName().toStdString());
    }
    else
    {
        spdlog::warn("[Settings] failed writing temp file for {}", settingsFile.getFullPathName().toStdString());
    }

    // Miroir de chaque cle dans le SettingsManager central
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* settingsMgr = g_serviceLocator->tryGet<Services::Config::SettingsManager>()) {
            settingsMgr->set("general.language",          m_languageCombo ? m_languageCombo->getSelectedId() : 1);
            settingsMgr->set("general.theme",             m_themeCombo ? m_themeCombo->getSelectedId() : 1);
            settingsMgr->set("general.startup",           m_startupCombo ? m_startupCombo->getSelectedId() : 1);
            settingsMgr->set("general.autoSave",          m_autoSaveCheck ? m_autoSaveCheck->getToggleState() : true);
            settingsMgr->set("general.autoSaveInterval", m_autoSaveIntervalCombo ? m_autoSaveIntervalCombo->getSelectedId() : 2);
            settingsMgr->set("general.checkUpdates",      m_checkUpdatesCheck ? m_checkUpdatesCheck->getToggleState() : true);
            settingsMgr->set<std::string>("general.dataFolder", (m_dataFolderLabel ? m_dataFolderLabel->getText() : juce::String()).toStdString());
            settingsMgr->set("audio.deviceId",      m_audioDeviceCombo ? m_audioDeviceCombo->getSelectedId() : 1);
            settingsMgr->set<std::string>("audio.deviceName", (m_audioDeviceCombo ? m_audioDeviceCombo->getText() : juce::String()).toStdString());
            settingsMgr->set("audio.inputDeviceId", m_audioInputCombo ? m_audioInputCombo->getSelectedId() : 1);
            settingsMgr->set("audio.sampleRate",    m_sampleRateCombo ? m_sampleRateCombo->getSelectedId() : 1);
            settingsMgr->set("audio.bufferSize",    m_bufferSizeCombo ? m_bufferSizeCombo->getSelectedId() : 3);
            settingsMgr->set("library.autoImport",       m_autoImportCheck ? m_autoImportCheck->getToggleState() : true);
            settingsMgr->set("library.analyzeOnImport",  m_analyzeOnImportCheck ? m_analyzeOnImportCheck->getToggleState() : true);
            settingsMgr->set("library.detectDuplicates", m_detectDuplicatesCheck ? m_detectDuplicatesCheck->getToggleState() : true);
            settingsMgr->set("analysis.quality",          m_analysisQualityCombo ? m_analysisQualityCombo->getSelectedId() : 2);
            settingsMgr->set("analysis.bpmMin",           m_bpmMinSlider ? m_bpmMinSlider->getValue() : 70.0);
            settingsMgr->set("analysis.bpmMax",           m_bpmMaxSlider ? m_bpmMaxSlider->getValue() : 180.0);
            settingsMgr->set("analysis.bpmMode",          m_bpmModeCombo ? m_bpmModeCombo->getSelectedId() : 1);
            settingsMgr->set("analysis.keyMethod",        m_keyMethodCombo ? m_keyMethodCombo->getSelectedId() : 1);
            settingsMgr->set("analysis.autoAnalyze",      m_autoAnalyzeCheck ? m_autoAnalyzeCheck->getToggleState() : true);
            settingsMgr->set("analysis.threads",          m_analysisThreadsCombo ? m_analysisThreadsCombo->getSelectedId() : 2);
            settingsMgr->set("analysis.enableStems",      m_enableStemsCheck ? m_enableStemsCheck->getToggleState() : false);
            settingsMgr->set("analysis.generateWaveform", m_generateWaveformCheck ? m_generateWaveformCheck->getToggleState() : true);
            settingsMgr->set("djSoftware.autoSync",     m_autoSyncDJCheck ? m_autoSyncDJCheck->getToggleState() : false);
            settingsMgr->set("djSoftware.syncInterval", m_syncIntervalCombo ? m_syncIntervalCombo->getSelectedId() : 3);
            settingsMgr->set("backup.autoBackup",     m_autoBackupCheck ? m_autoBackupCheck->getToggleState() : true);
            settingsMgr->set("backup.backupInterval", m_backupIntervalCombo ? m_backupIntervalCombo->getSelectedId() : 2);
            settingsMgr->set("backup.maxBackups",     m_maxBackupsSpin ? (int)m_maxBackupsSpin->getValue() : 10);
            settingsMgr->set("appearance.accentColor",   m_accentColorCombo ? m_accentColorCombo->getSelectedId() : 1);
            settingsMgr->set("appearance.fontSize",      m_fontSizeSlider ? m_fontSizeSlider->getValue() : 13.0);
            settingsMgr->set("appearance.density",       m_densityCombo ? m_densityCombo->getSelectedId() : 2);
            settingsMgr->set("appearance.waveformStyle", m_waveformStyleCombo ? m_waveformStyleCombo->getSelectedId() : 1);
            settingsMgr->saveDB();
        }
    }
}


void SettingsView::loadSettingsFromJSON()
{
    auto settingsFile = getSettingsFile();
    if (!settingsFile.existsAsFile())
        return;

    auto jsonText = settingsFile.loadFileAsString();
    auto parsed = juce::JSON::parse(jsonText);
    if (!parsed.isObject())
        return;

    auto* root = parsed.getDynamicObject();
    if (!root) return;

    if (auto* general = root->getProperty("general").getDynamicObject())
    {
        if (m_languageCombo)        m_languageCombo->setSelectedId((int)general->getProperty("language"), juce::dontSendNotification);
        if (m_themeCombo)           m_themeCombo->setSelectedId((int)general->getProperty("theme"), juce::dontSendNotification);
        if (m_startupCombo)         m_startupCombo->setSelectedId((int)general->getProperty("startup"), juce::dontSendNotification);
        if (m_autoSaveCheck)        m_autoSaveCheck->setToggleState((bool)general->getProperty("autoSave"), juce::dontSendNotification);
        if (m_autoSaveIntervalCombo) m_autoSaveIntervalCombo->setSelectedId((int)general->getProperty("autoSaveInterval"), juce::dontSendNotification);
        if (m_checkUpdatesCheck)    m_checkUpdatesCheck->setToggleState((bool)general->getProperty("checkUpdates"), juce::dontSendNotification);
        if (m_dataFolderLabel)
        {
            auto folder = general->getProperty("dataFolder").toString();
            if (folder.isNotEmpty())
                m_dataFolderLabel->setText(folder, juce::dontSendNotification);
        }
    }

    if (auto* audio = root->getProperty("audio").getDynamicObject())
    {
        if (m_audioDeviceCombo)  m_audioDeviceCombo->setSelectedId((int)audio->getProperty("deviceId"), juce::dontSendNotification);
        if (m_audioInputCombo)   m_audioInputCombo->setSelectedId((int)audio->getProperty("inputDeviceId"), juce::dontSendNotification);
        if (m_sampleRateCombo)   m_sampleRateCombo->setSelectedId((int)audio->getProperty("sampleRate"), juce::dontSendNotification);
        if (m_bufferSizeCombo)   m_bufferSizeCombo->setSelectedId((int)audio->getProperty("bufferSize"), juce::dontSendNotification);
    }

    if (auto* library = root->getProperty("library").getDynamicObject())
    {
        m_watchFolders.clear();
        auto foldersVar = library->getProperty("watchFolders");
        if (auto* foldersArr = foldersVar.getArray())
        {
            for (const auto& f : *foldersArr)
                m_watchFolders.add(f.toString());
        }
        if (m_watchFoldersList) m_watchFoldersList->updateContent();
        if (m_autoImportCheck)      m_autoImportCheck->setToggleState((bool)library->getProperty("autoImport"), juce::dontSendNotification);
        if (m_analyzeOnImportCheck)  m_analyzeOnImportCheck->setToggleState((bool)library->getProperty("analyzeOnImport"), juce::dontSendNotification);
        if (m_detectDuplicatesCheck) m_detectDuplicatesCheck->setToggleState((bool)library->getProperty("detectDuplicates"), juce::dontSendNotification);
    }

    if (auto* analysis = root->getProperty("analysis").getDynamicObject())
    {
        if (m_analysisQualityCombo) m_analysisQualityCombo->setSelectedId((int)analysis->getProperty("quality"), juce::dontSendNotification);
        if (m_bpmMinSlider)         m_bpmMinSlider->setValue((double)analysis->getProperty("bpmMin"), juce::dontSendNotification);
        if (m_bpmMaxSlider)         m_bpmMaxSlider->setValue((double)analysis->getProperty("bpmMax"), juce::dontSendNotification);
        if (m_bpmModeCombo)         m_bpmModeCombo->setSelectedId((int)analysis->getProperty("bpmMode"), juce::dontSendNotification);
        if (m_keyMethodCombo)       m_keyMethodCombo->setSelectedId((int)analysis->getProperty("keyMethod"), juce::dontSendNotification);
        if (m_autoAnalyzeCheck)     m_autoAnalyzeCheck->setToggleState((bool)analysis->getProperty("autoAnalyze"), juce::dontSendNotification);
        if (m_analysisThreadsCombo) m_analysisThreadsCombo->setSelectedId((int)analysis->getProperty("threads"), juce::dontSendNotification);
        if (m_enableStemsCheck)     m_enableStemsCheck->setToggleState((bool)analysis->getProperty("enableStems"), juce::dontSendNotification);
        if (m_generateWaveformCheck) m_generateWaveformCheck->setToggleState((bool)analysis->getProperty("generateWaveform"), juce::dontSendNotification);
    }

    if (auto* dj = root->getProperty("djSoftware").getDynamicObject())
    {
        if (m_autoSyncDJCheck)   m_autoSyncDJCheck->setToggleState((bool)dj->getProperty("autoSync"), juce::dontSendNotification);
        if (m_syncIntervalCombo) m_syncIntervalCombo->setSelectedId((int)dj->getProperty("syncInterval"), juce::dontSendNotification);
    }

    if (auto* licence = root->getProperty("licence").getDynamicObject())
    {
        auto key = licence->getProperty("key").toString();
        auto status = licence->getProperty("status").toString();
        auto type = licence->getProperty("type").toString();

        if (m_licenceKeyEdit && key.isNotEmpty())
        {
            m_licenceKeyEdit->setText(key, false);
            if (status == "Active" || status == "Activee")
            {
                m_licenceKeyEdit->setReadOnly(true);
                if (m_licenceStatusLbl)
                {
                    m_licenceStatusLbl->setText(BM_TJ("settings.label.statusActive"), juce::dontSendNotification);
                    m_licenceStatusLbl->setColour(juce::Label::textColourId, Colors::success());
                }
                if (m_licenceTypeLbl && type.isNotEmpty())
                    m_licenceTypeLbl->setText(type, juce::dontSendNotification);
                if (m_activateBtn)
                    m_activateBtn->setButtonText(BM_TJ("settings.btn.changeLicence"));
            }
        }
    }

    if (auto* backup = root->getProperty("backup").getDynamicObject())
    {
        if (m_autoBackupCheck)      m_autoBackupCheck->setToggleState((bool)backup->getProperty("autoBackup"), juce::dontSendNotification);
        if (m_backupIntervalCombo)  m_backupIntervalCombo->setSelectedId((int)backup->getProperty("backupInterval"), juce::dontSendNotification);
        if (m_maxBackupsSpin)       m_maxBackupsSpin->setValue((double)(int)backup->getProperty("maxBackups"), juce::dontSendNotification);
    }

    if (auto* appearance = root->getProperty("appearance").getDynamicObject())
    {
        if (m_accentColorCombo)   m_accentColorCombo->setSelectedId((int)appearance->getProperty("accentColor"), juce::dontSendNotification);
        if (m_fontSizeSlider)     m_fontSizeSlider->setValue((double)appearance->getProperty("fontSize"), juce::dontSendNotification);
        if (m_densityCombo)       m_densityCombo->setSelectedId((int)appearance->getProperty("density"), juce::dontSendNotification);
        if (m_waveformStyleCombo) m_waveformStyleCombo->setSelectedId((int)appearance->getProperty("waveformStyle"), juce::dontSendNotification);
    }

    if (m_bufferSizeCombo && m_sampleRateCombo && m_latencyLabel) {
        static const int    bufSizes[] = { 64, 128, 256, 512, 1024, 2048 };
        static const double srRates[]  = { 44100.0, 48000.0, 88200.0, 96000.0 };
        const int bufIdx = m_bufferSizeCombo->getSelectedId() - 1;
        const int srIdx  = m_sampleRateCombo->getSelectedId() - 1;
        if (bufIdx >= 0 && bufIdx < 6 && srIdx >= 0 && srIdx < 4) {
            const double latencyMs = (double)bufSizes[bufIdx] / srRates[srIdx] * 1000.0;
            m_latencyLabel->setText(BM_TJ("settings.label.latencyPrefix")
                                    + juce::String(latencyMs, 1) + " ms",
                                    juce::dontSendNotification);
        }
    }
}


SettingsView::~SettingsView() {
    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>())
            svc->removeChangeListener(this);
    }
}

void SettingsView::changeListenerCallback(juce::ChangeBroadcaster*) {
    retranslateUi();
}

void SettingsView::retranslateUi() {
    extern BeatMate::ServiceLocator* g_serviceLocator;
    std::string lang = "fr";
    if (g_serviceLocator) {
        if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>())
            lang = svc->getCurrentLanguage();
    }

    auto rebuild = [](juce::ComboBox* cb, std::initializer_list<const char*> keys) {
        if (!cb) return;
        int prev = cb->getSelectedId();
        cb->clear(juce::dontSendNotification);
        int id = 1;
        for (auto k : keys) cb->addItem(BM_TJ(k), id++);
        if (prev > 0)
            cb->setSelectedId(prev, juce::dontSendNotification);
    };

    if (m_titleLabel) m_titleLabel->setText(BM_TJ("settings.title"), juce::dontSendNotification);
    if (m_resetBtn)   m_resetBtn->setButtonText(BM_TJ("settings.defaults"));
    if (m_cancelBtn)  m_cancelBtn->setButtonText(BM_TJ("settings.cancel"));
    if (m_applyBtn)   m_applyBtn->setButtonText(BM_TJ("settings.apply"));

    if (m_tabWidget) {
        auto& bar = m_tabWidget->getTabbedButtonBar();
        const char* tabKeys[] = {
            "settings.tab.general", "settings.tab.audio", "settings.tab.library",
            "settings.tab.analysis", "settings.tab.djSoftware", "settings.tab.licence",
            "settings.tab.backup", "settings.tab.shortcuts"
        };
        for (int i = 0; i < bar.getNumTabs() && i < 8; ++i)
            bar.setTabName(i, BM_TJ(tabKeys[i]));
    }

    rebuild(m_languageCombo.get(), { "settings.lang.fr", "settings.lang.en" });
    rebuild(m_themeCombo.get(),    { "settings.theme.dark", "settings.theme.light",
                                      "settings.theme.nord", "settings.theme.dracula",
                                      "settings.theme.highContrast", "settings.theme.custom" });
    rebuild(m_startupCombo.get(),  { "settings.combo.startup.fullscreen",
                                      "settings.combo.startup.lastSize",
                                      "settings.combo.startup.defaultSize" });
    rebuild(m_autoSaveIntervalCombo.get(), { "settings.combo.autoSave.1min",
                                              "settings.combo.autoSave.5min",
                                              "settings.combo.autoSave.10min",
                                              "settings.combo.autoSave.30min" });
    if (m_autoSaveCheck)     m_autoSaveCheck->setButtonText(BM_TJ("settings.btn.autoSave"));
    if (m_checkUpdatesCheck) m_checkUpdatesCheck->setButtonText(BM_TJ("settings.btn.checkUpdates"));
    if (m_changeDataFolderBtn) m_changeDataFolderBtn->setButtonText(BM_TJ("settings.changeFolder"));
    if (m_resetSettingsBtn)  m_resetSettingsBtn->setButtonText(BM_TJ("settings.btn.resetAllSettings"));

    rebuild(m_bufferSizeCombo.get(), { "settings.combo.audio.buffer64",
                                        "settings.combo.audio.buffer128",
                                        "settings.combo.audio.buffer256",
                                        "settings.combo.audio.buffer512",
                                        "settings.combo.audio.buffer1024",
                                        "settings.combo.audio.buffer2048" });
    rebuild(m_sampleRateCombo.get(), { "settings.combo.audio.sr44100",
                                        "settings.combo.audio.sr48000",
                                        "settings.combo.audio.sr88200",
                                        "settings.combo.audio.sr96000" });
    if (m_testAudioBtn) m_testAudioBtn->setButtonText(BM_TJ("settings.btn.testAudio"));

    if (m_addFolderBtn)           m_addFolderBtn->setButtonText(BM_TJ("settings.btn.addFolder"));
    if (m_removeFolderBtn)        m_removeFolderBtn->setButtonText(BM_TJ("settings.btn.remove"));
    if (m_autoImportCheck)        m_autoImportCheck->setButtonText(BM_TJ("settings.btn.autoImport"));
    if (m_analyzeOnImportCheck)   m_analyzeOnImportCheck->setButtonText(BM_TJ("settings.btn.analyzeOnImport"));
    if (m_detectDuplicatesCheck)  m_detectDuplicatesCheck->setButtonText(BM_TJ("settings.btn.detectDuplicates"));
    if (m_cleanMissingBtn)        m_cleanMissingBtn->setButtonText(BM_TJ("settings.btn.cleanMissing"));

    rebuild(m_analysisQualityCombo.get(), { "settings.combo.quality.fast",
                                             "settings.combo.quality.standard",
                                             "settings.combo.quality.high",
                                             "settings.combo.quality.ultra" });
    rebuild(m_bpmModeCombo.get(),         { "settings.combo.bpmMode.normal",
                                             "settings.combo.bpmMode.dynamic" });
    rebuild(m_keyMethodCombo.get(),       { "settings.combo.keyMethod.camelot",
                                             "settings.combo.keyMethod.openKey",
                                             "settings.combo.keyMethod.standard" });
    rebuild(m_analysisThreadsCombo.get(), { "settings.combo.threads.1",
                                             "settings.combo.threads.2",
                                             "settings.combo.threads.4",
                                             "settings.combo.threads.8" });
    if (m_autoAnalyzeCheck)       m_autoAnalyzeCheck->setButtonText(BM_TJ("settings.btn.autoAnalyze"));
    if (m_enableStemsCheck)       m_enableStemsCheck->setButtonText(BM_TJ("settings.btn.enableStems"));
    if (m_generateWaveformCheck)  m_generateWaveformCheck->setButtonText(BM_TJ("settings.btn.generateWaveform"));

    rebuild(m_syncIntervalCombo.get(), { "settings.combo.sync.hourly",
                                          "settings.combo.sync.6h",
                                          "settings.combo.sync.daily",
                                          "settings.combo.sync.weekly" });
    if (m_autoSyncDJCheck) m_autoSyncDJCheck->setButtonText(BM_TJ("settings.btn.autoSync"));
    if (m_scanDJBtn)       m_scanDJBtn->setButtonText(BM_TJ("settings.btn.scanNow"));
    if (m_syncDJBtn)       m_syncDJBtn->setButtonText(BM_TJ("settings.syncDj"));

    if (m_activateBtn) {
        bool locked = m_licenceKeyEdit && m_licenceKeyEdit->isReadOnly();
        m_activateBtn->setButtonText(locked ? BM_TJ("settings.btn.changeLicence")
                                             : BM_TJ("settings.btn.activateLicence"));
    }
    if (m_buyBtn) m_buyBtn->setButtonText(BM_TJ("settings.btn.buyLicence"));
    if (m_licenceKeyEdit)
        m_licenceKeyEdit->setTextToShowWhenEmpty(BM_TJ("settings.msg.licenceKeyPlaceholder"), juce::Colours::grey);

    rebuild(m_backupIntervalCombo.get(), { "settings.combo.backup.hourly",
                                            "settings.combo.backup.daily",
                                            "settings.combo.backup.weekly" });
    if (m_autoBackupCheck) m_autoBackupCheck->setButtonText(BM_TJ("settings.btn.autoBackup"));
    if (m_backupNowBtn)    m_backupNowBtn->setButtonText(BM_TJ("settings.btn.createBackup"));
    if (m_restoreBtn)      m_restoreBtn->setButtonText(BM_TJ("settings.btn.restoreBackup"));

    if (m_resetShortcutsBtn) m_resetShortcutsBtn->setButtonText(BM_TJ("settings.btn.resetShortcuts"));
    if (m_shortcutsTable) {
        auto& h = m_shortcutsTable->getHeader();
        if (h.getNumColumns(true) >= 2) {
            h.setColumnName(1, BM_TJ("settings.shortcut.colAction"));
            h.setColumnName(2, BM_TJ("settings.shortcut.colKey"));
        }
    }

    rebuild(m_accentColorCombo.get(), { "settings.combo.accent.blue",
                                         "settings.combo.accent.purple",
                                         "settings.combo.accent.green",
                                         "settings.combo.accent.orange",
                                         "settings.combo.accent.red",
                                         "settings.combo.accent.pink",
                                         "settings.combo.accent.cyan",
                                         "settings.combo.accent.gold" });
    rebuild(m_densityCombo.get(),       { "settings.combo.density.compact",
                                           "settings.combo.density.normal",
                                           "settings.combo.density.spacious" });
    rebuild(m_waveformStyleCombo.get(), { "settings.combo.wave.rgb",
                                           "settings.combo.wave.mono",
                                           "settings.combo.wave.blue",
                                           "settings.combo.wave.green" });

    repaint();
}

SettingsView::SettingsView()
{
    setupUI();

    extern BeatMate::ServiceLocator* g_serviceLocator;
    if (g_serviceLocator) {
        if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>())
            svc->addChangeListener(this);
    }

    auto settingsFile = getSettingsFile();
    bool needsMigration = true;
    if (settingsFile.existsAsFile()) {
        auto text = settingsFile.loadFileAsString();
        auto parsed = juce::JSON::parse(text);
        if (parsed.isObject()) {
            auto* root = parsed.getDynamicObject();
            if (root && root->hasProperty("general"))
                needsMigration = false;
        }
    }

    if (needsMigration) {
        spdlog::info("[Settings] Migrating settings to V11 format (deferred)");
        // Differe saveSettingsToJSON() apres le callAsync de setupUI()
        juce::Component::SafePointer<SettingsView> self(this);
        juce::MessageManager::callAsync([self]() {
            if (!self) return;
            juce::MessageManager::callAsync([self]() {
                if (!self) return;
                self->saveSettingsToJSON();
            });
        });
    }

    loadSettingsFromJSON();


    // Boot force le theme Dark, la selection persistee ne s'applique pas ici
    if (m_themeCombo)
        m_themeCombo->setSelectedId(1 + (int)ThemeEngine::instance().currentTheme(),
                                    juce::dontSendNotification);

    if (m_accentColorCombo && m_accentColorCombo->getSelectedId() > 0) {
        static const juce::uint32 accentColors[] = {
            0xFF3B82F6, 0xFF8B5CF6, 0xFF10B981, 0xFFEF4444,
            0xFFF59E0B, 0xFFEC4899, 0xFF06B6D4, 0xFFF1F5F9
        };
        int idx = juce::jlimit(0, 7, m_accentColorCombo->getSelectedId() - 1);
        Colors::Dynamic::setAccentColor(accentColors[idx]);
    }

    auto autoApply = [this] { applySettings(); };

    if (m_accentColorCombo)   m_accentColorCombo->onChange = autoApply;
    if (m_densityCombo)       m_densityCombo->onChange = autoApply;
    if (m_waveformStyleCombo) m_waveformStyleCombo->onChange = autoApply;
    if (m_fontSizeSlider)     m_fontSizeSlider->onValueChange = autoApply;

    if (m_languageCombo)          m_languageCombo->onChange = autoApply;
    if (m_startupCombo)           m_startupCombo->onChange = autoApply;
    if (m_autoSaveIntervalCombo)  m_autoSaveIntervalCombo->onChange = autoApply;
    if (m_autoSaveCheck)          m_autoSaveCheck->onClick = autoApply;
    if (m_checkUpdatesCheck)      m_checkUpdatesCheck->onClick = autoApply;

    // Audio (buffer/samplerate have their own onChange for latency calc, keep those)
    if (m_audioDeviceCombo) m_audioDeviceCombo->onChange = autoApply;
    if (m_audioInputCombo)  m_audioInputCombo->onChange = autoApply;

    if (m_autoImportCheck)       m_autoImportCheck->onClick = autoApply;
    if (m_analyzeOnImportCheck)  m_analyzeOnImportCheck->onClick = autoApply;
    if (m_detectDuplicatesCheck) m_detectDuplicatesCheck->onClick = autoApply;

    if (m_analysisQualityCombo) m_analysisQualityCombo->onChange = autoApply;
    if (m_bpmModeCombo)         m_bpmModeCombo->onChange = autoApply;
    if (m_keyMethodCombo)       m_keyMethodCombo->onChange = autoApply;
    if (m_analysisThreadsCombo) m_analysisThreadsCombo->onChange = autoApply;
    if (m_autoAnalyzeCheck)     m_autoAnalyzeCheck->onClick = autoApply;
    if (m_enableStemsCheck)     m_enableStemsCheck->onClick = autoApply;
    if (m_generateWaveformCheck) m_generateWaveformCheck->onClick = autoApply;
    if (m_bpmMinSlider)         m_bpmMinSlider->onValueChange = autoApply;
    if (m_bpmMaxSlider)         m_bpmMaxSlider->onValueChange = autoApply;

    if (m_autoSyncDJCheck)   m_autoSyncDJCheck->onClick = autoApply;
    if (m_syncIntervalCombo) m_syncIntervalCombo->onChange = autoApply;

    if (m_autoBackupCheck)      m_autoBackupCheck->onClick = autoApply;
    if (m_backupIntervalCombo)  m_backupIntervalCombo->onChange = autoApply;
    if (m_maxBackupsSpin)       m_maxBackupsSpin->onValueChange = autoApply;
}

void SettingsView::setupUI()
{
    m_titleLabel = std::make_unique<juce::Label>("t", BM_TJ("settings.title"));
    m_titleLabel->setFont(juce::Font(24.0f, juce::Font::bold));
    m_titleLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    addAndMakeVisible(*m_titleLabel);

    m_tabWidget = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    m_tabWidget->setColour(juce::TabbedComponent::backgroundColourId, Colors::bgDark());
    m_tabWidget->setTabBarDepth(32);

    // Perf : seul l'onglet General est construit en synchrone, le reste en differe
    m_tabWidget->addTab(BM_TJ("settings.tab.general"), Colors::bgDark(), createGeneralTab(), true);

    juce::Component::SafePointer<SettingsView> self(this);
    juce::MessageManager::callAsync([self]() {
        if (!self || !self->m_tabWidget) return;
        self->m_tabWidget->addTab(BM_TJ("settings.tab.audio"),      Colors::bgDark(), self->createAudioTab(),        true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.library"),    Colors::bgDark(), self->createLibraryTab(),      true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.analysis"),   Colors::bgDark(), self->createAnalysisTab(),     true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.djSoftware"), Colors::bgDark(), self->createDJSoftwareTab(),   true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.licence"),    Colors::bgDark(), self->createLicenceTab(),      true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.backup"),     Colors::bgDark(), self->createBackupTab(),       true);
        self->m_tabWidget->addTab(BM_TJ("settings.tab.shortcuts"),  Colors::bgDark(), self->createShortcutsTab(),    true);
        spdlog::info("[SettingsView] async tabs added (PERF deferred construction)");
    });
    // Onglet Apparence retire a la demande utilisateur

    addAndMakeVisible(*m_tabWidget);

    auto createBottomBtn = [this](const juce::String& text, juce::Colour bg) {
        auto btn = std::make_unique<juce::TextButton>(text);
        btn->setColour(juce::TextButton::buttonColourId, bg);
        btn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
        addAndMakeVisible(*btn);
        return btn;
    };

    m_resetBtn  = createBottomBtn(BM_TJ("settings.defaults"), Colors::bgLighter());
    m_cancelBtn = createBottomBtn(BM_TJ("settings.cancel"),   Colors::bgLighter());
    m_applyBtn  = createBottomBtn(BM_TJ("settings.apply"),    Colors::primary());

    m_resetBtn->onClick  = [this] { resetDefaults(); };
    m_cancelBtn->onClick = [this] { cancelChanges(); };
    m_applyBtn->onClick  = [this] { applySettings(); };
}

juce::Component* SettingsView::createGeneralTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.general"), 24, y, 300, 22, true);
    y += 36;

    makeLabel(page, BM_TJ("settings.label.language"), 24, y, 160, 24);
    m_languageCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 190, y, 220, 26));
    m_languageCombo->addItem(BM_TJ("settings.lang.fr"), 1);
    m_languageCombo->addItem(BM_TJ("settings.lang.en"), 2);
    m_languageCombo->setSelectedId(1);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.theme"), 24, y, 160, 24);
    m_themeCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 190, y, 220, 26));
    m_themeCombo->addItem(BM_TJ("settings.theme.dark"), 1);
    m_themeCombo->addItem(BM_TJ("settings.theme.light"), 2);
    m_themeCombo->addItem(BM_TJ("settings.theme.nord"), 3);
    m_themeCombo->addItem(BM_TJ("settings.theme.dracula"), 4);
    m_themeCombo->addItem(BM_TJ("settings.theme.highContrast"), 5);
    m_themeCombo->addItem(BM_TJ("settings.theme.custom"), 6);
    m_themeCombo->setSelectedId(1 + (int)ThemeEngine::instance().currentTheme(),
                                 juce::dontSendNotification);
    m_themeCombo->onChange = [this] {
        const int id = m_themeCombo->getSelectedId();
        auto& eng = ThemeEngine::instance();
        ThemeEngine::Theme t;
        switch (id) {
            case 2:  t = ThemeEngine::Light;        break;
            case 3:  t = ThemeEngine::Nord;         break;
            case 4:  t = ThemeEngine::Dracula;      break;
            case 5:  t = ThemeEngine::HighContrast; break;
            case 6:  t = ThemeEngine::Custom;       break;
            default: t = ThemeEngine::Dark;         break;
        }
        eng.setTheme(t);
        eng.applyTheme(juce::Desktop::getInstance().getDefaultLookAndFeel());
        if (t == ThemeEngine::Custom)
            eng.saveCustomTheme(ThemeEngine::defaultCustomThemeFile().getFullPathName());
        if (auto* tlw = getTopLevelComponent()) tlw->repaint();
        applySettings();
    };
    y += 34;

    makeLabel(page, BM_TJ("settings.label.startup"), 24, y, 160, 24);
    m_startupCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 190, y, 220, 26));
    m_startupCombo->addItem(BM_TJ("settings.combo.startup.fullscreen"), 1);
    m_startupCombo->addItem(BM_TJ("settings.combo.startup.lastSize"), 2);
    m_startupCombo->addItem(BM_TJ("settings.combo.startup.defaultSize"), 3);
    m_startupCombo->setSelectedId(2);
    y += 34;

    m_autoSaveCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.autoSave"), 24, y, 200, 24, true));
    m_autoSaveIntervalCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 170, 26));
    m_autoSaveIntervalCombo->addItem(BM_TJ("settings.combo.autoSave.1min"), 1);
    m_autoSaveIntervalCombo->addItem(BM_TJ("settings.combo.autoSave.5min"), 2);
    m_autoSaveIntervalCombo->addItem(BM_TJ("settings.combo.autoSave.10min"), 3);
    m_autoSaveIntervalCombo->addItem(BM_TJ("settings.combo.autoSave.30min"), 4);
    m_autoSaveIntervalCombo->setSelectedId(2);
    y += 34;

    m_checkUpdatesCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.checkUpdates"), 24, y, 400, 24, true));
    y += 34;

    m_checkUpdateNowBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.checkUpdateNow"), 24, y, 260, 28, Colors::accent()));
    m_checkUpdateNowBtn->onClick = [this] { runOnlineUpdateCheck(); };

    m_installLocalMsiBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.installLocalMsi"), 296, y, 220, 28, Colors::bgLighter()));
    m_installLocalMsiBtn->onClick = [this] { runLocalMsiInstall(); };
    y += 34;

    m_updateStatusLbl = std::make_unique<juce::Label>("", juce::String("BeatMate V") + BEATMATE_VERSION);
    m_updateStatusLbl->setFont(juce::Font(12.0f));
    m_updateStatusLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    m_updateStatusLbl->setBounds(24, y, 520, 20);
    page->addAndMakeVisible(*m_updateStatusLbl);
    y += 32;

    makeLabel(page, BM_TJ("settings.label.dataFolder"), 24, y, 160, 24);
    m_dataFolderLabel = std::make_unique<juce::Label>("", juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("BeatMate").getFullPathName());
    m_dataFolderLabel->setFont(juce::Font(12.0f));
    m_dataFolderLabel->setColour(juce::Label::textColourId, Colors::textMuted());
    m_dataFolderLabel->setBounds(24, y + 24, 360, 20);
    page->addAndMakeVisible(*m_dataFolderLabel);

    m_changeDataFolderBtn = std::make_unique<juce::TextButton>(BM_TJ("settings.changeFolder"));
    m_changeDataFolderBtn->setColour(juce::TextButton::buttonColourId, Colors::bgLighter());
    m_changeDataFolderBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_changeDataFolderBtn->setBounds(400, y, 100, 26);
    m_changeDataFolderBtn->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(BM_TJ("settings.msg.chooseDataFolder"),
            juce::File(m_dataFolderLabel->getText()));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists())
                    m_dataFolderLabel->setText(result.getFullPathName(), juce::dontSendNotification);
            });
    };
    page->addAndMakeVisible(*m_changeDataFolderBtn);
    y += 56;

    m_resetSettingsBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.resetAllSettings"), 24, y, 260, 30, Colors::error()));
    m_resetSettingsBtn->onClick = [this] { resetDefaults(); };

    return page;
}

void SettingsView::runOnlineUpdateCheck()
{
    if (m_updateStatusLbl)
        m_updateStatusLbl->setText(juce::String::fromUTF8("Recherche de mises a jour..."),
                                   juce::dontSendNotification);
    if (m_checkUpdateNowBtn) m_checkUpdateNowBtn->setEnabled(false);

    std::string base = "https://beatmate.fr/beatmate-mise-a-jour";
    if (auto* sm = g_serviceLocator ? g_serviceLocator->tryGet<Services::Config::SettingsManager>() : nullptr) {
        const auto v = sm->get<std::string>("general.updateBaseUrl", std::string());
        if (! v.empty()) base = v;
    }

    auto svc = std::make_shared<Services::Update::UpdateService>(std::string(BEATMATE_VERSION));
    svc->setBaseUrl(base);

    juce::Component::SafePointer<SettingsView> self (this);
    svc->checkRemoteAsync([self, svc](Services::Update::UpdateInfo info)
    {
        if (self == nullptr) return;
        if (self->m_checkUpdateNowBtn) self->m_checkUpdateNowBtn->setEnabled(true);

        if (! info.error.empty()) {
            if (self->m_updateStatusLbl)
                self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Erreur : " + info.error).c_str()),
                                                 juce::dontSendNotification);
            return;
        }
        if (! info.available) {
            if (self->m_updateStatusLbl)
                self->m_updateStatusLbl->setText(juce::String("A jour (V") + BEATMATE_VERSION + ")",
                                                 juce::dontSendNotification);
            return;
        }

        if (self->m_updateStatusLbl)
            self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Mise a jour disponible : V" + info.latestVersion).c_str()),
                                             juce::dontSendNotification);

        juce::String msg;
        msg << juce::String::fromUTF8("Une nouvelle version est disponible : V")
            << juce::String(info.latestVersion)
            << " (version actuelle V" << BEATMATE_VERSION << ").\n\n";
        if (! info.notes.empty()) msg << juce::String::fromUTF8(info.notes.c_str()) << "\n\n";
        msg << juce::String::fromUTF8("Telecharger et installer maintenant ? L'application se fermera pour terminer l'installation.");

        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8("Mise a jour BeatMate"),
            msg,
            juce::String::fromUTF8("Mettre a jour"),
            juce::String::fromUTF8("Annuler"),
            nullptr,
            juce::ModalCallbackFunction::create([self, svc, info](int result)
            {
                if (result != 1 || self == nullptr) return;
                if (self->m_updateStatusLbl)
                    self->m_updateStatusLbl->setText(juce::String::fromUTF8("Telechargement..."),
                                                     juce::dontSendNotification);

                juce::Component::SafePointer<SettingsView> inner (self);
                svc->downloadAndInstallAsync(info.downloadUrl,
                    [inner](bool ok, std::string m)
                    {
                        if (inner && inner->m_updateStatusLbl)
                            inner->m_updateStatusLbl->setText(juce::String::fromUTF8(m.c_str()),
                                                              juce::dontSendNotification);
                        if (ok)
                            if (auto* app = juce::JUCEApplicationBase::getInstance())
                                app->systemRequestedQuit();
                    },
                    [inner](double pct)
                    {
                        if (inner && inner->m_updateStatusLbl)
                            inner->m_updateStatusLbl->setText(
                                "Telechargement... " + juce::String(juce::roundToInt(pct * 100.0)) + "%",
                                juce::dontSendNotification);
                    });
            }));
    });
}

void SettingsView::runLocalMsiInstall()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8("Selectionner l'installeur MSI"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.msi");

    juce::Component::SafePointer<SettingsView> self (this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [self, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (! file.existsAsFile() || self == nullptr) return;

            Services::Update::UpdateService svc(std::string(BEATMATE_VERSION));
            std::string err;
            if (svc.installFromFile(file, err)) {
                if (self->m_updateStatusLbl)
                    self->m_updateStatusLbl->setText(juce::String::fromUTF8("Installeur lance, fermeture..."),
                                                     juce::dontSendNotification);
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            } else if (self->m_updateStatusLbl) {
                self->m_updateStatusLbl->setText(juce::String::fromUTF8(("Echec : " + err).c_str()),
                                                 juce::dontSendNotification);
            }
        });
}

juce::Component* SettingsView::createAudioTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.audio"), 24, y, 300, 22, true);
    y += 36;

    makeLabel(page, BM_TJ("settings.label.outputDevice"), 24, y, 200, 24);
    m_audioDeviceCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 300, 26));
    {
        // FIX freeze boot : enumerer les devices sans initialisation (initialiseWithDefaultDevices ouvre le device)
        juce::AudioDeviceManager tempManager;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        tempManager.createAudioDeviceTypes(types);
        if (types.size() > 0)
        {
            auto* type = types[0];
            type->scanForDevices();
            auto deviceNames = type->getDeviceNames();
            for (int i = 0; i < deviceNames.size(); ++i)
                m_audioDeviceCombo->addItem(deviceNames[i], i + 1);
        }
    }
    if (m_audioDeviceCombo->getNumItems() == 0)
        m_audioDeviceCombo->addItem(BM_TJ("settings.combo.audio.defaultDevice"), 1);
    m_audioDeviceCombo->setSelectedId(1);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.inputDevice"), 24, y, 200, 24);
    m_audioInputCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 300, 26));
    m_audioInputCombo->addItem(BM_TJ("settings.combo.audio.noneDisabled"), 1);
    {
        // Idem : énumération sans initialisation (cf. note ci-dessus).
        juce::AudioDeviceManager tempManager;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        tempManager.createAudioDeviceTypes(types);
        if (types.size() > 0)
        {
            auto* type = types[0];
            type->scanForDevices();
            auto deviceNames = type->getDeviceNames(true);
            for (int i = 0; i < deviceNames.size(); ++i)
                m_audioInputCombo->addItem(deviceNames[i], i + 2);
        }
    }
    m_audioInputCombo->setSelectedId(1);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.bufferSize"), 24, y, 200, 24);
    m_bufferSizeCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 180, 26));
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer64"), 1);
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer128"), 2);
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer256"), 3);
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer512"), 4);
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer1024"), 5);
    m_bufferSizeCombo->addItem(BM_TJ("settings.combo.audio.buffer2048"), 6);
    m_bufferSizeCombo->setSelectedId(4);
    m_bufferSizeCombo->onChange = [this] {
        int bufferSizes[] = { 64, 128, 256, 512, 1024, 2048 };
        int sampleRates[] = { 44100, 48000, 88200, 96000 };
        int bufIdx = m_bufferSizeCombo->getSelectedId() - 1;
        int srIdx  = m_sampleRateCombo->getSelectedId() - 1;
        if (bufIdx >= 0 && bufIdx < 6 && srIdx >= 0 && srIdx < 4) {
            double latencyMs = (double)bufferSizes[bufIdx] / (double)sampleRates[srIdx] * 1000.0;
            m_latencyLabel->setText(BM_TJ("settings.label.latencyPrefix") + juce::String(latencyMs, 1) + " ms",
                                     juce::dontSendNotification);
        }
        applySettings();
    };
    y += 34;

    makeLabel(page, BM_TJ("settings.label.sampleRate"), 24, y, 240, 24);
    m_sampleRateCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 280, y, 180, 26));
    m_sampleRateCombo->addItem(BM_TJ("settings.combo.audio.sr44100"), 1);
    m_sampleRateCombo->addItem(BM_TJ("settings.combo.audio.sr48000"), 2);
    m_sampleRateCombo->addItem(BM_TJ("settings.combo.audio.sr88200"), 3);
    m_sampleRateCombo->addItem(BM_TJ("settings.combo.audio.sr96000"), 4);
    m_sampleRateCombo->setSelectedId(1);
    m_sampleRateCombo->onChange = [this] {
        if (m_bufferSizeCombo && m_bufferSizeCombo->onChange)
            m_bufferSizeCombo->onChange(); // This also calls applySettings()
    };
    y += 34;

    m_latencyLabel = std::make_unique<juce::Label>("", BM_TJ("settings.label.latencyPrefix") + "11.6 ms");
    m_latencyLabel->setFont(juce::Font(14.0f, juce::Font::bold));
    m_latencyLabel->setColour(juce::Label::textColourId, Colors::primary());
    m_latencyLabel->setBounds(24, y, 300, 24);
    page->addAndMakeVisible(*m_latencyLabel);
    y += 38;

    m_testAudioBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.testAudio"), 24, y, 160, 30, Colors::secondary()));
    m_testAudioBtn->onClick = [this] {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (! g_serviceLocator) return;

        auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>();
        auto* preview = g_serviceLocator->tryGet<Core::AudioPreview>();
        if (! preview) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("settings.msg.testAudioTitle"),
                juce::String::fromUTF8("Le moteur audio n'est pas initialis\xc3\xa9."));
            return;
        }

        double sr = engine ? engine->getSampleRate() : 44100.0;
        if (sr <= 0.0) sr = 44100.0;

        const int numSamples = static_cast<int>(sr * 1.2);
        std::vector<float> tone(static_cast<size_t>(numSamples) * 2);
        const int fade = static_cast<int>(sr * 0.01);
        for (int i = 0; i < numSamples; ++i) {
            const double t = static_cast<double>(i) / sr;
            const double freq = (t < 0.4) ? 440.0 : (t < 0.8 ? 554.37 : 659.25);
            double env = 1.0;
            if (i < fade) env = static_cast<double>(i) / fade;
            else if (i > numSamples - fade) env = static_cast<double>(numSamples - i) / fade;
            const float v = static_cast<float>(
                std::sin(2.0 * juce::MathConstants<double>::pi * freq * t) * 0.25 * env);
            tone[static_cast<size_t>(i) * 2] = v;
            tone[static_cast<size_t>(i) * 2 + 1] = v;
        }

        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("beatmate_audio_test.wav");
        Core::AudioTrack seg;
        seg.loadData(tone.data(), tone.size(), static_cast<int>(sr), 2);
        Core::AudioFileWriter writer;
        if (! writer.writeWAV(seg, tmp.getFullPathName().toStdString())) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("settings.msg.testAudioTitle"),
                juce::String::fromUTF8("Impossible de g\xc3\xa9n\xc3\xa9rer le son de test."));
            return;
        }

        preview->previewTrack(tmp.getFullPathName().toStdString(), 0.0, 1.2);

        juce::String device = juce::String::fromUTF8("p\xc3\xa9riph\xc3\xa9rique par d\xc3\xa9""faut");
        if (engine) {
            if (auto* dev = engine->getJuceDeviceManager().getCurrentAudioDevice())
                device = dev->getName();
        }
        Widgets::ToastNotifier::getInstance().show(
            BM_TJ("settings.msg.testAudioTitle"),
            juce::String::fromUTF8("Trois notes sont jou\xc3\xa9""es sur ") + device
                + juce::String::fromUTF8(". Si vous n'entendez rien, v\xc3\xa9rifiez la sortie "
                                         "choisie ci-dessus et le volume de Windows."),
            Widgets::ToastNotifier::Kind::Info, 7000);
    };

    return page;
}

juce::Component* SettingsView::createLibraryTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.library"), 24, y, 300, 22, true);
    y += 36;

    makeLabel(page, BM_TJ("settings.label.watchedFolders"), 24, y, 200, 22);
    y += 26;

    m_watchFoldersModel.folders = &m_watchFolders;
    m_watchFoldersList = std::make_unique<juce::ListBox>("watchFolders", &m_watchFoldersModel);
    m_watchFoldersList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_watchFoldersList->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_watchFoldersList->setRowHeight(24);
    m_watchFoldersList->setBounds(24, y, 400, 120);
    page->addAndMakeVisible(*m_watchFoldersList);

    m_addFolderBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.addFolder"), 430, y, 100, 28, Colors::primary()));
    m_addFolderBtn->onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(BM_TJ("settings.msg.addWatchFolder"));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    m_watchFolders.add(result.getFullPathName());
                    m_watchFoldersList->updateContent();
                }
            });
    };

    m_removeFolderBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.remove"), 430, y + 34, 100, 28, Colors::error()));
    m_removeFolderBtn->onClick = [this] {
        int sel = m_watchFoldersList->getSelectedRow();
        if (sel >= 0 && sel < m_watchFolders.size()) {
            m_watchFolders.remove(sel);
            m_watchFoldersList->updateContent();
        }
    };

    y += 140;

    m_autoImportCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.autoImport"), 24, y, 400, 24, true));
    y += 30;

    m_analyzeOnImportCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.analyzeOnImport"), 24, y, 400, 24, true));
    y += 30;

    m_detectDuplicatesCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.detectDuplicates"), 24, y, 400, 24, true));
    y += 36;

    makeLabel(page, BM_TJ("settings.label.supportedFormats"), 24, y, 200, 22);
    y += 24;
    auto* fmtLbl = new juce::Label("", BM_TJ("settings.msg.formatsList"));
    fmtLbl->setFont(juce::Font(12.0f));
    fmtLbl->setColour(juce::Label::textColourId, Colors::primary());
    fmtLbl->setBounds(24, y, 500, 20);
    page->addAndMakeVisible(fmtLbl);
    y += 30;

    m_cleanMissingBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.cleanMissing"), 24, y, 220, 30, Colors::warning()));
    m_cleanMissingBtn->onClick = [] {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        int missing = 0;
        if (g_serviceLocator) {
            auto* provider = g_serviceLocator->tryGet<Services::Library::TrackDataProvider>();
            if (provider) {
                auto tracks = provider->getAllTracks();
                for (const auto& t : tracks) {
                    if (!t.filePath.empty() && !juce::File(t.filePath).existsAsFile())
                        missing++;
                }
            }
        }
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("settings.msg.cleanupTitle"),
            juce::String(missing) + BM_TJ("settings.msg.cleanupPrefix"));
    };

    return page;
}

juce::Component* SettingsView::createAnalysisTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.analysis"), 24, y, 300, 22, true);
    y += 36;

    makeLabel(page, BM_TJ("settings.label.analysisQuality"), 24, y, 200, 24);
    m_analysisQualityCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 200, 26));
    m_analysisQualityCombo->addItem(BM_TJ("settings.combo.quality.fast"), 1);
    m_analysisQualityCombo->addItem(BM_TJ("settings.combo.quality.standard"), 2);
    m_analysisQualityCombo->addItem(BM_TJ("settings.combo.quality.high"), 3);
    m_analysisQualityCombo->addItem(BM_TJ("settings.combo.quality.ultra"), 4);
    m_analysisQualityCombo->setSelectedId(2);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.bpmMin"), 24, y, 200, 24);
    m_bpmMinSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_bpmMinSlider->setRange(60, 120, 1);
    m_bpmMinSlider->setValue(70);
    m_bpmMinSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_bpmMinSlider->setColour(juce::Slider::trackColourId, Colors::bgLighter());
    m_bpmMinSlider->setTextValueSuffix(" BPM");
    m_bpmMinSlider->setBounds(240, y, 280, 26);
    page->addAndMakeVisible(*m_bpmMinSlider);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.bpmMax"), 24, y, 200, 24);
    m_bpmMaxSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_bpmMaxSlider->setRange(120, 200, 1);
    m_bpmMaxSlider->setValue(180);
    m_bpmMaxSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_bpmMaxSlider->setColour(juce::Slider::trackColourId, Colors::bgLighter());
    m_bpmMaxSlider->setTextValueSuffix(" BPM");
    m_bpmMaxSlider->setBounds(240, y, 280, 26);
    page->addAndMakeVisible(*m_bpmMaxSlider);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.bpmMode"), 24, y, 200, 24);
    m_bpmModeCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 200, 26));
    m_bpmModeCombo->addItem(BM_TJ("settings.combo.bpmMode.normal"), 1);
    m_bpmModeCombo->addItem(BM_TJ("settings.combo.bpmMode.dynamic"), 2);
    m_bpmModeCombo->setSelectedId(1);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.keyNotation"), 24, y, 200, 24);
    m_keyMethodCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 200, 26));
    m_keyMethodCombo->addItem(BM_TJ("settings.combo.keyMethod.camelot"), 1);
    m_keyMethodCombo->addItem(BM_TJ("settings.combo.keyMethod.openKey"), 2);
    m_keyMethodCombo->addItem(BM_TJ("settings.combo.keyMethod.standard"), 3);
    m_keyMethodCombo->setSelectedId(1);
    y += 34;

    m_autoAnalyzeCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.autoAnalyze"), 24, y, 300, 24, true));
    y += 30;

    makeLabel(page, BM_TJ("settings.label.parallelism"), 24, y, 200, 24);
    m_analysisThreadsCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 140, 26));
    m_analysisThreadsCombo->addItem(BM_TJ("settings.combo.threads.1"), 1);
    m_analysisThreadsCombo->addItem(BM_TJ("settings.combo.threads.2"), 2);
    m_analysisThreadsCombo->addItem(BM_TJ("settings.combo.threads.4"), 3);
    m_analysisThreadsCombo->addItem(BM_TJ("settings.combo.threads.8"), 4);
    m_analysisThreadsCombo->setSelectedId(2);
    y += 34;

    m_enableStemsCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.enableStems"), 24, y, 400, 24, false));
    y += 30;

    m_generateWaveformCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.generateWaveform"), 24, y, 400, 24, true));

    return page;
}

juce::Component* SettingsView::createDJSoftwareTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.djSoftware"), 24, y, 300, 22, true);
    y += 36;

    struct DJSoftwareDef { juce::String name; };

    DJSoftwareDef softwareList[] = {
        { "Rekordbox" }, { "Serato DJ" }, { "Traktor Pro" },
        { "VirtualDJ" }, { "Engine DJ" }, { "djay Pro" }
    };

    makeLabel(page, BM_TJ("settings.label.software"), 24, y, 200, 20);
    makeLabel(page, BM_TJ("settings.label.status"), 340, y, 100, 20);
    y += 24;

    auto* sepLine = new juce::Component();
    sepLine->setBounds(24, y, 500, 1);
    page->addAndMakeVisible(sepLine);
    y += 6;

    m_djSoftwareRows.clear();
    for (auto& sw : softwareList)
    {
        DJSoftwareRow row;
        row.nameLbl = std::make_unique<juce::Label>("", sw.name);
        row.nameLbl->setFont(juce::Font(14.0f));
        row.nameLbl->setColour(juce::Label::textColourId, Colors::textPrimary());
        row.nameLbl->setBounds(24, y, 300, 24);
        page->addAndMakeVisible(*row.nameLbl);

        row.statusLbl = std::make_unique<juce::Label>("");
        row.statusLbl->setFont(juce::Font(12.0f, juce::Font::bold));
        row.statusLbl->setText(BM_TJ("settings.notDetected"), juce::dontSendNotification);
        row.statusLbl->setColour(juce::Label::textColourId, Colors::textMuted());
        row.statusLbl->setBounds(340, y, 150, 24);
        page->addAndMakeVisible(*row.statusLbl);

        m_djSoftwareRows.push_back(std::move(row));
        y += 28;
    }

    y += 16;

    m_autoSyncDJCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.autoSync"), 24, y, 250, 24, false));
    m_syncIntervalCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 280, y, 180, 26));
    m_syncIntervalCombo->addItem(BM_TJ("settings.combo.sync.hourly"), 1);
    m_syncIntervalCombo->addItem(BM_TJ("settings.combo.sync.6h"), 2);
    m_syncIntervalCombo->addItem(BM_TJ("settings.combo.sync.daily"), 3);
    m_syncIntervalCombo->addItem(BM_TJ("settings.combo.sync.weekly"), 4);
    m_syncIntervalCombo->setSelectedId(3);
    y += 38;

    m_scanDJBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.scanNow"), 24, y, 180, 30, Colors::primary()));
    m_scanDJBtn->onClick = [this] {
        for (auto& row : m_djSoftwareRows)  {
            row.statusLbl->setText(BM_TJ("settings.msg.scanInProgress"), juce::dontSendNotification);
            row.statusLbl->setColour(juce::Label::textColourId, Colors::warning());
        }

        juce::Timer::callAfterDelay(300, [this] {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            if (g_serviceLocator) {
                auto* detector = g_serviceLocator->tryGet<Services::DJSoftware::DJSoftwareDetector>();
                if (detector) {
                    auto results = detector->detect();
                    // Map: Rekordbox=0, Serato=1, Traktor=2, VirtualDJ=3, EngineDJ=4, DjayPro=5
                    for (auto& info : results) {
                        int idx = -1;
                        switch (info.type) {
                            case Services::DJSoftware::DJSoftwareType::Rekordbox: idx = 0; break;
                            case Services::DJSoftware::DJSoftwareType::Serato:    idx = 1; break;
                            case Services::DJSoftware::DJSoftwareType::Traktor:   idx = 2; break;
                            case Services::DJSoftware::DJSoftwareType::VirtualDJ: idx = 3; break;
                            case Services::DJSoftware::DJSoftwareType::EngineDJ:  idx = 4; break;
                            case Services::DJSoftware::DJSoftwareType::DjayPro:   idx = 5; break;
                        }
                        if (idx >= 0 && idx < static_cast<int>(m_djSoftwareRows.size())) {
                            if (info.isInstalled) {
                                juce::String statusText = BM_TJ("settings.msg.detectedPrefix");
                                if (!info.version.empty())
                                    statusText += BM_TJ("settings.msg.detectedVersionPrefix") + juce::String(info.version) + ")";
                                if (!info.databasePath.empty())
                                    statusText += BM_TJ("settings.msg.detectedDbFound");
                                m_djSoftwareRows[idx].statusLbl->setText(statusText, juce::dontSendNotification);
                                m_djSoftwareRows[idx].statusLbl->setColour(juce::Label::textColourId, Colors::success());
                            } else {
                                m_djSoftwareRows[idx].statusLbl->setText(BM_TJ("settings.notDetected"), juce::dontSendNotification);
                                m_djSoftwareRows[idx].statusLbl->setColour(juce::Label::textColourId, Colors::textMuted());
                            }
                        }
                    }
                    spdlog::info("[Settings] DJ Software scan: {} software detected", results.size());
                    return;
                }
            }

            spdlog::warn("[Settings] DJSoftwareDetector not available, using fallback paths");
            auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getFullPathName();
            auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getFullPathName();
            struct FallbackCheck { juce::String path; };
            std::vector<FallbackCheck> checks = {
                { appData + "/Pioneer/rekordbox" },    // Rekordbox
                { appData + "/../Local/Serato" },       // Serato (LocalAppData)
                { appData + "/../Local/Native Instruments/Traktor Pro 3" }, // Traktor
                { docs + "/VirtualDJ" },                // VirtualDJ
                { appData + "/Engine DJ" },             // Engine DJ
                { appData + "/Algoriddim/djay" }        // djay Pro
            };
            for (size_t i = 0; i < m_djSoftwareRows.size() && i < checks.size(); ++i) {
                bool found = checks[i].path.isNotEmpty() && juce::File(checks[i].path).exists();
                m_djSoftwareRows[i].statusLbl->setText(found ? BM_TJ("settings.detected") : BM_TJ("settings.notDetected"), juce::dontSendNotification);
                m_djSoftwareRows[i].statusLbl->setColour(juce::Label::textColourId, found ? Colors::success() : Colors::textMuted());
            }
        });
    };

    y += 38;

    m_syncDJBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.syncDj"), 24, y, 240, 30, Colors::accent()));
    m_syncDJBtn->onClick = [this] {
        m_syncDJBtn->setButtonText(BM_TJ("settings.syncDjInProgress"));
        m_syncDJBtn->setEnabled(false);

        juce::Component::SafePointer<SettingsView> self(this);
        std::thread([self] {
            extern BeatMate::ServiceLocator* g_serviceLocator;
            Services::DJSoftware::CollectionSyncService* syncService = nullptr;
            if (g_serviceLocator)
                syncService = g_serviceLocator->tryGet<Services::DJSoftware::CollectionSyncService>();

            if (syncService) {
                syncService->syncAll();
                spdlog::info("[Settings] Manual collection sync triggered");
                juce::MessageManager::callAsync([self] {
                    if (!self) return;
                    self->m_syncDJBtn->setButtonText(BM_TJ("settings.syncDjDone"));
                    self->m_syncDJBtn->setEnabled(true);
                    juce::Timer::callAfterDelay(3000, [self] {
                        if (!self) return;
                        self->m_syncDJBtn->setButtonText(BM_TJ("settings.syncDj"));
                    });
                });
                return;
            }

            juce::MessageManager::callAsync([self] {
                if (!self) return;
                self->m_syncDJBtn->setButtonText(BM_TJ("settings.msg.serviceUnavailable"));
                self->m_syncDJBtn->setEnabled(true);
                juce::Timer::callAfterDelay(3000, [self] {
                    if (!self) return;
                    self->m_syncDJBtn->setButtonText(BM_TJ("settings.syncDj"));
                });
            });
        }).detach();
    };

    y += 38;

    auto* importBtn = makeButton(page, BM_TJ("settings.btn.importRekordboxXml"),
                                 24, y, 240, 30, juce::Colour(0xFF8B5CF6));
    importBtn->onClick = [importBtn] {
        auto chooser = std::make_shared<juce::FileChooser>(
            BM_TJ("settings.msg.rbXmlImport"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.xml");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
            [chooser, importBtn](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (!f.existsAsFile()) return;
                spdlog::info("[Settings] Import RB XML: {}",
                             f.getFullPathName().toStdString());
                extern BeatMate::ServiceLocator* g_serviceLocator;
                if (!g_serviceLocator) return;
                auto* rb = g_serviceLocator->tryGet<Services::Rekordbox::RekordboxService>();
                if (!rb) return;

                importBtn->setButtonText(BM_TJ("settings.syncDjInProgress"));
                importBtn->setEnabled(false);
                juce::Component::SafePointer<juce::TextButton> btnSafe(importBtn);

                std::thread([rb, f, btnSafe]() {
                    auto sum = rb->importFromXmlFile(f, nullptr, nullptr);
                    juce::MessageManager::callAsync([btnSafe, sum]() {
                        if (!btnSafe) return;
                        btnSafe->setEnabled(true);
                        if (sum.ok()) {
                            btnSafe->setButtonText(BM_TJ("settings.msg.rbXmlImportedPrefix") +
                                                   juce::String(sum.tracksImported) + BM_TJ("settings.msg.rbXmlImportedSuffix"));
                            juce::Component::SafePointer<juce::TextButton> b2(btnSafe);
                            juce::Timer::callAfterDelay(3000, [b2] {
                                if (b2) b2->setButtonText(BM_TJ("settings.btn.importRekordboxXml"));
                            });
                        } else {
                            btnSafe->setButtonText(BM_TJ("settings.msg.rbXmlFailPrefix") + juce::String(sum.error));
                        }
                    });
                }).detach();
            });
    };

    y += 38;

    auto* exportBtn = makeButton(page, BM_TJ("settings.btn.exportMasterDb"),
                                 24, y, 240, 30, juce::Colour(0xFF06B6D4));
    exportBtn->onClick = [exportBtn] {
        juce::Component::SafePointer<juce::TextButton> btnSafe(exportBtn);
        std::thread([btnSafe]() {
#ifdef _WIN32
            auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                               .getParentDirectory().getChildFile("Roaming");
#else
            auto appData = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                               .getChildFile("Library");
#endif
            juce::File src = appData.getChildFile("Pioneer")
                                    .getChildFile("rekordbox")
                                    .getChildFile("master.db");
            if (!src.existsAsFile()) {
                src = appData.getChildFile("Pioneer")
                             .getChildFile("rekordbox6")
                             .getChildFile("master.db");
            }
            if (!src.existsAsFile()) {
                juce::MessageManager::callAsync([btnSafe] {
                    if (!btnSafe) return;
                    btnSafe->setButtonText(BM_TJ("settings.msg.masterDbNotFound"));
                });
                return;
            }

            auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("beatmate_export_master.db");
            tmp.deleteFile();
            src.copyFileTo(tmp);
            for (auto* ext : {"-wal", "-shm"}) {
                juce::File s(src.getFullPathName() + ext);
                if (s.existsAsFile())
                    s.copyFileTo(juce::File(tmp.getFullPathName() + ext));
            }

            sqlite3* db = nullptr;
            int rc = Services::Rekordbox::RekordboxCipher::openEncrypted(
                tmp.getFullPathName().toStdString(),
                "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497",
                &db);
            if (rc != SQLITE_OK || !db) {
                if (db) sqlite3_close(db);
                juce::MessageManager::callAsync([btnSafe] {
                    if (!btnSafe) return;
                    btnSafe->setButtonText(BM_TJ("settings.msg.masterDbDecryptFail"));
                });
                return;
            }

            auto outDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                              .getChildFile("Downloads");
            outDir.createDirectory();
            auto outFile = outDir.getChildFile("master_decrypted.db");
            outFile.deleteFile();

            std::string sql =
                "ATTACH DATABASE '" + outFile.getFullPathName().toStdString() +
                "' AS plain KEY ''; "
                "SELECT sqlcipher_export('plain'); "
                "DETACH DATABASE plain;";
            char* err = nullptr;
            rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
            sqlite3_close(db);
            if (rc != SQLITE_OK) {
                std::string msg = err ? err : "?";
                if (err) sqlite3_free(err);
                juce::MessageManager::callAsync([btnSafe, msg] {
                    spdlog::error("[Settings] sqlcipher_export failed: {}", msg);
                    if (!btnSafe) return;
                    btnSafe->setButtonText(BM_TJ("settings.msg.masterDbExportFail"));
                });
                return;
            }

            spdlog::info("[Settings] master_decrypted.db written to {}",
                         outFile.getFullPathName().toStdString());
            juce::MessageManager::callAsync([btnSafe, outFile] {
                outFile.revealToUser();
                if (!btnSafe) return;
                btnSafe->setButtonText(BM_TJ("settings.msg.masterDbExportOk"));
                juce::Timer::callAfterDelay(5000, [btnSafe] {
                    if (!btnSafe) return;
                    btnSafe->setButtonText(BM_TJ("settings.btn.exportMasterDb"));
                });
            });
        }).detach();
    };

    return page;
}

juce::Component* SettingsView::createLicenceTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.licence"), 24, y, 300, 22, true);
    y += 30;

    auto* devLbl = new juce::Label("", BM_TJ("settings.label.developedBy"));
    devLbl->setFont(juce::Font(15.0f, juce::Font::bold));
    devLbl->setColour(juce::Label::textColourId, Colors::primary());
    devLbl->setBounds(24, y, 400, 22);
    page->addAndMakeVisible(devLbl);
    y += 32;

    makeLabel(page, BM_TJ("settings.label.licenceKey"), 24, y, 160, 24);
    m_licenceKeyEdit = std::make_unique<juce::TextEditor>();
    m_licenceKeyEdit->setColour(juce::TextEditor::backgroundColourId, Colors::bgLighter());
    m_licenceKeyEdit->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_licenceKeyEdit->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_licenceKeyEdit->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    m_licenceKeyEdit->setTextToShowWhenEmpty(BM_TJ("settings.msg.licenceKeyPlaceholder"), juce::Colours::grey);
    m_licenceKeyEdit->setBounds(190, y, 300, 28);
    page->addAndMakeVisible(*m_licenceKeyEdit);
    y += 38;

    m_activateBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.activateLicence"), 190, y, 160, 30, Colors::primary()));
    m_activateBtn->onClick = [this] { openLicenseActivationDialog(); };
    y += 44;

    makeLabel(page, BM_TJ("settings.section.licenceInfo"), 24, y, 300, 22, true);
    y += 30;

    m_licenceTypeLbl = std::make_unique<juce::Label>("", BM_TJ("settings.label.licenceType"));
    m_licenceTypeLbl->setFont(juce::Font(14.0f));
    m_licenceTypeLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_licenceTypeLbl->setBounds(24, y, 400, 22);
    page->addAndMakeVisible(*m_licenceTypeLbl);
    y += 26;

    m_activationDateLbl = std::make_unique<juce::Label>("", BM_TJ("settings.label.activationDash"));
    m_activationDateLbl->setFont(juce::Font(13.0f));
    m_activationDateLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_activationDateLbl->setBounds(24, y, 400, 22);
    page->addAndMakeVisible(*m_activationDateLbl);
    y += 26;

    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>();
            if (licSvc) {
                auto state = licSvc->getState();
                if (state == Services::Security::LicenseState::Licensed) {
                    m_licenceTypeLbl->setText(BM_TJ("settings.label.licenceTypeProf"), juce::dontSendNotification);
                } else if (state == Services::Security::LicenseState::Trial) {
                    int days = licSvc->trialDaysRemaining();
                    juce::String msg = BM_TJ("settings.label.licenceTypeTrialDays");
                    msg = msg.replace("{}", juce::String(days));
                    m_licenceTypeLbl->setText(msg, juce::dontSendNotification);
                } else if (state == Services::Security::LicenseState::TrialExpired) {
                    m_licenceTypeLbl->setText(BM_TJ("settings.label.licenceTypeExpired"), juce::dontSendNotification);
                    m_licenceTypeLbl->setColour(juce::Label::textColourId, Colors::error());
                }
            }
        }
    }

    m_expirationDateLbl = std::make_unique<juce::Label>("", BM_TJ("settings.label.expirationTrial"));
    m_expirationDateLbl->setFont(juce::Font(13.0f));
    m_expirationDateLbl->setColour(juce::Label::textColourId, Colors::textSecondary());
    m_expirationDateLbl->setBounds(24, y, 400, 22);
    page->addAndMakeVisible(*m_expirationDateLbl);
    y += 26;

    juce::String machineId = juce::SystemStats::getUniqueDeviceID();
    if (machineId.isEmpty())
        machineId = juce::String::toHexString(juce::SystemStats::getOperatingSystemName().hashCode64());
    m_machineIdLbl = std::make_unique<juce::Label>("", BM_TJ("settings.label.machineIdPrefix") + machineId);
    m_machineIdLbl->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    m_machineIdLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    m_machineIdLbl->setBounds(24, y, 500, 22);
    page->addAndMakeVisible(*m_machineIdLbl);
    y += 34;

    makeLabel(page, BM_TJ("settings.label.statusLabel"), 24, y, 80, 24);
    juce::String statusText = BM_TJ("settings.label.statusTrial");
    juce::Colour statusColor = Colors::warning();
    {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (g_serviceLocator) {
            auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>();
            if (licSvc) {
                auto state = licSvc->getState();
                if (state == Services::Security::LicenseState::Licensed) {
                    statusText = BM_TJ("settings.label.statusActive");
                    statusColor = Colors::success();
                } else if (state == Services::Security::LicenseState::Trial) {
                    statusText = BM_TJ("settings.label.statusTrialDaysRemainingPrefix")
                                 + juce::String(licSvc->trialDaysRemaining())
                                 + BM_TJ("settings.label.statusTrialDaysRemainingSuffix");
                    statusColor = Colors::warning();
                } else {
                    statusText = BM_TJ("settings.label.statusExpired");
                    statusColor = Colors::error();
                }
            }
        }
    }
    m_licenceStatusLbl = std::make_unique<juce::Label>("", statusText);
    m_licenceStatusLbl->setFont(juce::Font(14.0f, juce::Font::bold));
    m_licenceStatusLbl->setColour(juce::Label::textColourId, statusColor);
    m_licenceStatusLbl->setBounds(110, y, 200, 24);
    page->addAndMakeVisible(*m_licenceStatusLbl);
    y += 40;

    m_buyBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.buyLicence"), 24, y, 200, 32, Colors::secondary()));
    m_buyBtn->onClick = [] {
        juce::URL purchase("https://beatmate.fr/tarifs/");
        if (purchase.isWellFormed() && ! purchase.isEmpty())
            purchase.launchInDefaultBrowser();
    };

    y += 38;
    auto* viewPlansBtn = makeButton(page, BM_TJ("settings.btn.viewPlans"), 24, y, 280, 32, Colors::accent());
    viewPlansBtn->onClick = [] {
        juce::URL plans("https://beatmate.fr/tarifs/");
        if (plans.isWellFormed() && ! plans.isEmpty())
            plans.launchInDefaultBrowser();
    };

    y += 44;
    auto* licInfo = new juce::Label("", BM_TJ("settings.msg.licencePlansInfo"));
    licInfo->setFont(juce::Font(12.0f));
    licInfo->setColour(juce::Label::textColourId, Colors::textMuted());
    licInfo->setJustificationType(juce::Justification::topLeft);
    licInfo->setBounds(24, y, 500, 60);
    page->addAndMakeVisible(licInfo);
    y += 70;

    return page;
}

juce::Component* SettingsView::createBackupTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.backup"), 24, y, 300, 22, true);
    y += 36;

    m_autoBackupCheck = std::unique_ptr<juce::ToggleButton>(
        makeToggle(page, BM_TJ("settings.btn.autoBackup"), 24, y, 220, 24, true));
    m_backupIntervalCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 260, y, 200, 26));
    m_backupIntervalCombo->addItem(BM_TJ("settings.combo.backup.hourly"), 1);
    m_backupIntervalCombo->addItem(BM_TJ("settings.combo.backup.daily"), 2);
    m_backupIntervalCombo->addItem(BM_TJ("settings.combo.backup.weekly"), 3);
    m_backupIntervalCombo->setSelectedId(2);
    y += 38;

    makeLabel(page, BM_TJ("settings.label.maxBackups"), 24, y, 250, 24);
    m_maxBackupsSpin = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
    m_maxBackupsSpin->setRange(1, 50, 1);
    m_maxBackupsSpin->setValue(10);
    m_maxBackupsSpin->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_maxBackupsSpin->setColour(juce::Slider::textBoxTextColourId, Colors::textPrimary());
    m_maxBackupsSpin->setColour(juce::Slider::textBoxBackgroundColourId, Colors::bgLighter());
    m_maxBackupsSpin->setBounds(280, y, 150, 26);
    page->addAndMakeVisible(*m_maxBackupsSpin);
    y += 38;

    m_backupNowBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.createBackup"), 24, y, 220, 30, Colors::primary()));
    m_backupNowBtn->onClick = [this] {
        auto backupDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate/backups");
        backupDir.createDirectory();

        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        auto backupName = "backup_" + timestamp;

        auto settingsFile = getSettingsFile();
        if (settingsFile.existsAsFile())
        {
            auto backupFile = backupDir.getChildFile(backupName + ".json");
            settingsFile.copyFileTo(backupFile);
        }

        m_backupsModel.backupNames.insert(0, backupName);

        int maxBackups = m_maxBackupsSpin ? (int)m_maxBackupsSpin->getValue() : 10;
        while (m_backupsModel.backupNames.size() > maxBackups)
        {
            auto oldest = m_backupsModel.backupNames[m_backupsModel.backupNames.size() - 1];
            backupDir.getChildFile(oldest + ".json").deleteFile();
            m_backupsModel.backupNames.remove(m_backupsModel.backupNames.size() - 1);
        }

        if (m_backupsList)
            m_backupsList->updateContent();

        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            BM_TJ("settings.msg.backupTitle"),
            BM_TJ("settings.msg.backupCreatedPrefix") + backupName);
    };

    m_restoreBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.restoreBackup"), 260, y, 200, 30, Colors::warning()));
    m_restoreBtn->onClick = [this] {
        if (m_backupsList && m_backupsList->getSelectedRow() >= 0) {
            auto name = m_backupsModel.backupNames[m_backupsList->getSelectedRow()];
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                BM_TJ("settings.msg.restoreTitle"),
                BM_TJ("settings.msg.restoreQuestionPrefix") + name + BM_TJ("settings.msg.restoreQuestionSuffix"),
                BM_TJ("settings.msg.restoreBtn"), BM_TJ("settings.msg.cancelBtn"), nullptr,
                juce::ModalCallbackFunction::create([this, name](int result) {
                    if (result == 1) {
                        auto backupDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("BeatMate/backups");
                        auto backupFile = backupDir.getChildFile(name + ".json");
                        if (backupFile.existsAsFile())
                        {
                            backupFile.copyFileTo(getSettingsFile());
                            loadSettingsFromJSON();
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                BM_TJ("settings.msg.restoreResult"), BM_TJ("settings.msg.restoreDone"));
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                BM_TJ("settings.msg.restoreResult"), BM_TJ("settings.msg.restoreMissing"));
                        }
                    }
                }));
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                BM_TJ("settings.msg.restoreTitle"), BM_TJ("settings.msg.restorePrompt"));
        }
    };
    y += 44;

    makeLabel(page, BM_TJ("settings.label.existingBackups"), 24, y, 200, 22);
    y += 26;

    auto backupDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate/backups");
    if (backupDir.exists()) {
        auto files = backupDir.findChildFiles(juce::File::findFiles, false, "*.json");
        files.sort();
        for (int i = files.size() - 1; i >= 0; --i)
            m_backupsModel.backupNames.add(files[i].getFileNameWithoutExtension());
    }

    m_backupsList = std::make_unique<juce::ListBox>("backups", &m_backupsModel);
    m_backupsList->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_backupsList->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_backupsList->setRowHeight(24);
    m_backupsList->setBounds(24, y, 500, 180);
    page->addAndMakeVisible(*m_backupsList);

    return page;
}

juce::Component* SettingsView::createShortcutsTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.shortcuts"), 24, y, 300, 22, true);
    y += 36;

    m_shortcutsModel.shortcuts = {
        { BM_TJ("settings.shortcut.import"),            "Ctrl+I" },
        { BM_TJ("settings.shortcut.analyzeSelection"),  "Ctrl+A" },
        { BM_TJ("settings.shortcut.search"),            "Ctrl+F" },
        { BM_TJ("settings.shortcut.save"),              "Ctrl+S" },
        { BM_TJ("settings.shortcut.saveAs"),            "Ctrl+Shift+S" },
        { BM_TJ("settings.shortcut.export"),            "Ctrl+E" },
        { BM_TJ("settings.shortcut.liveSuggest"),       "Ctrl+L" },
        { BM_TJ("settings.shortcut.newSet"),            "Ctrl+N" },
        { BM_TJ("settings.shortcut.open"),              "Ctrl+O" },
        { BM_TJ("settings.shortcut.undo"),              "Ctrl+Z" },
        { BM_TJ("settings.shortcut.redo"),              "Ctrl+Shift+Z" },
        { BM_TJ("settings.shortcut.playPause"),         "Space" },
        { BM_TJ("settings.shortcut.help"),              "F1" },
        { BM_TJ("settings.shortcut.refresh"),           "F5" },
        { BM_TJ("settings.shortcut.fullscreen"),        "F11" },
        { BM_TJ("settings.shortcut.delete"),            "Delete" },
        { BM_TJ("settings.shortcut.forward10"),         "Ctrl+Right" },
        { BM_TJ("settings.shortcut.back10"),            "Ctrl+Left" },
        { BM_TJ("settings.shortcut.volUp"),             "Ctrl+Up" },
        { BM_TJ("settings.shortcut.volDown"),           "Ctrl+Down" },
        { BM_TJ("settings.shortcut.mute"),              "M" },
        { BM_TJ("settings.shortcut.settings"),          "Ctrl+," },
        { BM_TJ("settings.shortcut.quit"),              "Ctrl+Q" },
        { BM_TJ("settings.shortcut.zoomIn"),            "Ctrl+Plus" },
        { BM_TJ("settings.shortcut.zoomOut"),           "Ctrl+Minus" },
        { BM_TJ("settings.shortcut.hotCuesAuto"),       "Ctrl+B" },
        { BM_TJ("settings.shortcut.analyzeAll"),        "Ctrl+Shift+A" },
    };

    m_shortcutsTable = std::make_unique<juce::TableListBox>("shortcuts", &m_shortcutsModel);
    m_shortcutsTable->setColour(juce::ListBox::backgroundColourId, Colors::bgDark());
    m_shortcutsTable->setColour(juce::ListBox::outlineColourId, Colors::border());
    m_shortcutsTable->setRowHeight(26);
    m_shortcutsTable->getHeader().addColumn(BM_TJ("settings.shortcut.colAction"), 1, 320, 200, 500);
    m_shortcutsTable->getHeader().addColumn(BM_TJ("settings.shortcut.colKey"), 2, 180, 100, 300);
    m_shortcutsTable->getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, Colors::bgLighter());
    m_shortcutsTable->getHeader().setColour(juce::TableHeaderComponent::textColourId, Colors::textPrimary());
    m_shortcutsTable->setBounds(24, y, 520, 340);
    page->addAndMakeVisible(*m_shortcutsTable);

    y += 350;

    m_resetShortcutsBtn = std::unique_ptr<juce::TextButton>(
        makeButton(page, BM_TJ("settings.btn.resetShortcuts"), 24, y, 200, 30, Colors::bgLighter()));
    m_resetShortcutsBtn->onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            BM_TJ("settings.msg.shortcutsResetTitle"), BM_TJ("settings.msg.shortcutsResetPrompt"),
            BM_TJ("settings.msg.shortcutsResetBtn"), BM_TJ("settings.msg.cancelBtn"), nullptr,
            juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1 && m_shortcutsTable) {
                    spdlog::info("[Settings] Shortcuts reset to defaults");
                    m_shortcutsTable->updateContent();
                }
            }));
    };

    return page;
}

juce::Component* SettingsView::createAppearanceTab()
{
    auto* page = new juce::Component();
    int y = 20;

    makeLabel(page, BM_TJ("settings.section.appearance"), 24, y, 300, 22, true);
    y += 36;

    makeLabel(page, BM_TJ("settings.label.accentColor"), 24, y, 200, 24);
    m_accentColorCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 220, 26));
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.blue"), 1);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.purple"), 2);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.green"), 3);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.orange"), 4);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.red"), 5);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.pink"), 6);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.cyan"), 7);
    m_accentColorCombo->addItem(BM_TJ("settings.combo.accent.gold"), 8);
    m_accentColorCombo->setSelectedId(1);
    y += 38;

    makeLabel(page, BM_TJ("settings.label.fontSize"), 24, y, 200, 24);
    m_fontSizeSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    m_fontSizeSlider->setRange(10, 20, 1);
    m_fontSizeSlider->setValue(13);
    m_fontSizeSlider->setColour(juce::Slider::thumbColourId, Colors::primary());
    m_fontSizeSlider->setColour(juce::Slider::trackColourId, Colors::bgLighter());
    m_fontSizeSlider->setTextValueSuffix(" pt");
    m_fontSizeSlider->setBounds(240, y, 260, 26);
    page->addAndMakeVisible(*m_fontSizeSlider);
    y += 38;

    makeLabel(page, BM_TJ("settings.label.interfaceDensity"), 24, y, 200, 24);
    m_densityCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 220, 26));
    m_densityCombo->addItem(BM_TJ("settings.combo.density.compact"), 1);
    m_densityCombo->addItem(BM_TJ("settings.combo.density.normal"), 2);
    m_densityCombo->addItem(BM_TJ("settings.combo.density.spacious"), 3);
    m_densityCombo->setSelectedId(2);
    y += 38;

    makeLabel(page, BM_TJ("settings.label.waveformStyle"), 24, y, 200, 24);
    m_waveformStyleCombo = std::unique_ptr<juce::ComboBox>(makeCombo(page, 240, y, 220, 26));
    m_waveformStyleCombo->addItem(BM_TJ("settings.combo.wave.rgb"), 1);
    m_waveformStyleCombo->addItem(BM_TJ("settings.combo.wave.mono"), 2);
    m_waveformStyleCombo->addItem(BM_TJ("settings.combo.wave.blue"), 3);
    m_waveformStyleCombo->addItem(BM_TJ("settings.combo.wave.green"), 4);
    m_waveformStyleCombo->setSelectedId(1);
    y += 50;

    makeLabel(page, BM_TJ("settings.section.preview"), 24, y, 200, 22, true);
    y += 28;

    auto* previewLbl = new juce::Label("", BM_TJ("settings.msg.previewText"));
    previewLbl->setFont(juce::Font(13.0f));
    previewLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    previewLbl->setJustificationType(juce::Justification::topLeft);
    previewLbl->setBounds(24, y, 400, 40);
    page->addAndMakeVisible(previewLbl);

    y += 60;

    auto* creditLbl = new juce::Label("", BM_TJ("settings.msg.credit"));
    creditLbl->setFont(juce::Font(11.0f));
    creditLbl->setColour(juce::Label::textColourId, Colors::textMuted());
    creditLbl->setBounds(24, y, 400, 18);
    page->addAndMakeVisible(creditLbl);

    return page;
}


void SettingsView::applySettings()
{

    // Debounce 500 ms : un callback AudioDeviceManager re-declenche applySettings
    static thread_local bool   s_applyingSettings = false;
    static thread_local int64_t s_lastApplyMs      = 0;
    // Le premier applySettings() a lieu au boot : ne pas rouvrir le device audio
    static thread_local bool   s_firstApply       = true;
    const bool isStartupApply = s_firstApply;
    s_firstApply = false;
    if (s_applyingSettings) return;
    const int64_t nowMs = juce::Time::getMillisecondCounter();
    if (s_lastApplyMs != 0 && (nowMs - s_lastApplyMs) < 500) return;
    s_lastApplyMs = nowMs;

    struct Guard {
        bool& flag;
        Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard{ s_applyingSettings };

    saveSettingsToJSON();

    extern BeatMate::ServiceLocator* g_serviceLocator;

    if (g_serviceLocator) {
        if (auto* engine = g_serviceLocator->tryGet<Core::AudioEngine>(); engine && !isStartupApply) {
            if (m_audioDeviceCombo && m_audioDeviceCombo->getSelectedId() > 0) {
                int deviceId = m_audioDeviceCombo->getSelectedId() - 1;
                engine->setOutputDevice(deviceId);
                spdlog::info("[Settings] Audio output device set to id={}", deviceId);
            }
            if (m_audioInputCombo && m_audioInputCombo->getSelectedId() > 0) {
                int inputId = m_audioInputCombo->getSelectedId() - 1;
                engine->setInputDevice(inputId);
                spdlog::info("[Settings] Audio input device set to id={}", inputId);
            }
            if (m_bufferSizeCombo && m_sampleRateCombo) {
                static const int bufferSizes[] = { 64, 128, 256, 512, 1024, 2048, 4096 };
                static const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
                int bufIdx = juce::jlimit(0, 6, m_bufferSizeCombo->getSelectedId() - 1);
                int srIdx  = juce::jlimit(0, 3, m_sampleRateCombo->getSelectedId() - 1);
                int newBuffer = bufferSizes[bufIdx];
                double newSR = sampleRates[srIdx];
                if (static_cast<int>(engine->getBufferSize()) != newBuffer ||
                    engine->getSampleRate() != newSR) {
                    engine->stop();
                    engine->initialize(newSR, static_cast<unsigned long>(newBuffer));
                    engine->start();
                    spdlog::info("[Settings] Audio engine reinitialized: SR={:.0f} Buffer={}", newSR, newBuffer);
                }
            }
        }
    }

    {
        if (m_startupCombo) {
            int startupMode = m_startupCombo->getSelectedId();
            if (auto* topLevel = getTopLevelComponent()) {
                if (auto* docWin = dynamic_cast<juce::DocumentWindow*>(topLevel)) {
                    if (startupMode == 1) { // Plein écran
                        juce::Desktop::getInstance().setKioskModeComponent(topLevel);
                    } else if (startupMode == 2 || startupMode == 3) {
                        juce::Desktop::getInstance().setKioskModeComponent(nullptr);
                    }
                }
            }
            spdlog::info("[Settings] Startup mode: {}", startupMode);
        }

        if (m_languageCombo) {
            int langId = m_languageCombo->getSelectedId();
            const std::string code = (langId == 2) ? "en" : "fr";
            if (g_serviceLocator) {
                if (auto* svc = g_serviceLocator->tryGet<Services::Config::LocalizationService>()) {
                    svc->setLanguage(code);
                }
            }
            if (auto* topLevel = getTopLevelComponent()) {
                if (auto* docWin = dynamic_cast<juce::DocumentWindow*>(topLevel)) {
                    docWin->setName(langId == 2 ? "BeatMate V12 Professional"
                                                : "BeatMate V12 Professionnel");
                }
            }
            spdlog::info("[Settings] Language: {}", code);
        }
    }

    if (g_serviceLocator) {
        if (auto* scanner = g_serviceLocator->tryGet<Services::Library::TrackScanner>()) {
            scanner->unwatchAll();
            for (const auto& folder : m_watchFolders) {
                scanner->watchFolder(folder.toStdString(), true);
            }
            spdlog::info("[Settings] {} watch folders configured", m_watchFolders.size());

            if (m_autoImportCheck && m_autoImportCheck->getToggleState()) {
                scanner->setOnFileAdded(Services::Library::makeAutoImportHandler());
                spdlog::info("[Settings] Auto-import: enabled");
            } else {
                scanner->setOnFileAdded(nullptr);
                spdlog::info("[Settings] Auto-import: disabled");
            }
        }

        if (m_detectDuplicatesCheck) {
            bool detectDupes = m_detectDuplicatesCheck->getToggleState();
            if (auto* dupeDetector = g_serviceLocator->tryGet<Services::Library::DuplicateDetector>()) {
                dupeDetector->setEnabled(detectDupes);
                spdlog::info("[Settings] Duplicate detection: {}", detectDupes ? "enabled" : "disabled");
            }
        }
    }

    if (g_serviceLocator) {
        if (auto* pipeline = g_serviceLocator->tryGet<Core::AudioAnalysisPipeline>()) {
            if (m_generateWaveformCheck)
                pipeline->setGenerateWaveform(m_generateWaveformCheck->getToggleState());
            if (m_enableStemsCheck)
                pipeline->setAnalyzeStructure(m_enableStemsCheck->getToggleState());
            if (m_analysisQualityCombo) {
                int quality = m_analysisQualityCombo->getSelectedId();
                pipeline->setAnalyzeBPM(true);
                pipeline->setAnalyzeKey(true);
                pipeline->setAnalyzeEnergy(true);
                pipeline->setAnalyzeLoudness(quality >= 2); // Standard+
                pipeline->setAnalyzeStructure(quality >= 3 || (m_enableStemsCheck && m_enableStemsCheck->getToggleState()));
                spdlog::info("[Settings] Analysis quality: {}", quality);
            }
        }

        if (m_bpmMinSlider && m_bpmMaxSlider) {
            double bpmMin = m_bpmMinSlider->getValue();
            double bpmMax = m_bpmMaxSlider->getValue();
            if (auto* settingsMgr = g_serviceLocator->tryGet<Services::Config::SettingsManager>()) {
                settingsMgr->set("analysis.bpmMin", bpmMin);
                settingsMgr->set("analysis.bpmMax", bpmMax);
            }
            spdlog::info("[Settings] BPM range: {:.0f} - {:.0f}", bpmMin, bpmMax);
        }

        if (m_bpmModeCombo) {
            spdlog::info("[Settings] BPM mode: {}", m_bpmModeCombo->getSelectedId() == 1 ? "Normal" : "Dynamic");
        }

        if (m_keyMethodCombo) {
            int keyMethod = m_keyMethodCombo->getSelectedId();
            spdlog::info("[Settings] Key notation: {}", keyMethod == 1 ? "Camelot" : keyMethod == 2 ? "Open Key" : "Standard");
        }

        if (m_analysisThreadsCombo) {
            static const int threadCounts[] = { 1, 2, 4, 8 };
            int tIdx = juce::jlimit(0, 3, m_analysisThreadsCombo->getSelectedId() - 1);
            spdlog::info("[Settings] Analysis threads: {}", threadCounts[tIdx]);
        }

        spdlog::info("[Settings] Analysis: waveform={}, stems={}",
            m_generateWaveformCheck ? m_generateWaveformCheck->getToggleState() : true,
            m_enableStemsCheck ? m_enableStemsCheck->getToggleState() : false);
    }

    if (g_serviceLocator) {
        if (auto* backup = g_serviceLocator->tryGet<Services::Config::BackupService>()) {
            if (m_maxBackupsSpin)
                backup->setMaxBackups(static_cast<int>(m_maxBackupsSpin->getValue()));
            if (m_autoBackupCheck && m_autoBackupCheck->getToggleState()) {
                // Combo IDs: 1=hourly, 2=daily, 3=weekly  -> minutes
                static const int intervals[] = { 60, 1440, 10080 };
                int intIdx = m_backupIntervalCombo ? juce::jlimit(0, 2, m_backupIntervalCombo->getSelectedId() - 1) : 1;
                backup->startAutoBackup(intervals[intIdx]);
                spdlog::info("[Settings] Auto-backup enabled: interval={}min, max={}",
                    intervals[intIdx], m_maxBackupsSpin ? (int)m_maxBackupsSpin->getValue() : 10);
            }
        }
    }

    auto& theme = ThemeEngine::instance();
    auto& lf = juce::Desktop::getInstance().getDefaultLookAndFeel();

    if (m_fontSizeSlider) {
        float fontSize = static_cast<float>(m_fontSizeSlider->getValue());
        lf.setDefaultSansSerifTypefaceName("Segoe UI");
        theme.setColor("font.size", juce::Colour::fromRGBA(
            static_cast<uint8_t>(fontSize), 0, 0, 255));
        spdlog::info("[Settings] Font size: {:.0f}", fontSize);
    }

    if (m_accentColorCombo) {
        int colorId = m_accentColorCombo->getSelectedId();
        static const juce::uint32 accentColors[] = {
            0xFF3B82F6, 0xFF8B5CF6, 0xFF10B981, 0xFFEF4444,
            0xFFF59E0B, 0xFFEC4899, 0xFF06B6D4, 0xFFF1F5F9
        };
        int idx = juce::jlimit(0, 7, colorId - 1);
        juce::uint32 newArgb = accentColors[idx];
        juce::Colour newAccent(newArgb);

        Colors::Dynamic::setAccentColor(newArgb);

        theme.setColor("primary", newAccent);
        theme.setColor("primary.hover", newAccent.brighter(0.2f));
        theme.setColor("accent", newAccent.withAlpha(0.85f));
        lf.setColour(juce::TextButton::buttonColourId, newAccent);
        lf.setColour(juce::Slider::thumbColourId, newAccent);
        lf.setColour(juce::TextEditor::focusedOutlineColourId, newAccent);
        lf.setColour(juce::ComboBox::focusedOutlineColourId, newAccent);
        lf.setColour(juce::PopupMenu::highlightedBackgroundColourId, newAccent);
        lf.setColour(juce::ScrollBar::thumbColourId, newAccent.withAlpha(0.5f));
        lf.setColour(juce::ToggleButton::tickColourId, newAccent);
        spdlog::info("[Settings] Accent color applied globally: id={} argb=0x{:08X}", colorId, newArgb);
    }

    if (m_densityCombo) {
        int densityId = m_densityCombo->getSelectedId();
        // 1=Compact(20px), 2=Normal(28px), 3=Spacieux(36px)
        static const int rowHeights[] = { 20, 28, 36 };
        int rowH = rowHeights[juce::jlimit(0, 2, densityId - 1)];
        theme.setColor("density.id", juce::Colour::fromRGBA(
            static_cast<uint8_t>(densityId), 0, 0, 255));

        if (auto* topLevel = getTopLevelComponent()) {
            std::function<void(juce::Component*)> applyDensity = [&](juce::Component* c) {
                if (!c) return;
                if (auto* listBox = dynamic_cast<juce::ListBox*>(c))
                    listBox->setRowHeight(rowH);
                if (auto* table = dynamic_cast<juce::TableListBox*>(c))
                    table->setRowHeight(rowH);
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                    applyDensity(c->getChildComponent(i));
            };
            applyDensity(topLevel);
        }
        spdlog::info("[Settings] Density: {} (row height={}px)", densityId, rowH);
    }

    if (m_waveformStyleCombo) {
        int styleId = m_waveformStyleCombo->getSelectedId();
        theme.setColor("waveform.style", juce::Colour::fromRGBA(
            static_cast<uint8_t>(styleId), 0, 0, 255));
        spdlog::info("[Settings] Waveform style: {}", styleId);
    }

    theme.applyTheme(lf);

    std::function<void(juce::Component*)> updateColorsRecursive = [&](juce::Component* comp) {
        if (!comp) return;
        juce::Colour accent = Colors::primary();

        if (auto* btn = dynamic_cast<juce::TextButton*>(comp)) {
            auto currentBg = btn->findColour(juce::TextButton::buttonColourId);
            bool isErrorBtn = (currentBg.getRed() > 200 && currentBg.getGreen() < 100 && currentBg.getBlue() < 100);
            bool isWarningBtn = (currentBg.getRed() > 200 && currentBg.getGreen() > 150 && currentBg.getBlue() < 50);
            bool isGrayBtn = (currentBg.getRed() == currentBg.getGreen() && currentBg.getGreen() == currentBg.getBlue()
                              && currentBg.getRed() < 80);
            if (!isErrorBtn && !isWarningBtn && !isGrayBtn && currentBg.getAlpha() > 100) {
                btn->setColour(juce::TextButton::buttonColourId, accent);
            }
            btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        }
        if (auto* slider = dynamic_cast<juce::Slider*>(comp)) {
            slider->setColour(juce::Slider::thumbColourId, accent);
        }
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(comp)) {
            toggle->setColour(juce::ToggleButton::tickColourId, accent);
        }
        if (auto* editor = dynamic_cast<juce::TextEditor*>(comp)) {
            editor->setColour(juce::TextEditor::focusedOutlineColourId, accent);
        }
        if (auto* combo = dynamic_cast<juce::ComboBox*>(comp)) {
            combo->setColour(juce::ComboBox::focusedOutlineColourId, accent);
        }
        if (auto* label = dynamic_cast<juce::Label*>(comp)) {
            auto currentCol = label->findColour(juce::Label::textColourId);
            if (currentCol == juce::Colour(0xFF3B82F6) || // default blue
                currentCol == juce::Colour(0xFF8B5CF6) || // violet
                currentCol == juce::Colour(0xFF10B981) || // green
                currentCol == juce::Colour(0xFFEF4444) || // red (skip error labels)
                currentCol == juce::Colour(0xFFF59E0B) || // orange
                currentCol == juce::Colour(0xFFEC4899) || // pink
                currentCol == juce::Colour(0xFF06B6D4))   // cyan
            {
                label->setColour(juce::Label::textColourId, accent);
            }
        }
        if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(comp)) {
            tabs->repaint();
        }
        if (auto* tabBar = dynamic_cast<juce::TabbedButtonBar*>(comp)) {
            tabBar->repaint();
        }

        comp->repaint();

        for (int i = 0; i < comp->getNumChildComponents(); ++i)
            updateColorsRecursive(comp->getChildComponent(i));
    };

    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        updateColorsRecursive(juce::Desktop::getInstance().getComponent(i));

    if (auto* topLevel = getTopLevelComponent()) {
        topLevel->repaint();
        std::function<void(juce::Component*)> forceRepaintAll = [&](juce::Component* c) {
            if (!c) return;
            c->repaint();
            for (int i = 0; i < c->getNumChildComponents(); ++i)
                forceRepaintAll(c->getChildComponent(i));
        };
        forceRepaintAll(topLevel);
    }

    if (g_serviceLocator) {
        if (auto* syncSvc = g_serviceLocator->tryGet<Services::DJSoftware::CollectionSyncService>()) {
            if (m_autoSyncDJCheck && m_autoSyncDJCheck->getToggleState()) {
                // Combo IDs: 1=hourly, 2=6h, 3=daily, 4=weekly  -> seconds
                static const int syncIntervals[] = { 3600, 21600, 86400, 604800 };
                int sIdx = m_syncIntervalCombo ? juce::jlimit(0, 3, m_syncIntervalCombo->getSelectedId() - 1) : 2;
                syncSvc->stop();
                syncSvc->start(syncIntervals[sIdx]); // seconds
                spdlog::info("[Settings] DJ Sync enabled: interval={}s", syncIntervals[sIdx]);
            } else if (m_autoSyncDJCheck) {
                syncSvc->stop();
                spdlog::info("[Settings] DJ Sync disabled");
            }
        }
    }

    m_listeners.call(&Listener::settingsApplied);

    spdlog::info("[Settings] All settings applied successfully");
}

void SettingsView::cancelChanges()
{
    loadSettingsFromJSON();

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}

void SettingsView::resetDefaults()
{
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
        BM_TJ("settings.msg.resetDialogTitle"), BM_TJ("settings.msg.resetDialogPrompt"),
        BM_TJ("settings.msg.resetDialogBtn"), BM_TJ("settings.msg.cancelBtn"), nullptr,
        juce::ModalCallbackFunction::create([this](int result) {
            if (result == 1)
            {
                if (m_languageCombo)        m_languageCombo->setSelectedId(1);
                if (m_themeCombo)           m_themeCombo->setSelectedId(1);
                if (m_startupCombo)         m_startupCombo->setSelectedId(2);
                if (m_autoSaveCheck)        m_autoSaveCheck->setToggleState(true, juce::dontSendNotification);
                if (m_autoSaveIntervalCombo) m_autoSaveIntervalCombo->setSelectedId(2);
                if (m_checkUpdatesCheck)    m_checkUpdatesCheck->setToggleState(true, juce::dontSendNotification);

                if (m_audioDeviceCombo)     m_audioDeviceCombo->setSelectedId(1);
                if (m_audioInputCombo)      m_audioInputCombo->setSelectedId(1);
                if (m_sampleRateCombo)      m_sampleRateCombo->setSelectedId(1);
                if (m_bufferSizeCombo)      m_bufferSizeCombo->setSelectedId(4);

                if (m_analysisQualityCombo) m_analysisQualityCombo->setSelectedId(2);
                if (m_bpmMinSlider)         m_bpmMinSlider->setValue(70);
                if (m_bpmMaxSlider)         m_bpmMaxSlider->setValue(180);
                if (m_bpmModeCombo)         m_bpmModeCombo->setSelectedId(1);
                if (m_keyMethodCombo)       m_keyMethodCombo->setSelectedId(1);
                if (m_autoAnalyzeCheck)     m_autoAnalyzeCheck->setToggleState(true, juce::dontSendNotification);
                if (m_analysisThreadsCombo) m_analysisThreadsCombo->setSelectedId(2);
                if (m_enableStemsCheck)     m_enableStemsCheck->setToggleState(false, juce::dontSendNotification);
                if (m_generateWaveformCheck) m_generateWaveformCheck->setToggleState(true, juce::dontSendNotification);

                if (m_autoImportCheck)      m_autoImportCheck->setToggleState(true, juce::dontSendNotification);
                if (m_analyzeOnImportCheck)  m_analyzeOnImportCheck->setToggleState(true, juce::dontSendNotification);
                if (m_detectDuplicatesCheck) m_detectDuplicatesCheck->setToggleState(true, juce::dontSendNotification);

                if (m_autoSyncDJCheck)      m_autoSyncDJCheck->setToggleState(false, juce::dontSendNotification);
                if (m_syncIntervalCombo)    m_syncIntervalCombo->setSelectedId(3);

                if (m_autoBackupCheck)      m_autoBackupCheck->setToggleState(true, juce::dontSendNotification);
                if (m_backupIntervalCombo)  m_backupIntervalCombo->setSelectedId(2);
                if (m_maxBackupsSpin)       m_maxBackupsSpin->setValue(10);

                if (m_accentColorCombo)     m_accentColorCombo->setSelectedId(1);
                if (m_fontSizeSlider)       m_fontSizeSlider->setValue(13);
                if (m_densityCombo)         m_densityCombo->setSelectedId(2);
                if (m_waveformStyleCombo)   m_waveformStyleCombo->setSelectedId(1);

                saveSettingsToJSON();
            }
        }));
}


void SettingsView::paint(juce::Graphics& g)
{
    ProDraw::viewBackground(g, getWidth(), getHeight());

    auto headerArea = getLocalBounds().removeFromTop(60).toFloat();
    g.setGradientFill(juce::ColourGradient(
        Colors::bgDark(), 0.0f, 0.0f,
        Colors::bgDarker(), 0.0f, headerArea.getBottom(), false));
    g.fillRect(headerArea);

    g.setColour(Colors::border());
    g.drawHorizontalLine(58, 24.0f, (float)(getWidth() - 24));
    g.drawHorizontalLine(getHeight() - 52, 24.0f, (float)(getWidth() - 24));
}

void SettingsView::resized()
{
    m_titleLabel->setBounds(24, 18, 300, 30);
    m_tabWidget->setBounds(24, 62, getWidth() - 48, getHeight() - 120);

    int bx = getWidth() - 340, by = getHeight() - 42;
    m_resetBtn->setBounds(bx, by, 100, 30);
    bx += 110;
    m_cancelBtn->setBounds(bx, by, 100, 30);
    bx += 110;
    m_applyBtn->setBounds(bx, by, 100, 30);
}

void SettingsView::openLicenseActivationDialog()
{
    extern BeatMate::ServiceLocator* g_serviceLocator;

    // Toggle behaviour: if a license is already active, this button deactivates first.
    if (g_serviceLocator) {
        auto* licSvc = g_serviceLocator->tryGet<Services::Security::LicenseService>();
        if (licSvc && licSvc->isActivated()) {
            // Desactivation serveur best-effort ; URL/cle resolues en interne (beatmate.fr + override %APPDATA%/BeatMate/wp.json), jamais saisies par l'utilisateur.
            {
                std::string wpUrl, wpApiKey;
                resolveWpCredentials(wpUrl, wpApiKey);
                if (!wpUrl.empty()) {
                    auto http   = std::make_shared<Services::Network::HttpClient>();
                    auto client = std::make_shared<Services::WordPress::WordPressLicenseClient>(
                        http, wpUrl, wpApiKey);
                    client->deactivate(licSvc->getKey(), [](Services::WordPress::DeactivationResult){});
                }
            }
            licSvc->forceLocalRevocation("user requested deactivation");
            if (auto* player = g_serviceLocator->tryGet<Core::AudioPlayer>()) {
                player->setMaxPlaybackSeconds(Services::Security::LicenseService::kTrialMaxPlaybackSeconds);
                player->resetTrialState();
            }
            m_licenceStatusLbl->setText(BM_TJ("settings.label.statusNotActivated"), juce::dontSendNotification);
            m_licenceStatusLbl->setColour(juce::Label::textColourId, Colors::error());
            m_licenceTypeLbl  ->setText(BM_TJ("settings.label.licenceType"), juce::dontSendNotification);
            m_activateBtn     ->setButtonText(BM_TJ("settings.btn.activateLicence"));
            if (m_licenceKeyEdit) {
                m_licenceKeyEdit->setReadOnly(false);
                m_licenceKeyEdit->clear();
            }
            saveSettingsToJSON();
            return;
        }
    }

    auto* content = new LicenseDialog();
    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle                   = BM_TJ("dialog.license.title");
    opts.dialogBackgroundColour        = Colors::bgDark();
    opts.escapeKeyTriggersCloseButton  = true;
    opts.useNativeTitleBar             = false;
    opts.resizable                     = false;
    opts.content.setOwned(content);

    content->onActivationRequestedFull = [content, this](const LicenseActivationRequest& req) {
        extern BeatMate::ServiceLocator* g_serviceLocator;
        if (!g_serviceLocator) {
            content->setStatus("Internal error: no service locator", Colors::error());
            content->setBusy(false);
            return;
        }
        auto* licSvc   = g_serviceLocator->tryGet<Services::Security::LicenseService>();
        if (!licSvc) {
            content->setStatus("Services indisponibles", Colors::error());
            content->setBusy(false);
            return;
        }
        std::string wpUrl, wpApiKey;
        resolveWpCredentials(wpUrl, wpApiKey);
        if (wpUrl.empty()) {
            content->setStatus(juce::String::fromUTF8(
                u8"Serveur de licence indisponible. Réessayez plus tard."),
                Colors::error());
            content->setBusy(false);
            return;
        }
        auto http   = std::make_shared<Services::Network::HttpClient>();
        auto client = std::make_shared<Services::WordPress::WordPressLicenseClient>(
            http, wpUrl, wpApiKey);

        auto machine = juce::SystemStats::getComputerName().toStdString();

        client->activate(req.key.toStdString(),
                         req.email.toStdString(),
                         req.prenom.toStdString(),
                         req.nom.toStdString(),
                         machine,
                         [content, licSvc, this, req](Services::WordPress::ActivationResult r) {
            // Hop back to the message thread for any UI/state mutation.
            juce::MessageManager::callAsync([content, licSvc, this, req, r]() {
                if (!r.success) {
                    juce::String msg = r.error.empty()
                        ? juce::String::fromUTF8(u8"Activation refusée par le serveur.")
                        : juce::String(r.error);
                    if (r.httpStatus == 429)
                        msg = juce::String::fromUTF8(u8"Trop de tentatives. Réessayez plus tard.");
                    else if (r.httpStatus == 409)
                        msg = juce::String::fromUTF8(u8"Licence déjà active sur une autre machine. Désactivez-la d'abord.");
                    else if (r.httpStatus == 403)
                        msg = juce::String::fromUTF8(u8"Licence révoquée — contactez le support.");
                    content->setStatus(msg, Colors::error());
                    content->setBusy(false);
                    return;
                }

                licSvc->activateFromServer(req.key.toStdString(),
                                           r.type,
                                           r.expiresAtEpoch,
                                           r.signature,
                                           req.email.toStdString());

                extern BeatMate::ServiceLocator* g_serviceLocator;
                if (g_serviceLocator) {
                    if (auto* player = g_serviceLocator->tryGet<Core::AudioPlayer>()) {
                        player->setMaxPlaybackSeconds(0.0);
                        player->resetTrialState();
                    }
                }

                content->setStatus(juce::String::fromUTF8(u8"Activation réussie."), Colors::success());
                if (m_licenceKeyEdit) { m_licenceKeyEdit->setText(req.key, false); m_licenceKeyEdit->setReadOnly(true); }
                if (m_licenceStatusLbl) {
                    m_licenceStatusLbl->setText(BM_TJ("settings.label.statusActive"), juce::dontSendNotification);
                    m_licenceStatusLbl->setColour(juce::Label::textColourId, Colors::success());
                }
                if (m_licenceTypeLbl)
                    m_licenceTypeLbl->setText(BM_TJ("settings.label.licenceTypePrefix") + juce::String(r.type),
                                               juce::dontSendNotification);
                if (m_activateBtn)
                    m_activateBtn->setButtonText(BM_TJ("settings.btn.changeLicence"));
                saveSettingsToJSON();

                juce::Timer::callAfterDelay(800, [content]() {
                    if (auto* dw = content->findParentComponentOfClass<juce::DialogWindow>())
                        dw->exitModalState(1);
                });
            });
        });
    };

    opts.launchAsync();
}

} // namespace BeatMate::UI
