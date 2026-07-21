#include "TransitionEditorPanel.h"

namespace BeatMate::UI::Studio {

namespace {
constexpr int kHeaderH    = 36;
constexpr int kTabH       = 26;
constexpr int kSidebarL   = 180;
constexpr juce::uint32 kBg          = 0xFF0D0F16;
constexpr juce::uint32 kBgTab       = 0xFF12141C;
constexpr juce::uint32 kSeparator   = 0xFF1F2937;
constexpr juce::uint32 kAccentBlue  = 0xFF3B82F6;
constexpr juce::uint32 kTextPrimary = 0xFFF1F5F9;
constexpr juce::uint32 kTextDim     = 0xFF94A3B8;

const char* kindToLabel(TransitionKind k) {
    switch (k) {
        case TransitionKind::Cut:         return "Cut";
        case TransitionKind::Fade:        return "Fade";
        case TransitionKind::EqualPower:  return "EqualPower";
        case TransitionKind::EchoOut:     return "EchoOut";
        case TransitionKind::FilterSweep: return "FilterSweep";
        case TransitionKind::Backspin:    return "Backspin";
        case TransitionKind::Custom:      return "Custom";
    }
    return "?";
}
} // namespace

TransitionEditorPanel::TransitionEditorPanel(StudioTimeline& tl) : m_tl(tl) {
    addAndMakeVisible(m_btnEQ);
    addAndMakeVisible(m_btnFX);
    addAndMakeVisible(m_btnStems);
    addAndMakeVisible(m_btnPresets);
    for (auto* b : { &m_btnEQ, &m_btnFX, &m_btnStems, &m_btnPresets }) {
        b->setColour(juce::TextButton::buttonColourId,   juce::Colour(kBgTab));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccentBlue));
        b->setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        b->setColour(juce::TextButton::textColourOffId,  juce::Colour(kTextDim));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(7777);
    }
    m_btnEQ.setToggleState(true, juce::dontSendNotification);
    m_btnEQ     .onClick = [this]{ setSection(Section::EQ);      };
    m_btnFX     .onClick = [this]{ setSection(Section::Effects); };
    m_btnStems  .onClick = [this]{ setSection(Section::Stems);   };
    m_btnPresets.onClick = [this]{ setSection(Section::Presets); };

    addAndMakeVisible(m_kindCombo);
    m_kindCombo.addItem("Cut",          1);
    m_kindCombo.addItem("Fade",         2);
    m_kindCombo.addItem("EqualPower",   3);
    m_kindCombo.addItem("EchoOut",      4);
    m_kindCombo.addItem("FilterSweep",  5);
    m_kindCombo.addItem("Backspin",     6);
    m_kindCombo.addItem("Custom",       7);
    m_kindCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(kBgTab));
    m_kindCombo.setColour(juce::ComboBox::textColourId,       juce::Colour(kTextPrimary));
    m_kindCombo.setColour(juce::ComboBox::outlineColourId,    juce::Colour(kAccentBlue));
    m_kindCombo.onChange = [this]{ applyKindFromCombo(); };

    addAndMakeVisible(m_barsSlider);
    m_barsSlider.setRange(1.0, 32.0, 1.0);
    m_barsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_barsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
    m_barsSlider.setColour(juce::Slider::thumbColourId,         juce::Colour(kAccentBlue));
    m_barsSlider.setColour(juce::Slider::trackColourId,         juce::Colour(kAccentBlue).withAlpha(0.4f));
    m_barsSlider.setColour(juce::Slider::backgroundColourId,    juce::Colour(kBgTab));
    m_barsSlider.setColour(juce::Slider::textBoxTextColourId,   juce::Colour(kTextPrimary));
    m_barsSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(kBgTab));
    m_barsSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    m_barsSlider.onValueChange = [this]{ applyBarsFromSlider(); };

    addAndMakeVisible(m_barsLabel);
    m_barsLabel.setColour(juce::Label::textColourId, juce::Colour(kTextDim));
    m_barsLabel.setFont(juce::FontOptions("Segoe UI", 10.0f, juce::Font::plain));
    m_barsLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(m_statusLabel);
    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour(kTextDim));
    m_statusLabel.setFont(juce::FontOptions("Segoe UI", 11.0f, juce::Font::plain));
    m_statusLabel.setJustificationType(juce::Justification::centredLeft);

    setActiveGap(-1);
}

TransitionEditorPanel::~TransitionEditorPanel() = default;

void TransitionEditorPanel::setActiveGap(int gapIdx) {
    m_gapIdx = gapIdx;
    if (m_gapIdx < 0) {
        m_kindCombo.setEnabled(false);
        m_barsSlider.setEnabled(false);
        m_statusLabel.setText(juce::String::fromUTF8("Aucune transition sélectionnée — double-cliquer un cadre bleu pour éditer."),
                              juce::dontSendNotification);
    } else {
        m_kindCombo.setEnabled(true);
        m_barsSlider.setEnabled(true);
        const int n = std::max(0, (int) m_tl.getNumClips() - 1);
        if (m_gapIdx < n) {
            const auto& curve = m_tl.getTransitionCustomCurve(m_gapIdx);
            const bool isCustom = !curve.empty();
            m_kindCombo.setSelectedId(isCustom ? 7 : 3, juce::dontSendNotification);
            m_barsSlider.setValue(8.0, juce::dontSendNotification);
        }
        m_statusLabel.setText("Gap #" + juce::String(m_gapIdx + 1) + " — édition active",
                              juce::dontSendNotification);
    }
    rebuildBody();
    repaint();
}

