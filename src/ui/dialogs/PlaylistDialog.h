#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class PlaylistDialog : public juce::Component, public juce::TableListBoxModel, public juce::ChangeListener {
public:
    PlaylistDialog();
    ~PlaylistDialog() override = default;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    juce::String playlistName() const;
    juce::String description() const;
    juce::Colour color() const;
    bool isSmart() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    static int showDialog(juce::Component* parent = nullptr);
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
private:
    std::unique_ptr<juce::Label> m_nameLabel;
    std::unique_ptr<juce::TextEditor> m_nameEdit;
    std::unique_ptr<juce::Label> m_descLabel;
    std::unique_ptr<juce::TextEditor> m_descEdit;
    std::unique_ptr<juce::Label> m_colorLabel;
    std::unique_ptr<juce::TextButton> m_colorBtn;
    juce::Colour m_selectedColor{juce::Colours::cyan};
    std::unique_ptr<juce::ToggleButton> m_smartCheck;
    std::unique_ptr<juce::TableListBox> m_rulesTable;
    std::unique_ptr<juce::TextButton> m_addRuleBtn;
    std::unique_ptr<juce::TextButton> m_cancelBtn, m_okBtn;
    int m_ruleCount = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistDialog)
};
} // namespace BeatMate::UI
