#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

class EnergyArcWidget : public juce::Component
{
public:
    EnergyArcWidget();
    ~EnergyArcWidget() override = default;

    void setEnergy(float energy); // 0.0 - 10.0
    float getEnergy() const { return energy_; }

    void paint(juce::Graphics& g) override;

private:
    float energy_ = 0.0f;

    juce::Colour getEnergyColour(float normalisedValue) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnergyArcWidget)
};

} // namespace BeatMate::UI
