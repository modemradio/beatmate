#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace BeatMate::UI {

class MiniWaveformInline : public juce::Component
{
public:
    MiniWaveformInline();
    ~MiniWaveformInline() override = default;

    void setWaveformData(const std::vector<float>& low,
                         const std::vector<float>& mid,
                         const std::vector<float>& high);

    void paint(juce::Graphics& g) override;

private:
    std::vector<float> lowData_;
    std::vector<float> midData_;
    std::vector<float> highData_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniWaveformInline)
};

} // namespace BeatMate::UI
