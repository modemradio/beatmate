#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace BeatMate::UI {

class ScatterMapWidget : public juce::Component
{
public:
    ScatterMapWidget();
    ~ScatterMapWidget() override = default;

    struct TrackPoint
    {
        juce::String title;
        juce::String artist;
        double bpm = 128.0;
        float energy = 5.0f;
        juce::String genre;
        juce::Colour colour;
    };

    void addTrack(const juce::String& title, const juce::String& artist,
                  double bpm, float energy, const juce::String& genre,
                  juce::Colour colour = juce::Colour(0xFF4A90E2));
    void clearTracks();
    int getSelectedTrack() const { return selectedTrack_; }

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void scatterTrackSelected(int index) {}
    };
    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::vector<TrackPoint> tracks_;
    int selectedTrack_ = -1;
    int hoveredTrack_ = -1;

    juce::ListenerList<Listener> listeners_;

    double bpmMin_ = 80.0, bpmMax_ = 180.0;
    float energyMin_ = 0.0f, energyMax_ = 10.0f;

    static constexpr float margin_ = 40.0f;
    static constexpr float pointRadius_ = 6.0f;

    juce::Point<float> trackToScreen(const TrackPoint& t) const;
    int hitTestTrack(juce::Point<float> pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScatterMapWidget)
};

} // namespace BeatMate::UI
