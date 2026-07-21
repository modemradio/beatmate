#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class PlayHistoryDialog : public juce::Component, public juce::TableListBoxModel {
public:
    PlayHistoryDialog();
    ~PlayHistoryDialog() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    int getNumRows() override { return 0; }
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_fromLabel, m_toLabel;
    std::unique_ptr<juce::TextEditor> m_fromEdit, m_toEdit;
    std::unique_ptr<juce::TextButton> m_filterBtn;
    std::unique_ptr<juce::Label> m_totalPlaysLabel, m_mostPlayedLabel;
    std::unique_ptr<juce::TableListBox> m_historyTable;
    std::unique_ptr<juce::TextButton> m_closeBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayHistoryDialog)
};
}
