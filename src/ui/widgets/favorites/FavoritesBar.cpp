#include "FavoritesBar.h"

#include <algorithm>
#include <cctype>

namespace BeatMate::UI::Widgets {

namespace {
// palette de repli quand le slot n'a pas de colorHex
constexpr std::array<juce::uint32, 12> kDefaultPalette = {
    0xFFEF4444, // red
    0xFFF97316, // orange
    0xFFF59E0B, // amber
    0xFFEAB308, // yellow
    0xFF84CC16, // lime
    0xFF22C55E, // green
    0xFF14B8A6, // teal
    0xFF06B6D4, // cyan
    0xFF3B82F6, // blue
    0xFF8B5CF6, // violet
    0xFFEC4899, // pink
    0xFF94A3B8  // slate
};

constexpr int kCornerRadius = 4;
constexpr int kSlotPadding  = 4;
constexpr int kBarPaddingX  = 2;
constexpr int kBarPaddingY  = 2;
} // namespace

FavoritesBar::FavoritesBar() {
    for (int i = 0; i < kNumSlots; ++i) {
        m_slots[i].slotIndex = i;
        m_slots[i].playlistId = -1;
    }
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void FavoritesBar::setSlots(const std::array<FavoriteSlot, kNumSlots>& slots) {
    m_slots = slots;
    // slotIndex reste autoritaire quelles que soient les données appelantes
    for (int i = 0; i < kNumSlots; ++i)
        m_slots[i].slotIndex = i;
    repaint();
}

std::array<FavoriteSlot, FavoritesBar::kNumSlots> FavoritesBar::getSlots() const {
    return m_slots;
}

void FavoritesBar::resized() {
    // layout calculé dans slotBounds() à chaque paint
}

juce::Rectangle<int> FavoritesBar::slotBounds(int index) const {
    const auto area = getLocalBounds().reduced(kBarPaddingX, kBarPaddingY);
    if (area.isEmpty()) return {};
    const float totalW = static_cast<float>(area.getWidth());
    const float slotW  = totalW / static_cast<float>(kNumSlots);
    const int x0 = area.getX() + static_cast<int>(std::round(slotW * static_cast<float>(index)));
    const int x1 = area.getX() + static_cast<int>(std::round(slotW * static_cast<float>(index + 1)));
    juce::Rectangle<int> r(x0, area.getY(), x1 - x0, area.getHeight());
    return r.reduced(kSlotPadding / 2, 0);
}

int FavoritesBar::slotAt(juce::Point<int> p) const {
    for (int i = 0; i < kNumSlots; ++i)
        if (slotBounds(i).contains(p))
            return i;
    return -1;
}

juce::Colour FavoritesBar::parseHex(const std::string& hex, juce::Colour fallback) {
    if (hex.empty()) return fallback;
    std::string s = hex;
    if (!s.empty() && s.front() == '#') s.erase(s.begin());
    if (s.size() != 6 && s.size() != 8) return fallback;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return fallback;
    const auto value = static_cast<juce::uint32>(std::stoul(s, nullptr, 16));
    if (s.size() == 6)
        return juce::Colour(static_cast<juce::uint32>(0xFF000000u | value));
    return juce::Colour(value);
}

juce::Colour FavoritesBar::colourForSlot(const FavoriteSlot& slot, int index) {
    const auto fallback = juce::Colour(kDefaultPalette[static_cast<size_t>(
        index % static_cast<int>(kDefaultPalette.size()))]);
    return parseHex(slot.colorHex, fallback);
}

void FavoritesBar::paint(juce::Graphics& g) {
    auto bg = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF1A1D24));
    g.fillRoundedRectangle(bg, static_cast<float>(kCornerRadius + 2));
    g.setColour(juce::Colour(0xFF2A2F3A));
    g.drawRoundedRectangle(bg.reduced(0.5f), static_cast<float>(kCornerRadius + 2), 1.0f);

    juce::Font font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold"));
    juce::Font glyphFont(juce::FontOptions{}.withHeight(18.0f).withStyle("Bold"));

