#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class EventWizardDialog : public juce::Component {
public:
    EventWizardDialog();
    ~EventWizardDialog() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    static int showDialog(juce::Component* parent = nullptr);
private:
    void showPage(int index);
    int m_currentPage = 0;
    static constexpr int m_pageCount = 4;
    std::unique_ptr<juce::Label> m_titleLabel, m_subtitleLabel;
    std::unique_ptr<juce::TextButton> m_prevBtn, m_nextBtn;
    // Page 0: Event info
    std::unique_ptr<juce::Label> m_nameLabel, m_venueLabel, m_dateLabel, m_startLabel, m_endLabel;
    std::unique_ptr<juce::TextEditor> m_nameEdit, m_venueEdit, m_dateEdit, m_startEdit, m_endEdit;
    // Page 1-3: placeholder labels
    juce::OwnedArray<juce::Component> m_pages;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventWizardDialog)
};
} // namespace BeatMate::UI
