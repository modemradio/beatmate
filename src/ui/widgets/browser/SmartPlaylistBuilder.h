#pragma once
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class SmartPlaylistBuilder : public juce::Component {
public:
    SmartPlaylistBuilder(); ~SmartPlaylistBuilder() override=default;
    struct Rule{juce::String field,op,value,conjunction;};
    std::vector<Rule> rules() const;
    void addRule();void removeSelectedRule();void clearRules();
    void resized() override;
    class Listener{public:virtual ~Listener()=default;virtual void rulesChanged(){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
private:
    void setupUI();
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::TextButton> m_addBtn,m_removeBtn;
    std::unique_ptr<juce::ListBox> m_rulesTable;
    class RulesModel:public juce::ListBoxModel{public:int getNumRows()override{return 0;}void paintListBoxItem(int,juce::Graphics&,int,int,bool)override{}};
    std::unique_ptr<RulesModel> m_rulesModel;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmartPlaylistBuilder)
};
} // namespace BeatMate::UI
