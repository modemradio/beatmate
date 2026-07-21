#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

class GlassmorphicCard : public juce::Component
{
public:
    GlassmorphicCard();
    ~GlassmorphicCard() override = default;

    void setTitle(const juce::String& title);
    void addContent(juce::Component* content);
    void setCornerRadius(float radius);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String title_;
    float cornerRadius_ = 16.0f;
    std::vector<juce::Component*> contentComponents_;

    static constexpr int headerHeight_ = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlassmorphicCard)
};

} // namespace BeatMate::UI
