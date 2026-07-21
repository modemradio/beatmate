#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>

namespace BeatMate::UI::Utils {

inline void showAutoCloseAlert(juce::MessageBoxIconType icon,
                               const juce::String& title,
                               const juce::String& message,
                               int autoCloseMs = 20000)
{
    auto* win = new juce::AlertWindow(title, message, icon, nullptr);
    win->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    win->enterModalState(true,
        juce::ModalCallbackFunction::create([win](int) { delete win; }),
        false);

    juce::Component::SafePointer<juce::AlertWindow> safe(win);
    juce::Timer::callAfterDelay(autoCloseMs, [safe]() mutable {
        if (auto* w = safe.getComponent()) w->exitModalState(0);
    });
}

}
