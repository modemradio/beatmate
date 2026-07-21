#include "CueEditPopover.h"
#include "../HotCueView.h"
#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"

namespace BeatMate::UI {

namespace {
constexpr int kSwatchSize = 24;
constexpr int kSwatchGap = 5;
constexpr int kSwatchCols = 8;
constexpr int kMargin = 14;
constexpr int kSwatchTop = 96;
}

CueEditPopover::CueEditPopover(int cueIndex, const juce::String& cueName, int colorIndex,
                               bool transientEnabled)
    : m_cueIndex(cueIndex), m_colorIndex(colorIndex)
{
    const juce::String letter = juce::String::charToString(
        static_cast<juce::juce_wchar>('A' + cueIndex));

    m_nameLabel = std::make_unique<juce::Label>("nl",
        BM_TJ("hotcue.popover.rename") + " " + letter);
    m_nameLabel->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    m_nameLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_nameLabel);

    m_nameEditor = std::make_unique<juce::TextEditor>("ne");
    m_nameEditor->setText(cueName, juce::dontSendNotification);
    m_nameEditor->setFont(juce::Font(13.0f));
    m_nameEditor->setColour(juce::TextEditor::backgroundColourId, Colors::bgSurface());
    m_nameEditor->setColour(juce::TextEditor::textColourId, Colors::textPrimary());
    m_nameEditor->setColour(juce::TextEditor::outlineColourId, Colors::border());
    m_nameEditor->setColour(juce::TextEditor::focusedOutlineColourId, Colors::primary());
    m_nameEditor->onReturnKey = [this] {
        commitRename();
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    m_nameEditor->onFocusLost = [this] { commitRename(); };
    addAndMakeVisible(*m_nameEditor);

    m_colorLabel = std::make_unique<juce::Label>("cl", BM_TJ("hotcue.popover.color"));
    m_colorLabel->setFont(juce::Font("Segoe UI", 11.0f, juce::Font::bold));
    m_colorLabel->setColour(juce::Label::textColourId, Colors::textSecondary());
    addAndMakeVisible(*m_colorLabel);

    m_transientBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.popover.transient"));
    m_transientBtn->setColour(juce::TextButton::buttonColourId, Colors::primary().withAlpha(0.25f));
    m_transientBtn->setColour(juce::TextButton::textColourOffId, Colors::textPrimary());
    m_transientBtn->setEnabled(transientEnabled);
    m_transientBtn->setTooltip(BM_TJ("hotcue.popover.transientTip"));
    m_transientBtn->onClick = [this] { if (onSnapTransient) onSnapTransient(m_cueIndex); };
    addAndMakeVisible(*m_transientBtn);

    m_deleteBtn = std::make_unique<juce::TextButton>(BM_TJ("hotcue.popover.delete"));
    m_deleteBtn->setColour(juce::TextButton::buttonColourId, Colors::error().withAlpha(0.25f));
    m_deleteBtn->setColour(juce::TextButton::textColourOffId, Colors::error());
    m_deleteBtn->onClick = [this] {
        if (onDelete) onDelete(m_cueIndex);
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    addAndMakeVisible(*m_deleteBtn);

    setSize(kMargin * 2 + kSwatchCols * kSwatchSize + (kSwatchCols - 1) * kSwatchGap, 216);
}

void CueEditPopover::commitRename()
{
    if (onRename)
        onRename(m_cueIndex, m_nameEditor->getText());
}

juce::Rectangle<int> CueEditPopover::swatchBounds(int swatchIndex) const
{
    const int row = swatchIndex / kSwatchCols;
    const int col = swatchIndex % kSwatchCols;
    return { kMargin + col * (kSwatchSize + kSwatchGap),
             kSwatchTop + row * (kSwatchSize + kSwatchGap),
             kSwatchSize, kSwatchSize };
}

void CueEditPopover::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgElevated());

    for (int i = 0; i < 16; ++i)
    {
        auto b = swatchBounds(i).toFloat();
        const juce::Colour c = CueColors::kPalette[i];

        if (i == m_hoveredSwatch)
        {
            g.setColour(c.withAlpha(0.35f));
            g.fillRoundedRectangle(b.expanded(3.0f), 7.0f);
        }

        g.setColour(c);
        g.fillRoundedRectangle(b, 5.0f);

        if (i == m_colorIndex)
        {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(b.expanded(1.5f), 6.0f, 2.0f);
        }
        else
        {
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.drawRoundedRectangle(b, 5.0f, 1.0f);
        }
    }
}

void CueEditPopover::resized()
{
    const int w = getWidth();
    m_nameLabel->setBounds(kMargin, 10, w - kMargin * 2, 16);
    m_nameEditor->setBounds(kMargin, 28, w - kMargin * 2, 26);
    m_colorLabel->setBounds(kMargin, 74, w - kMargin * 2, 16);

    const int btnY = kSwatchTop + 2 * kSwatchSize + kSwatchGap + 14;
    const int btnW = (w - kMargin * 2 - 8) / 2;
    m_transientBtn->setBounds(kMargin, btnY, btnW, 28);
    m_deleteBtn->setBounds(kMargin + btnW + 8, btnY, btnW, 28);
}

void CueEditPopover::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < 16; ++i)
    {
        if (swatchBounds(i).contains(e.getPosition()))
        {
            m_colorIndex = i;
            repaint();
            if (onColorPicked)
                onColorPicked(m_cueIndex, i);
            return;
        }
    }
}

void CueEditPopover::mouseMove(const juce::MouseEvent& e)
{
    int hovered = -1;
    for (int i = 0; i < 16; ++i)
    {
        if (swatchBounds(i).contains(e.getPosition()))
        {
            hovered = i;
            break;
        }
    }
    if (hovered != m_hoveredSwatch)
    {
        m_hoveredSwatch = hovered;
        setMouseCursor(hovered >= 0 ? juce::MouseCursor::PointingHandCursor
                                    : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

} // namespace BeatMate::UI
