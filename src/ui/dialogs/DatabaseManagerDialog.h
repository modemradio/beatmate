#pragma once
#include <memory>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>
namespace BeatMate::UI {
class DatabaseManagerDialog : public juce::Component {
public:
    DatabaseManagerDialog();
    ~DatabaseManagerDialog() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;
    std::function<void()> onCleanupRequested;
    std::function<void()> onOptimizeRequested;
    std::function<void()> onRepairRequested;
private:
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_dbSizeLabel, m_trackCountLabel, m_playlistCountLabel, m_cueCountLabel;
    std::unique_ptr<juce::Label> m_dbSizeVal, m_trackCountVal, m_playlistCountVal, m_cueCountVal;
    std::unique_ptr<juce::TextButton> m_cleanBtn, m_optimizeBtn, m_repairBtn, m_closeBtn;
    std::unique_ptr<juce::Label> m_statusLabel;
    double m_progress = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DatabaseManagerDialog)
};
} // namespace BeatMate::UI
