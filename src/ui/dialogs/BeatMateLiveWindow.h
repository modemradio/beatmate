#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <functional>

#include "../views/LiveSuggestView.h"

namespace BeatMate::UI {

class BeatMateLiveWindow : public juce::DocumentWindow,
                           private juce::Timer
{
public:
    enum class AutoHidePolicy
    {
        Disabled = 0,
        Fade,
        MinimizeToTray,
        CompactDock,
        FloatingIcon
    };

    static juce::String policyToString(AutoHidePolicy p);
    static AutoHidePolicy policyFromString(const juce::String& s);

    BeatMateLiveWindow();
    explicit BeatMateLiveWindow(Services::Library::TrackDataProvider* provider);
    ~BeatMateLiveWindow() override;

    std::function<void()> onCloseCallback;

    void closeButtonPressed() override;
    void setCompactMode(bool compact);

    void setAutoHidePolicy(AutoHidePolicy policy);
    AutoHidePolicy getAutoHidePolicy() const noexcept { return policy_; }

    bool isDJSoftwareForeground() const noexcept { return djForeground_; }

    enum class ShrinkKind { None, Fade, MinimizeToTray, CompactDock, FloatingIcon };

    void onAutoHideApplied(ShrinkKind kind);
    void onAutoHideCleared();
    bool isShrunk() const noexcept { return m_isShrunk; }

    std::function<void()> reapplyAutoHide;

    juce::Rectangle<int> savedFullBounds;

    void expandToFull();

    void ensureFloatingBadge();
    void hideFloatingBadge();

private:
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    bool detectDJForeground();
    void onDJForegroundEntered();
    void onDJForegroundExited();

    void applyPolicyEnter();
    void applyPolicyExit();

    AutoHidePolicy loadPolicyFromSettings() const;
    void           savePolicyToSettings() const;

    bool isMouseNearWindow(int fringePx) const;

    void registerGlobalHotkey();
    void unregisterGlobalHotkey();
    void toggleVisibilityAndForeground();

    class LiveTrayIcon;
    std::unique_ptr<LiveTrayIcon> trayIcon_;
    void ensureTrayIconVisible();
    void hideTrayIcon();

    class FloatingBadge;
    std::unique_ptr<FloatingBadge> floatingBadge_;

    juce::Component::SafePointer<juce::TopLevelWindow> savedMainWindow_;

    AutoHidePolicy policy_         { AutoHidePolicy::Fade };
    bool           djForeground_   { false };
    bool           m_isShrunk      { false };
    ShrinkKind     m_shrinkKind    { ShrinkKind::None };
    double         m_mouseLeftAtMs { 0.0 };
    bool           m_userInteracting { false };
    bool           savedAlwaysOnTop_ { false };

    juce::Time     m_expandStartTime;
    bool           m_animatingAlpha { false };

   #if JUCE_WINDOWS
    int  hotkeyId_ { 0xB347 };
    bool hotkeyRegistered_ { false };
    std::unique_ptr<juce::Thread> hotkeyThread_;
   #endif

    static constexpr int  kPollMs        = 500;
    static constexpr int  kFringePx      = 12;
    static constexpr int  kReHideDelayMs = 2000;
    static constexpr int  kAlphaFadeMs   = 140;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatMateLiveWindow)
};

} // namespace BeatMate::UI
