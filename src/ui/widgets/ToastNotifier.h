#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <cstdint>

namespace BeatMate::UI::Widgets {

class ToastNotifier : public juce::Component, private juce::Timer {
public:
    enum class Kind { Info, Success, Warning, Error, Progress };

    ToastNotifier();
    ~ToastNotifier() override;

    int show(const juce::String& title,
             const juce::String& message = {},
             Kind kind = Kind::Info,
             int autoCloseMs = 4000);

    void update(int id, const juce::String& message, Kind kind = Kind::Progress);

    void dismiss(int id);

    static ToastNotifier& getInstance();
    static void setInstance(ToastNotifier* instance);

    void setCancelCallback(int id, std::function<void()> cb);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    struct ToastItem {
        int id;
        juce::String title;
        juce::String message;
        Kind kind;
        int autoCloseMs;
        int64_t createdMs;
        float alpha = 0.0f;
        bool dismissing = false;
        std::function<void()> cancelCb;
    };

    juce::Rectangle<int> getToastBounds(int index) const;
    juce::Rectangle<int> getCloseButtonBounds(const juce::Rectangle<int>& toast) const;
    void layoutToasts();
    juce::Colour accentFor(Kind k) const;
    int findById(int id) const;

    std::vector<ToastItem> m_items;
    int m_nextId = 1;
    int m_hoverCloseId = -1;
    float m_spinnerAngle = 0.0f;
    int64_t m_lastTickMs = 0;

    juce::Path m_spinnerPath;

    static ToastNotifier* s_instance;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToastNotifier)
};

} // namespace BeatMate::UI::Widgets
