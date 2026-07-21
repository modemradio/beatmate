#pragma once
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
struct CuePoint { int number; double position; juce::Colour color; juce::String label; };
struct Section { double start; double end; juce::String label; juce::Colour color; };
class WaveformWidget : public juce::Component, public juce::Timer {
public:
    WaveformWidget(); ~WaveformWidget() override;
    void setWaveformData(const std::vector<float>& peaks);
    void setColoredWaveformData(const std::vector<float>& bass,const std::vector<float>& mid,const std::vector<float>& treble);
    void setPlayheadPosition(double position);
    void setCuePoints(const std::vector<CuePoint>& cues);
    void setSections(const std::vector<Section>& sections);
    void setBeatGrid(const std::vector<double>& beats);
    void setDuration(double seconds);
    void setZoom(double zoom);void setScrollOffset(double offset);void setPlaying(bool playing);
    double playheadPosition() const{return m_playheadPos;}double zoom() const{return m_zoom;}
    class Listener{public:virtual ~Listener()=default;virtual void positionClicked(double){}virtual void positionChanged(double){}virtual void cuePointClicked(int){}virtual void cuePointRightClicked(int,juce::Point<int>){}virtual void cuePointMoved(int,double){}virtual void selectionChanged(double,double){}virtual void zoomChanged(double){}virtual void beatMoved(int /*beatIndex*/,double /*newPos*/){}};
    void addListener(Listener*l){m_listeners.add(l);}void removeListener(Listener*l){m_listeners.remove(l);}
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
    void timerCallback() override;
private:
    void drawBackground(juce::Graphics& g);void drawWaveform(juce::Graphics& g);void drawColoredWaveform(juce::Graphics& g);
    void drawBeatGrid(juce::Graphics& g);void drawSections(juce::Graphics& g);void drawCuePoints(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);void drawSelection(juce::Graphics& g);void drawTimeline(juce::Graphics& g);
    float cueScreenX(const CuePoint& cue) const;
    float flagWidthFor(const CuePoint& cue) const;
    juce::Rectangle<float> flagBoundsFor(const CuePoint& cue, float fx) const;
    int cueIndexAt(juce::Point<int> pos) const;
    std::vector<float> m_peaks,m_bassPeaks,m_midPeaks,m_treblePeaks;
    std::vector<CuePoint> m_cuePoints;std::vector<Section> m_sections;std::vector<double> m_beatGrid;
    double m_playheadPos=0.0,m_zoom=1.0,m_scrollOffset=0.0,m_duration=0.0,m_followFrac=0.3;
    double m_cacheStart=0.0,m_cacheSpanNorm=1.0;
    int m_cacheWidthLogical=0;
    float m_ampMax=0.001f;
    bool m_playing=false,m_useColoredWaveform=false,m_selecting=false,m_dragging=false;
    double m_selStart=0.0,m_selEnd=0.0,m_dragStartOffset=0.0;
    int m_draggingCueIndex=-1;
    int m_draggingBeatIndex=-1;
    int m_hoveredCueIndex=-1;
    juce::Image m_waveformCache;
    bool m_cacheValid=false;
    float m_cacheScale=1.0f;
    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformWidget)
};
} // namespace BeatMate::UI
