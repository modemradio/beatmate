#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace BeatMate::UI {

class ChapterBuilderWidget : public juce::Component
{
public:
    ChapterBuilderWidget();
    ~ChapterBuilderWidget() override = default;

    struct Chapter
    {
        juce::String name;
        juce::Colour colour;
        float duration = 1.0f;  // Relative duration
        juce::String genre;
        double bpm = 128.0;
        float energy = 5.0f;
    };

    void addChapter(const juce::String& name, juce::Colour colour, float duration);
    void clearChapters();
    int getNumChapters() const { return static_cast<int>(chapters_.size()); }
    const Chapter& getChapter(int index) const { return chapters_[static_cast<size_t>(index)]; }

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void chapterSelected(int index) {}
        virtual void chapterResized(int index, float newDuration) {}
    };
    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    std::vector<Chapter> chapters_;
    int selectedChapter_ = -1;
    int dragEdgeIndex_ = -1;    // Index of the edge being dragged (-1 = none)
    float dragStartX_ = 0.0f;

    juce::ListenerList<Listener> listeners_;

    float getTotalDuration() const;
    int hitTestEdge(float x) const;
    int hitTestChapter(float x) const;
    void addDefaultChapters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChapterBuilderWidget)
};

} // namespace BeatMate::UI
