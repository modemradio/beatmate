#include "SlidingPanel.h"
#include "../../styles/ColorPalette.h"
#include <cmath>
namespace BeatMate::UI {

SlidingPanel::SlidingPanel(SlideDirection direction) : m_direction(direction) {
    setInterceptsMouseClicks(false, true);
}

void SlidingPanel::setContentComponent(juce::Component* content) {
    m_content = content;
    if (content) addAndMakeVisible(content);
    updateLayout();
}

void SlidingPanel::open() {
    m_open = true;
    m_animTarget = 1.0f;
    m_animating = true;
    m_animStart = std::chrono::steady_clock::now();
    setInterceptsMouseClicks(true, true);
    setVisible(true);
    startTimer(16); // ~60fps
}

void SlidingPanel::close() {
    m_open = false;
    m_animTarget = 0.0f;
    m_animating = true;
    m_animStart = std::chrono::steady_clock::now();
    startTimer(16);
}

void SlidingPanel::toggle() {
    if (m_open) close(); else open();
}

void SlidingPanel::timerCallback() {
    if (!m_animating) { stopTimer(); return; }

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_animStart).count();
    float t = std::min(1.0f, elapsed / (float)m_animDurationMs);

    // Ease-out cubic
    float easedT = 1.0f - std::pow(1.0f - t, 3.0f);

    if (m_animTarget > m_animProgress) {
        m_animProgress = easedT;
    } else {
        m_animProgress = 1.0f - easedT;
    }

    if (t >= 1.0f) {
        m_animProgress = m_animTarget;
        m_animating = false;
        stopTimer();
        if (!m_open) {
            setInterceptsMouseClicks(false, true);
            setVisible(false);
            m_listeners.call([](Listener& l) { l.panelClosed(); });
        } else {
            m_listeners.call([](Listener& l) { l.panelOpened(); });
        }
    }

    updateLayout();
    repaint();
}

void SlidingPanel::paint(juce::Graphics& g) {
    if (m_overlayEnabled && m_animProgress > 0.01f) {
        auto overlayBounds = getLocalBounds();
        g.setColour(m_overlayColor.withAlpha(m_overlayColor.getFloatAlpha() * m_animProgress));
        g.fillRect(overlayBounds);
    }

    auto panelBounds = getPanelBounds(m_animProgress);
    g.setColour(Colors::bgDarker());
    g.fillRect(panelBounds);

    if (m_direction == FromRight) {
        juce::ColourGradient shadow(juce::Colour(0x40000000), (float)panelBounds.getX(), 0,
                                     juce::Colours::transparentBlack, (float)panelBounds.getX() - 20, 0, false);
        g.setGradientFill(shadow);
        g.fillRect(panelBounds.getX() - 20, panelBounds.getY(), 20, panelBounds.getHeight());
    } else if (m_direction == FromLeft) {
        juce::ColourGradient shadow(juce::Colour(0x40000000), (float)panelBounds.getRight(), 0,
                                     juce::Colours::transparentBlack, (float)panelBounds.getRight() + 20, 0, false);
        g.setGradientFill(shadow);
        g.fillRect(panelBounds.getRight(), panelBounds.getY(), 20, panelBounds.getHeight());
    } else if (m_direction == FromBottom) {
        juce::ColourGradient shadow(juce::Colour(0x40000000), 0, (float)panelBounds.getY(),
                                     juce::Colours::transparentBlack, 0, (float)panelBounds.getY() - 20, false);
        g.setGradientFill(shadow);
        g.fillRect(panelBounds.getX(), panelBounds.getY() - 20, panelBounds.getWidth(), 20);
    } else if (m_direction == FromTop) {
        juce::ColourGradient shadow(juce::Colour(0x40000000), 0, (float)panelBounds.getBottom(),
                                     juce::Colours::transparentBlack, 0, (float)panelBounds.getBottom() + 20, false);
        g.setGradientFill(shadow);
        g.fillRect(panelBounds.getX(), panelBounds.getBottom(), panelBounds.getWidth(), 20);
    }

    g.setColour(Colors::border());
    g.drawRect(panelBounds, 1);

    g.setColour(Colors::textDim());
    if (m_direction == FromRight || m_direction == FromLeft) {
        int hx = (m_direction == FromRight) ? panelBounds.getX() + 4 : panelBounds.getRight() - 8;
        for (int i = 0; i < 3; ++i) {
            int hy = panelBounds.getCentreY() - 10 + i * 10;
            g.fillRoundedRectangle((float)hx, (float)hy, 3.0f, 6.0f, 1.5f);
        }
    }
}

void SlidingPanel::resized() {
    updateLayout();
}

void SlidingPanel::mouseDown(const juce::MouseEvent& e) {
    if (m_open && m_overlayEnabled) {
        auto panelBounds = getPanelBounds(m_animProgress);
        if (!panelBounds.contains(e.getPosition())) {
            close();
        }
    }
}

juce::Rectangle<int> SlidingPanel::getPanelBounds(float progress) const {
    int w = getWidth(), h = getHeight();
    switch (m_direction) {
        case FromRight:  return {w - (int)(m_panelSize * progress), 0, m_panelSize, h};
        case FromLeft:   return {-(int)(m_panelSize * (1.0f - progress)), 0, m_panelSize, h};
        case FromBottom: return {0, h - (int)(m_panelSize * progress), w, m_panelSize};
        case FromTop:    return {0, -(int)(m_panelSize * (1.0f - progress)), w, m_panelSize};
        default:         return {w - m_panelSize, 0, m_panelSize, h};
    }
}

void SlidingPanel::updateLayout() {
    if (m_content) {
        auto bounds = getPanelBounds(m_animProgress);
        m_content->setBounds(bounds.reduced(1));
    }
}

} // namespace BeatMate::UI
