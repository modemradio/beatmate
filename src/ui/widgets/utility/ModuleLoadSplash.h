#pragma once


#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

namespace BeatMate::UI::Widgets {

class ModuleLoadSplash : public juce::Component, private juce::Timer {
public:
    ModuleLoadSplash(const juce::String& moduleTitle,
                     const juce::String& moduleSubtitle,
                     std::vector<juce::String> stages);
    ~ModuleLoadSplash() override;

    void setStageIndex(int idx);
    int  getStageIndex() const noexcept { return m_stageIndex.load(); }

    void setStageProgress(float p);

    void complete();
    std::function<void()> onCompleted;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintLogo(juce::Graphics& g, juce::Rectangle<float> r);
    void paintProgressBar(juce::Graphics& g, juce::Rectangle<int> r, float pct);

    juce::String              m_title;
    juce::String              m_subtitle;
    std::vector<juce::String> m_stages;
    std::atomic<int>          m_stageIndex { 0 };
    std::atomic<float>        m_stageProgress { 0.0f };
    std::atomic<bool>         m_completed { false };

    std::chrono::steady_clock::time_point m_startTime;
    float m_alpha = 0.0f;
    float m_phase = 0.0f;
    bool  m_fadingOut = false;
    float m_fadeOutAlpha = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleLoadSplash)
};

}
