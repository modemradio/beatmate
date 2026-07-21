#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../IRetranslatable.h"

namespace BeatMate::UI {

class HelpView : public juce::Component, public BeatMate::UI::IRetranslatable {
public:
    HelpView();
    ~HelpView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;
    void visibilityChanged() override;

private:
    void setupUI();
    void buildTabs();
    bool m_tabsBuilt = false;

    // Onglet construit seulement quand il devient visible : l'aide represente
    // plus de 100 Ko de texte, inutile de payer ce cout au demarrage.
    class LazyTab : public juce::Component {
    public:
        explicit LazyTab(std::function<juce::Component*()> f) : factory(std::move(f)) {}
        void visibilityChanged() override
        {
            if (! isVisible() || child != nullptr || ! factory) return;
            child.reset(factory());
            if (child != nullptr) { addAndMakeVisible(*child); resized(); }
        }
        void resized() override { if (child) child->setBounds(getLocalBounds()); }
    private:
        std::function<juce::Component*()> factory;
        std::unique_ptr<juce::Component> child;
    };

    juce::Component* createGuideTab();
    juce::Component* createShortcutsTab();
    juce::Component* createFAQTab();
    juce::Component* createIAAssistantTab();
    juce::Component* createAboutTab();

    static juce::TextEditor* createStyledTextArea();

    juce::String searchDocumentation(const juce::String& query);

    class FAQPanel : public juce::Component {
    public:
        FAQPanel(const juce::String& question, const juce::String& answer);
        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseUp(const juce::MouseEvent& e) override;
        int getDesiredHeight() const;
        juce::String getQuestion() const { return m_question; }
        juce::String getAnswer() const { return m_answer; }

    private:
        juce::String m_question, m_answer;
        bool m_expanded = false;
        juce::Label m_questionLabel;
        juce::Label m_answerLabel;
    };

    class FAQContainer : public juce::Component {
    public:
        void addPanel(FAQPanel* panel);
        void layoutPanels();
        void resized() override;
        juce::OwnedArray<FAQPanel> panels;
    };

    class ShortcutsTableModel : public juce::TableListBoxModel {
    public:
        struct Entry { juce::String category; juce::String shortcut; juce::String action; };
        std::vector<Entry> entries;
        int getNumRows() override { return (int)entries.size(); }
        void paintRowBackground(juce::Graphics& g, int row, int w, int h, bool selected) override;
        void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool selected) override;
    };

    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TabbedComponent> m_tabWidget;

    ShortcutsTableModel m_shortcutsModel;
    std::unique_ptr<juce::TableListBox> m_shortcutsTable;

    std::unique_ptr<FAQContainer> m_faqContainer;
    std::unique_ptr<juce::Viewport> m_faqViewport;

    std::unique_ptr<juce::TextEditor> m_iaInput;
    std::unique_ptr<juce::TextButton> m_iaSearchBtn;
    std::unique_ptr<juce::TextEditor> m_iaOutput;

    struct DocEntry { juce::String title; juce::String content; };
    std::vector<DocEntry> m_documentationDB;
    void buildDocumentationDB();

    void runOnlineUpdateCheck();
    void runLocalMsiInstall();
    std::unique_ptr<juce::TextButton> m_checkUpdateBtn;
    std::unique_ptr<juce::TextButton> m_installLocalMsiBtn;
    std::unique_ptr<juce::Label> m_updateStatusLbl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpView)
};

} // namespace BeatMate::UI
