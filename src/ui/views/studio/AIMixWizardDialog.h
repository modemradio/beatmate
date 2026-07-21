#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

#include "../../../models/Track.h"
#include "../../../services/ai/AIMashupOrchestrator.h"

namespace BeatMate::UI::Studio {

class AIMixWizardDialog : public juce::Component {
public:
    using ResultCb = std::function<void(BeatMate::Services::AI::MashupResult)>;

    AIMixWizardDialog();
    ~AIMixWizardDialog() override;

    void setLibrary(std::vector<Models::Track> library);
    void setOnGenerated(ResultCb cb) { m_onGenerated = std::move(cb); }

    static void showDialog(std::vector<Models::Track> library,
                           ResultCb onGenerated);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class TypeCard;
    class TrackListModel;

    void buildNoviceUI();
    void buildExpertUI();
    void switchMode(BeatMate::Services::AI::SkillMode mode);
    void onGenerateClicked();
    void updateGenerateEnabled();
    void setStatusText(const juce::String& text);

    BeatMate::Services::AI::SkillMode  m_mode { BeatMate::Services::AI::SkillMode::Novice };
    BeatMate::Services::AI::MashupType m_selectedType { BeatMate::Services::AI::MashupType::Megamix };
    bool m_typeChosen { false };

    std::vector<Models::Track> m_library;
    std::vector<int>           m_selectedIndices;
    ResultCb                   m_onGenerated;

    juce::TabbedComponent m_tabs { juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<juce::Component> m_noviceContent;
    std::unique_ptr<juce::Component> m_expertContent;

    std::vector<std::unique_ptr<TypeCard>> m_noviceCards;

    std::unique_ptr<juce::Component>   m_expertTabTracks;
    std::unique_ptr<juce::Component>   m_expertTabParams;
    std::unique_ptr<juce::Component>   m_expertTabType;
    std::unique_ptr<juce::Component>   m_expertTabPreview;

    std::unique_ptr<juce::ListBox>     m_libraryList;
    std::unique_ptr<juce::ListBox>     m_selectedList;
    std::unique_ptr<TrackListModel>    m_libraryModel;
    std::unique_ptr<TrackListModel>    m_selectedModel;
    std::unique_ptr<juce::TextButton>  m_addBtn;
    std::unique_ptr<juce::TextButton>  m_removeBtn;

    std::unique_ptr<juce::Slider>      m_bpmSlider;
    std::unique_ptr<juce::Slider>      m_durationSlider;
    std::unique_ptr<juce::Label>       m_bpmLabel;
    std::unique_ptr<juce::Label>       m_durationLabel;
    std::unique_ptr<juce::ToggleButton> m_useStemsToggle;
    std::unique_ptr<juce::ToggleButton> m_harmonicOnlyToggle;
    std::unique_ptr<juce::ComboBox>    m_keyCombo;
    std::unique_ptr<juce::Label>       m_keyLabel;

    std::unique_ptr<juce::ToggleButton> m_typeMashup;
    std::unique_ptr<juce::ToggleButton> m_typeMegamix;
    std::unique_ptr<juce::ToggleButton> m_typeMedley;
    std::unique_ptr<juce::ToggleButton> m_typeRemix;

    std::unique_ptr<juce::Label>       m_previewLabel;

    juce::ToggleButton m_modeToggle { "Mode Expert" };
    juce::TextButton   m_generateBtn { "Generer" };
    juce::TextButton   m_cancelBtn   { "Annuler" };
    juce::Label        m_statusLabel;

    std::unique_ptr<BeatMate::Services::AI::AIMashupOrchestrator> m_orchestrator;
    bool m_generating { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIMixWizardDialog)
};

}
