#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class MiniPlayerDialog : public juce::Component {
public:
    MiniPlayerDialog();
    ~MiniPlayerDialog() override = default;
    void setTrackInfo(const juce::String& title, const juce::String& artist, const juce::String& duration);
    void setPlaying(bool playing);
    void setPosition(int pos, int total);
    void paint(juce::Graphics& g) override;
    void resized() override;
    std::function<void()> onPlayPauseClicked;
    std::function<void(int)> onPositionChanged;
private:
    std::unique_ptr<juce::Label> m_titleLabel, m_artistLabel, m_timeLabel;
    std::unique_ptr<juce::TextButton> m_playPauseBtn;
    std::unique_ptr<juce::Slider> m_positionSlider;
    bool m_playing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniPlayerDialog)
};
} // namespace BeatMate::UI
