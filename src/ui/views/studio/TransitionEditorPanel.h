#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "StudioTimeline.h"

namespace BeatMate::UI::Studio {

// Bottom panel inspiré du Transition Editor de Studio (Studio mode).
class TransitionEditorPanel : public juce::Component {
public:
    explicit TransitionEditorPanel(StudioTimeline& tl);
    ~TransitionEditorPanel() override;

    // Sélectionne le gap dont on édite la transition (-1 = aucun, le panel
    void setActiveGap(int gapIdx);
    int  getActiveGap() const noexcept { return m_gapIdx; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    enum class Section { EQ, Effects, Stems, Presets };
    void setSection(Section s);
    void rebuildBody();
    void applyKindFromCombo();
    void applyBarsFromSlider();

    StudioTimeline&      m_tl;
    int                  m_gapIdx { -1 };
    Section              m_section { Section::EQ };

    juce::TextButton     m_btnEQ      { "EQ" };
    juce::TextButton     m_btnFX      { "Effects" };
    juce::TextButton     m_btnStems   { "Stems" };
    juce::TextButton     m_btnPresets { "Presets" };

    juce::ComboBox       m_kindCombo;
    juce::Slider         m_barsSlider;
    juce::Label          m_barsLabel  { {}, "Bars" };

    juce::Label          m_statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransitionEditorPanel)
};

} // namespace BeatMate::UI::Studio