    for (int i = 0; i < kNumSlots; ++i) {
        const auto& slot = m_slots[i];
        const auto rect = slotBounds(i).toFloat();
        if (rect.isEmpty()) continue;

        const bool empty = (slot.playlistId < 0);
        const bool hovered = (i == m_hoverIndex);
        const auto accent = colourForSlot(slot, i);

        if (empty) {
            g.setColour(juce::Colour(hovered ? 0xFF242A36 : 0xFF1F242E));
            g.fillRoundedRectangle(rect, static_cast<float>(kCornerRadius));

            const juce::Colour dashCol = hovered ? juce::Colour(0xFF6B7280)
                                                 : juce::Colour(0xFF3F4654);
            g.setColour(dashCol);
            juce::Path outline;
            outline.addRoundedRectangle(rect.reduced(1.0f), static_cast<float>(kCornerRadius));
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType stroke(1.0f);
            juce::Path dashed;
            stroke.createDashedStroke(dashed, outline, dashes, 2);
            g.fillPath(dashed);

            g.setFont(glyphFont);
            g.setColour(hovered ? juce::Colour(0xFFB0B8C5)
                                : juce::Colour(0xFF55606F));
            g.drawText("+", rect, juce::Justification::centred, false);
        } else {
            const auto baseTop    = accent.withMultipliedBrightness(hovered ? 0.65f : 0.48f)
                                           .withMultipliedSaturation(0.85f);
            const auto baseBottom = accent.withMultipliedBrightness(hovered ? 0.42f : 0.30f)
                                           .withMultipliedSaturation(0.80f);
            juce::ColourGradient grad(baseTop, rect.getCentreX(), rect.getY(),
                                      baseBottom, rect.getCentreX(), rect.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(rect, static_cast<float>(kCornerRadius));

            auto stripe = rect.withWidth(3.0f).reduced(0.0f, 2.0f);
            g.setColour(accent);
            g.fillRoundedRectangle(stripe, 1.5f);

            if (hovered) {
                g.setColour(accent.withAlpha(0.55f));
                g.drawRoundedRectangle(rect.reduced(0.5f),
                                       static_cast<float>(kCornerRadius), 1.8f);
            } else {
                g.setColour(juce::Colour(0xFF2A2F3A));
                g.drawRoundedRectangle(rect.reduced(0.5f),
                                       static_cast<float>(kCornerRadius), 1.0f);
            }

            g.setFont(font);
            g.setColour(juce::Colours::white.withAlpha(0.95f));
            auto textArea = rect.reduced(6.0f, 3.0f).withTrimmedLeft(4.0f);
            const juce::String label = slot.label.empty()
                                           ? juce::String("#") + juce::String(i + 1)
                                           : juce::String(slot.label).substring(0, 12);
            g.drawText(label, textArea, juce::Justification::centred, true);
        }

        {
            juce::Font idxFont(juce::FontOptions{}.withHeight(9.0f));
            g.setFont(idxFont);
            g.setColour(juce::Colours::white.withAlpha(empty ? 0.25f : 0.45f));
            juce::Rectangle<float> badge(rect.getRight() - 18.0f, rect.getY() + 1.0f,
                                         16.0f, 10.0f);
            badge = badge.reduced(2.0f, 1.0f);
            g.drawText(juce::String(i + 1), badge,
                       juce::Justification::centredRight, false);
        }
    }
}

void FavoritesBar::mouseDown(const juce::MouseEvent& e) {
    const int idx = slotAt(e.getPosition());
    if (idx < 0) return;

    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (onSlotRightClicked) onSlotRightClicked(idx);
    } else {
        if (onSlotClicked) onSlotClicked(idx);
    }
}

void FavoritesBar::mouseMove(const juce::MouseEvent& e) {
    const int idx = slotAt(e.getPosition());
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        repaint();
    }
}

void FavoritesBar::mouseExit(const juce::MouseEvent&) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        repaint();
    }
}

} // namespace BeatMate::UI::Widgets
