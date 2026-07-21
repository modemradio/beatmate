#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace BeatMate::UI {

class SettingsView : public juce::Component,
                     public juce::ChangeListener {
public:
    SettingsView();
    ~SettingsView() override;

    void retranslateUi();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void settingsChanged() {}
        virtual void settingsApplied() {}
    };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

    void applySettings();
    void cancelChanges();
    void resetDefaults();

    void saveSettingsToJSON();
    void loadSettingsFromJSON();
    juce::File getSettingsFile() const;

private:
    void setupUI();

    juce::Component* createGeneralTab();
    juce::Component* createAudioTab();
    juce::Component* createLibraryTab();
    juce::Component* createAnalysisTab();
    juce::Component* createDJSoftwareTab();
    juce::Component* createLicenceTab();
    juce::Component* createBackupTab();
    juce::Component* createShortcutsTab();
    juce::Component* createAppearanceTab();

    void openLicenseActivationDialog();

    static juce::Label* makeLabel(juce::Component* parent, const juce::String& text,
                                   int x, int y, int w, int h, bool isHeader = false);
    static juce::ComboBox* makeCombo(juce::Component* parent, int x, int y, int w, int h);
    static juce::ToggleButton* makeToggle(juce::Component* parent, const juce::String& text,
                                           int x, int y, int w, int h, bool defaultOn = false);
    static juce::TextButton* makeButton(juce::Component* parent, const juce::String& text,
                                         int x, int y, int w, int h, juce::Colour bg);

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TabbedComponent> m_tabWidget;
    std::unique_ptr<juce::TextButton> m_applyBtn, m_cancelBtn, m_resetBtn;
public:
    void showLicenceTab() { if (m_tabWidget) m_tabWidget->setCurrentTabIndex(m_tabWidget->getNumTabs() - 1); }
private:

    std::unique_ptr<juce::ComboBox> m_languageCombo, m_themeCombo, m_startupCombo;
    std::unique_ptr<juce::ToggleButton> m_autoSaveCheck, m_checkUpdatesCheck;
    std::unique_ptr<juce::ComboBox> m_autoSaveIntervalCombo;
    std::unique_ptr<juce::Label> m_dataFolderLabel;
    std::unique_ptr<juce::TextButton> m_changeDataFolderBtn, m_resetSettingsBtn;

    std::unique_ptr<juce::TextButton> m_checkUpdateNowBtn, m_installLocalMsiBtn;
    std::unique_ptr<juce::Label>      m_updateStatusLbl;
    void runOnlineUpdateCheck();
    void runLocalMsiInstall();

    std::unique_ptr<juce::ComboBox> m_audioDeviceCombo, m_audioInputCombo;
    std::unique_ptr<juce::ComboBox> m_sampleRateCombo, m_bufferSizeCombo;
    std::unique_ptr<juce::Label> m_latencyLabel;
    std::unique_ptr<juce::TextButton> m_testAudioBtn;

    std::unique_ptr<juce::ListBox> m_watchFoldersList;
    std::unique_ptr<juce::TextButton> m_addFolderBtn, m_removeFolderBtn, m_cleanMissingBtn;
    std::unique_ptr<juce::ToggleButton> m_autoImportCheck, m_analyzeOnImportCheck, m_detectDuplicatesCheck;
    juce::StringArray m_watchFolders;

    std::unique_ptr<juce::ComboBox> m_analysisQualityCombo, m_keyMethodCombo, m_bpmModeCombo, m_analysisThreadsCombo;
    std::unique_ptr<juce::Slider> m_bpmMinSlider, m_bpmMaxSlider;
    std::unique_ptr<juce::ToggleButton> m_enableStemsCheck, m_generateWaveformCheck, m_autoAnalyzeCheck;

    struct DJSoftwareRow {
        std::unique_ptr<juce::Label> nameLbl;
        std::unique_ptr<juce::Label> statusLbl;
    };
    std::vector<DJSoftwareRow> m_djSoftwareRows;
    std::unique_ptr<juce::ToggleButton> m_autoSyncDJCheck;
    std::unique_ptr<juce::ComboBox> m_syncIntervalCombo;
    std::unique_ptr<juce::TextButton> m_scanDJBtn;
    std::unique_ptr<juce::TextButton> m_syncDJBtn;

    std::unique_ptr<juce::TextEditor> m_licenceKeyEdit;
    std::unique_ptr<juce::TextButton> m_activateBtn, m_buyBtn;
    std::unique_ptr<juce::Label> m_licenceTypeLbl, m_activationDateLbl, m_expirationDateLbl;
    std::unique_ptr<juce::Label> m_machineIdLbl, m_licenceStatusLbl;

    std::unique_ptr<juce::ToggleButton> m_autoBackupCheck;
    std::unique_ptr<juce::ComboBox> m_backupIntervalCombo;
    std::unique_ptr<juce::Slider> m_maxBackupsSpin;
    std::unique_ptr<juce::TextButton> m_backupNowBtn, m_restoreBtn;
    std::unique_ptr<juce::ListBox> m_backupsList;

    std::unique_ptr<juce::TableListBox> m_shortcutsTable;
    std::unique_ptr<juce::TextButton> m_resetShortcutsBtn;

    std::unique_ptr<juce::ComboBox> m_accentColorCombo, m_densityCombo, m_waveformStyleCombo;
    std::unique_ptr<juce::Slider> m_fontSizeSlider;

    class WatchFoldersListModel : public juce::ListBoxModel {
    public:
        juce::StringArray* folders = nullptr;
        int getNumRows() override { return folders ? folders->size() : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    };
    WatchFoldersListModel m_watchFoldersModel;

    class ShortcutsTableModel : public juce::TableListBoxModel {
    public:
        struct ShortcutEntry { juce::String action; juce::String key; };
        std::vector<ShortcutEntry> shortcuts;
        int getNumRows() override { return (int)shortcuts.size(); }
        void paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected) override;
        void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool selected) override;
    };
    ShortcutsTableModel m_shortcutsModel;

    class BackupsListModel : public juce::ListBoxModel {
    public:
        juce::StringArray backupNames;
        int getNumRows() override { return backupNames.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    };
    BackupsListModel m_backupsModel;

    juce::ListenerList<Listener> m_listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsView)
};

} // namespace BeatMate::UI
