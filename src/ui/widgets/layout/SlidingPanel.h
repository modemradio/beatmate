#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <chrono>

namespace BeatMate::UI {


class SlidingPanel : public juce::Component, public juce::Timer {
public:
    enum SlideDirection { FromLeft, FromRight, FromTop, FromBottom };

    SlidingPanel(SlideDirection direction = FromRight);
    ~SlidingPanel() override = default;

    void setContentComponent(juce::Component* content);
    void setPanelSize(int size) { m_panelSize = size; }
    void setAnimationDurationMs(int ms) { m_animDurationMs = ms; }

    void open();
    void close();
    void toggle();
    bool isOpen() const { return m_open; }

    void setOverlayEnabled(bool enabled) { m_overlayEnabled = enabled; }
    void setOverlayColor(const juce::Colour& c) { m_overlayColor = c; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;

    class Listener { public: virtual ~Listener() = default;
        virtual void panelOpened() {} virtual void panelClosed() {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    void updateLayout();
    juce::Rectangle<int> getPanelBounds(float progress) const;

    SlideDirection m_direction;
    juce::Component* m_content = nullptr;
    int m_panelSize = 300;
    int m_animDurationMs = 250;
    bool m_open = false;
    bool m_animating = false;
    float m_animProgress = 0.0f;    // 0 = closed, 1 = open
    float m_animTarget = 0.0f;
    std::chrono::steady_clock::time_point m_animStart;
    bool m_overlayEnabled = true;
    juce::Colour m_overlayColor{juce::Colour::fromRGBA(0, 0, 0, 128)};
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlidingPanel)
};

} // namespace BeatMate::UI
