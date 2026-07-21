#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace BeatMate::UI::Motion {

constexpr int hoverMs = 160;
constexpr int transitionMs = 220;

inline juce::ComponentAnimator& animator()
{
    return juce::Desktop::getInstance().getAnimator();
}

inline void fadeIn(juce::Component& c, int ms = hoverMs)
{
    c.setVisible(true);
    animator().fadeIn(&c, ms);
}

inline void fadeOut(juce::Component& c, int ms = hoverMs)
{
    animator().fadeOut(&c, ms);
}

inline void animateBounds(juce::Component& c, juce::Rectangle<int> dest, int ms = transitionMs,
                          float finalAlpha = 1.0f)
{
    animator().animateComponent(&c, dest, finalAlpha, ms, false, 0.55, 0.2);
}

inline void slideIn(juce::Component& c, juce::Rectangle<int> dest, int fromDy = 12,
                    int ms = transitionMs)
{
    c.setBounds(dest.translated(0, fromDy));
    c.setAlpha(0.0f);
    c.setVisible(true);
    animator().animateComponent(&c, dest, 1.0f, ms, false, 0.55, 0.2);
}

inline void cancel(juce::Component& c)
{
    animator().cancelAnimation(&c, false);
}

} // namespace BeatMate::UI::Motion
