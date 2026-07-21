#include "CuePadGrid.h"

namespace BeatMate::UI {

CuePadGrid::CuePadGrid()
{
    for (int i = 0; i < 8; ++i)
    {
        m_pads[static_cast<size_t>(i)] = std::make_unique<CuePadComponent>(i);
        auto& pad = *m_pads[static_cast<size_t>(i)];
        pad.onPress = [this](int idx) { if (onPadPress) onPadPress(idx); };
        pad.onRelease = [this](int idx) { if (onPadRelease) onPadRelease(idx); };
        pad.onContextMenu = [this](int idx) { if (onPadContextMenu) onPadContextMenu(idx); };
        addAndMakeVisible(pad);
    }
}

void CuePadGrid::refreshPad(int index, const PadInfo& info)
{
    if (index < 0 || index >= 8) return;
    m_pads[static_cast<size_t>(index)]->setInfo(info);
}

void CuePadGrid::setSelectedIndex(int index)
{
    if (m_selectedIndex == index) return;
    m_selectedIndex = index;
    for (int i = 0; i < 8; ++i)
        m_pads[static_cast<size_t>(i)]->setSelected(i == index);
}

void CuePadGrid::setHoldIndex(int index)
{
    if (m_holdIndex == index) return;
    m_holdIndex = index;
    for (int i = 0; i < 8; ++i)
        m_pads[static_cast<size_t>(i)]->setHolding(i == index);
}

void CuePadGrid::setPreviewingIndex(int index)
{
    if (m_previewingIndex == index) return;
    m_previewingIndex = index;
    for (int i = 0; i < 8; ++i)
        m_pads[static_cast<size_t>(i)]->setPreviewing(i == index);
}

juce::Rectangle<int> CuePadGrid::padScreenBounds(int index) const
{
    if (index < 0 || index >= 8) return {};
    return m_pads[static_cast<size_t>(index)]->getScreenBounds();
}

void CuePadGrid::resized()
{
    const int gap = 6;
    const int cols = 4;
    const int rows = 2;
    const int padW = (getWidth() - gap * (cols - 1)) / cols;
    const int padH = (getHeight() - gap * (rows - 1)) / rows;

    for (int i = 0; i < 8; ++i)
    {
        const int row = i / cols;
        const int col = i % cols;
        m_pads[static_cast<size_t>(i)]->setBounds(
            col * (padW + gap), row * (padH + gap), padW, padH);
    }
}

} // namespace BeatMate::UI
