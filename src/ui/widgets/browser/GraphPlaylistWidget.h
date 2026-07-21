#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace BeatMate::UI {

// GraphPlaylistWidget - Force-directed graph showing track compatibility
class GraphPlaylistWidget : public juce::Component,
                            private juce::Timer
{
public:
    GraphPlaylistWidget();
    ~GraphPlaylistWidget() override;

    struct TrackNode
    {
        juce::String title;
        double bpm = 128.0;
        juce::String key;
        juce::Point<float> position;
        juce::Point<float> velocity;
    };

    struct CompatibilityEdge
    {
        int fromIndex = 0;
        int toIndex = 0;
        float score = 0.0f; // 0-1
    };

    int addTrack(const juce::String& title, double bpm, const juce::String& key);
    void addEdge(int from, int to, float score);
    void clear();

    int getSelectedTrack() const { return selectedNode_; }

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void trackNodeSelected(int index) {}
    };
    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void applyForceDirectedLayout();
    int hitTestNode(juce::Point<float> pos) const;

    std::vector<TrackNode> nodes_;
    std::vector<CompatibilityEdge> edges_;
    int selectedNode_ = -1;
    int draggedNode_ = -1;

    juce::ListenerList<Listener> listeners_;

    static constexpr float nodeRadius_ = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphPlaylistWidget)
};

} // namespace BeatMate::UI
