#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <string>
#include <vector>
namespace BeatMate::UI {

struct MetaKnobArc {
    juce::String label;
    juce::Colour color;
    float value = 0.0f;     // Current mapped value [0..1]
};

class MetaKnobWidget : public juce::Component {
public:
    MetaKnobWidget();
    ~MetaKnobWidget() override = default;

    void setPosition(float position);   // [0..1]
    float getPosition() const { return m_position; }

    void setLabel(const juce::String& label) { m_label = label; repaint(); }
    void setProfileName(const juce::String& name) { m_profileName = name; repaint(); }

    void setArcs(const std::vector<MetaKnobArc>& arcs) { m_arcs = arcs; repaint(); }
    void clearArcs() { m_arcs.clear(); repaint(); }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;

    class Listener { public: virtual ~Listener() = default; virtual void metaKnobChanged(float position) {} };
    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    float m_position = 0.5f;
    juce::String m_label = "META";
    juce::String m_profileName;
    std::vector<MetaKnobArc> m_arcs;
    juce::Point<int> m_lastMousePos;
    bool m_dragging = false;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetaKnobWidget)
};

} // namespace BeatMate::UI