void TransitionEditorPanel::setSection(Section s) {
    m_section = s;
    m_btnEQ     .setToggleState(s == Section::EQ,      juce::dontSendNotification);
    m_btnFX     .setToggleState(s == Section::Effects, juce::dontSendNotification);
    m_btnStems  .setToggleState(s == Section::Stems,   juce::dontSendNotification);
    m_btnPresets.setToggleState(s == Section::Presets, juce::dontSendNotification);
    rebuildBody();
    repaint();
}

void TransitionEditorPanel::rebuildBody() {
}

void TransitionEditorPanel::applyKindFromCombo() {
    if (m_gapIdx < 0) return;
    const int id = m_kindCombo.getSelectedId();
    TransitionKind k = TransitionKind::EqualPower;
    switch (id) {
        case 1: k = TransitionKind::Cut;         break;
        case 2: k = TransitionKind::Fade;        break;
        case 3: k = TransitionKind::EqualPower;  break;
        case 4: k = TransitionKind::EchoOut;     break;
        case 5: k = TransitionKind::FilterSweep; break;
        case 6: k = TransitionKind::Backspin;    break;
        case 7: k = TransitionKind::Custom;      break;
    }
    m_tl.setTransitionType(m_gapIdx, k);
}

void TransitionEditorPanel::applyBarsFromSlider() {
    if (m_gapIdx < 0) return;
    m_tl.setTransitionDurationBars(m_gapIdx, (int) m_barsSlider.getValue());
}

void TransitionEditorPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(kBg));

    juce::Rectangle<int> hdr(0, 0, getWidth(), kHeaderH);
    g.setColour(juce::Colour(kBgTab));
    g.fillRect(hdr);
    g.setColour(juce::Colour(kSeparator));
    g.drawHorizontalLine(kHeaderH - 1, 0.0f, (float) getWidth());

    g.setColour(juce::Colour(kTextPrimary));
    g.setFont(juce::FontOptions("Segoe UI", 12.0f, juce::Font::bold));
    g.drawText("Transition Editor",
               12, 0, 200, kHeaderH,
               juce::Justification::centredLeft);

    const int contentY = kHeaderH + kTabH + 4;
    const int contentH = std::max(0, getHeight() - contentY - 4);
    juce::Rectangle<int> body(8, contentY, getWidth() - 16, contentH);

    g.setColour(juce::Colour(kBgTab));
    g.fillRoundedRectangle(body.toFloat(), 6.0f);
    g.setColour(juce::Colour(kSeparator));
    g.drawRoundedRectangle(body.toFloat(), 6.0f, 1.0f);

    g.setColour(juce::Colour(kTextDim));
    g.setFont(juce::FontOptions("Segoe UI", 11.0f, juce::Font::plain));

    juce::String hint;
    switch (m_section) {
        case Section::EQ:
            hint = juce::String::fromUTF8(
                "EQ — Volume / Bass / Mid / High avec courbes preset (Crossfade, Fade In, Fade Out, Swap, Manual) "
                "et intensités 25 / 50 / 75 / 100 %. Le pipeline audio applique les transitions via TransitionEngine "
                "en Phase 4 du bouncePlaybackBuffer.");
            break;
        case Section::Effects:
            hint = juce::String::fromUTF8(
                "Effects — Noise, Effect In/Out, Filter, Side Chain. "
                "Les effets de clip (Reverb / Delay / EQ3 / Compressor / Filter) sont appliqués clip-par-clip via "
                "applyClipEffectsToBuffer().");
            break;
        case Section::Stems:
            hint = juce::String::fromUTF8(
                "Stems — Drums / Bass / Other / Vocals. Les stems Demucs déjà extraits sur les clips sont "
                "automatiquement mixés et muteables en sidebar V14.");
            break;
        case Section::Presets:
            hint = juce::String::fromUTF8(
                "My Presets — sauvegarde / chargement de configurations EQ + FX + Stems. "
                "À étendre dans une prochaine itération avec stockage JSON dans %APPDATA%.");
            break;
    }
    g.drawFittedText(hint, body.reduced(14, 12), juce::Justification::topLeft, 10);

    g.setColour(juce::Colour(kTextDim));
    g.setFont(juce::FontOptions("Segoe UI", 10.0f, juce::Font::italic));
    if (m_gapIdx >= 0) {
        const auto& curve = m_tl.getTransitionCustomCurve(m_gapIdx);
        const juce::String detail = "Custom curve points: " + juce::String((int) curve.size())
                                  + " · Kind selected: " + juce::String(kindToLabel(
                                      (curve.empty() ? TransitionKind::EqualPower : TransitionKind::Custom)));
        g.drawText(detail, body.reduced(14, 12).removeFromBottom(18),
                   juce::Justification::bottomLeft);
    }
}

void TransitionEditorPanel::resized() {
    auto r = getLocalBounds();
    auto hdr = r.removeFromTop(kHeaderH);
    hdr.removeFromLeft(180);
    auto right = hdr.reduced(8, 6);
    m_barsLabel.setBounds(right.removeFromRight(180).removeFromLeft(36));
    m_barsSlider.setBounds(right.removeFromRight(140));
    right.removeFromRight(8);
    m_kindCombo.setBounds(right.removeFromRight(140));
    m_statusLabel.setBounds(right.reduced(8, 0));

    auto tabs = r.removeFromTop(kTabH);
    const int btnW = std::max(80, tabs.getWidth() / 4);
    m_btnEQ     .setBounds(tabs.removeFromLeft(btnW).reduced(2));
    m_btnFX     .setBounds(tabs.removeFromLeft(btnW).reduced(2));
    m_btnStems  .setBounds(tabs.removeFromLeft(btnW).reduced(2));
    m_btnPresets.setBounds(tabs.removeFromLeft(btnW).reduced(2));
}

} // namespace BeatMate::UI::Studio
