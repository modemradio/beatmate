#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace BeatMate::UI::Widgets {

struct FavoriteSlot {
    int slotIndex = 0;
    int64_t playlistId = -1;
    std::string label;
    std::string colorHex;
};

class FavoritesBar : public juce::Component {
public:
    static constexpr int kNumSlots = 12;

    FavoritesBar();
    ~FavoritesBar() override = default;

    void setSlots(const std::array<FavoriteSlot, kNumSlots>& slots);
    std::array<FavoriteSlot, kNumSlots> getSlots() const;

    std::function<void(int slotIndex)> onSlotClicked;
    std::function<void(int slotIndex)> onSlotRightClicked;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> slotBounds(int index) const;
    int slotAt(juce::Point<int> p) const;
    static juce::Colour colourForSlot(const FavoriteSlot& slot, int index);
    static juce::Colour parseHex(const std::string& hex, juce::Colour fallback);

    std::array<FavoriteSlot, kNumSlots> m_slots{};
    int m_hoverIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FavoritesBar)
};

}
