#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <string>
#include <map>

namespace BeatMate::UI {

class AIHelpPanel : public juce::Component {
public:
    AIHelpPanel();
    ~AIHelpPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setContext(int moduleIndex);

    void showShortcuts();

    void toggleVisible();

private:
    struct HelpTopic {
        std::string title;
        std::string content;
        std::string module;
        std::vector<std::string> keywords;
    };

    struct Shortcut {
        std::string key;
        std::string action;
        std::string module;
    };

    void initHelpDatabase();
    void initShortcuts();
    void updateResults();
    void showTopicContent(int topicIndex);

    std::vector<HelpTopic> m_topics;
    std::vector<HelpTopic> m_filteredTopics;
    std::vector<Shortcut> m_shortcuts;

    std::unique_ptr<juce::TextEditor> m_searchEditor;
    std::unique_ptr<juce::ListBox> m_topicList;
    std::unique_ptr<juce::TextEditor> m_contentDisplay;
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_shortcutsBtn;
    std::unique_ptr<juce::TextButton> m_closeBtn;

    int m_currentModule = 0;
    bool m_showingShortcuts = false;

    class TopicListModel : public juce::ListBoxModel {
    public:
        std::vector<HelpTopic>* topics = nullptr;
        std::function<void(int)> onSelect;
        int getNumRows() override { return topics ? static_cast<int>(topics->size()) : 0; }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    };
    std::unique_ptr<TopicListModel> m_listModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIHelpPanel)
};

} // namespace BeatMate::UI
