#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace BeatMate::UI {

class FirstTimeSetupDialog : public juce::Component {
public:
    FirstTimeSetupDialog();
    ~FirstTimeSetupDialog() override = default;

    juce::String selectedLanguage() const;
    juce::StringArray musicFolders() const;
    juce::StringArray detectedDJSoftware() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static bool showDialog(juce::Component* parent);

private:
    void createPages();
    void showPage(int index);
    void saveWizardSettings();

    juce::Component* createWelcomePage();
    juce::Component* createMusicFoldersPage();
    juce::Component* createDJSoftwarePage();
    juce::Component* createAudioPage();
    juce::Component* createLicencePage();
    juce::Component* createSummaryPage();

    void updateSummary();
    void scanDJSoftware();

    int m_currentPage = 0;

    std::unique_ptr<juce::Label> m_titleLabel, m_subtitleLabel;

    std::unique_ptr<juce::TextButton> m_nextBtn, m_prevBtn;

    struct StepIndicator {
        std::unique_ptr<juce::Label> label;
        int pageIndex;
    };
    std::vector<StepIndicator> m_stepIndicators;

    // Raw pointers conservés pour applyLanguageChange()
    std::unique_ptr<juce::ComboBox> m_languageCombo;
    juce::Label* m_welcomeTitleLbl = nullptr;
    juce::Label* m_welcomeByLbl    = nullptr;
    juce::Label* m_welcomeTagLbl   = nullptr;
    juce::Label* m_welcomeFeatLbl  = nullptr;
    juce::Label* m_welcomeCtaLbl   = nullptr;
    juce::Label* m_welcomeLangLbl  = nullptr;
    void applyLanguageChange();

    std::unique_ptr<juce::ListBox> m_foldersList;
    std::unique_ptr<juce::TextButton> m_addFolderBtn, m_removeFolderBtn, m_addDefaultBtn;
    juce::StringArray m_musicFolders;
    class FoldersListModel : public juce::ListBoxModel {
    public:
        juce::StringArray* folders = nullptr;
        int getNumRows() override { return folders ? folders->size() : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    };
    FoldersListModel m_foldersModel;

    struct DJSoftwareEntry {
        juce::String name;
        bool detected = false;
        std::unique_ptr<juce::Label> nameLbl;
        std::unique_ptr<juce::Label> statusLbl;
    };
    std::vector<DJSoftwareEntry> m_djSoftwareEntries;
    std::unique_ptr<juce::TextButton> m_scanBtn;

    std::unique_ptr<juce::ComboBox> m_audioDeviceCombo, m_sampleRateCombo, m_bufferSizeCombo;
    std::unique_ptr<juce::Label> m_latencyLabel;

    std::unique_ptr<juce::TextEditor> m_licenseKeyEdit;
    std::unique_ptr<juce::TextButton> m_activateLicBtn;
    std::unique_ptr<juce::Label> m_licStatusLabel;

    std::unique_ptr<juce::TextEditor> m_summaryText;

    juce::OwnedArray<juce::Component> m_pages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FirstTimeSetupDialog)
};

} // namespace BeatMate::UI
