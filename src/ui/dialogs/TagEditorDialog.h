#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class TagEditorDialog : public juce::Component {
public:
    TagEditorDialog();
    ~TagEditorDialog() override = default;
    void setTrackInfo(const juce::String& title, const juce::String& artist,
                      const juce::String& album, const juce::String& genre,
                      int year, double bpm, const juce::String& key,
                      const juce::String& comment);
    juce::String title() const;
    juce::String artist() const;
    juce::String album() const;
    juce::String genre() const;
    int year() const;
    double bpm() const;
    juce::String key() const;
    juce::String comment() const;
    void paint(juce::Graphics& g) override;
    void resized() override;
    std::function<void()> onTagsChanged;
    static int showDialog(juce::Component* parent = nullptr);
private:
    std::unique_ptr<juce::Label> m_headerLabel;
    std::unique_ptr<juce::Label> m_titleLbl, m_artistLbl, m_albumLbl, m_genreLbl, m_yearLbl, m_bpmLbl, m_keyLbl, m_commentLbl;
    std::unique_ptr<juce::TextEditor> m_titleEdit, m_artistEdit, m_albumEdit;
    std::unique_ptr<juce::ComboBox> m_genreCombo, m_keyCombo;
    std::unique_ptr<juce::Slider> m_yearSlider, m_bpmSlider;
    std::unique_ptr<juce::TextEditor> m_commentEdit;
    std::unique_ptr<juce::Label> m_albumArtLabel;
    std::unique_ptr<juce::TextButton> m_changeArtBtn;
    std::unique_ptr<juce::TextButton> m_cancelBtn, m_saveBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TagEditorDialog)
};
}
