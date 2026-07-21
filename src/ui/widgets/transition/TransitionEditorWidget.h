#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../services/preparation/TransitionEditor.h"

namespace BeatMate::UI::Widgets {

class TransitionEditorWidget : public juce::Component {
public:
    TransitionEditorWidget();
    ~TransitionEditorWidget() override = default;

    void setPlan(const Services::Preparation::TransitionPlan& plan);
    Services::Preparation::TransitionPlan getPlan() const;

    std::function<void(const Services::Preparation::TransitionPlan&)> onPlanChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    enum class DragTarget { None, MixOutHandle, MixInHandle };

    juce::Rectangle<float> getBarAArea() const;
    juce::Rectangle<float> getBarBArea() const;
    juce::Rectangle<float> getMixOutHandleArea() const;
    juce::Rectangle<float> getMixInHandleArea() const;

    double viewSecondsA() const;
    double viewSecondsB() const;

    float timeToXA(double seconds, juce::Rectangle<float> bar) const;
    float timeToXB(double seconds, juce::Rectangle<float> bar) const;
    double xToTimeA(float x, juce::Rectangle<float> bar) const;
    double xToTimeB(float x, juce::Rectangle<float> bar) const;

    void notifyChange();

    Services::Preparation::TransitionPlan plan_;
    DragTarget dragTarget_ = DragTarget::None;

    double trackADuration_ = 0.0;
    double trackBDuration_ = 0.0;

    juce::Slider crossfadeSlider_;
};

} // namespace BeatMate::UI::Widgets
