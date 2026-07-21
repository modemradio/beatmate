#pragma once
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace BeatMate::Core { class AudioPlayer; class AudioFileReader; class StreamingPlayer; }

namespace BeatMate::UI {

class OverviewWaveformWidget;

class NowPlayingBar : public juce::Component,
                      public juce::Timer
{
public:
    NowPlayingBar();
    ~NowPlayingBar() override;

    static NowPlayingBar* instance();
    static void setInstance(NowPlayingBar* bar);

    void stopPlayback();

    void setTrackInfo(const juce::String& title, const juce::String& artist,
                      double bpm, const juce::String& key);

    void setTrackInfo(const juce::String& title, const juce::String& artist);

    void setPosition(double position);
    void setPlaying(bool playing);
    void setEnergy(int energy);

    void setAudioPlayer(Core::AudioPlayer* player);
    void setAudioFileReader(Core::AudioFileReader* reader);
    void setStreamingPlayer(Core::StreamingPlayer* streamer);

    void loadAndPlay(const juce::String& filePath);
    void loadAndPlay(const juce::String& filePath,
                     const juce::String& title,
                     const juce::String& artist);
    void loadAndPlay(const juce::String& filePath,
                     const juce::String& title,
                     const juce::String& artist,
                     double bpm,
                     const juce::String& key);

    void startHoverPreview(const juce::String& filePath);
    void stopHoverPreview();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void timerCallback() override;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void playPauseClicked() {}
    };

    void addListener(Listener* l) { m_listeners.add(l); }
    void removeListener(Listener* l) { m_listeners.remove(l); }

private:
    void setupUI();
    void drawAlbumArtPlaceholder(juce::Graphics& g, juce::Rectangle<int> area);
    void drawPlayPauseButton(juce::Graphics& g, juce::Rectangle<int> area, bool isPlaying);
    void drawBadge(juce::Graphics& g, juce::Rectangle<int> area,
                   const juce::String& text, juce::Colour bgColour, juce::Colour textColour);
    void updateTimeDisplay();
    juce::String formatTime(double seconds);
    void requestMiniWaveformFromService(const juce::String& filePath);

    std::unique_ptr<OverviewWaveformWidget> m_miniWaveform;
    std::unique_ptr<juce::Label> m_titleLabel;
    std::unique_ptr<juce::Label> m_artistLabel;

    std::unique_ptr<juce::Slider> m_seekSlider;

    std::unique_ptr<juce::Slider> m_volumeSlider;

    std::unique_ptr<juce::Label> m_timePositionLabel;
    std::unique_ptr<juce::Label> m_timeDurationLabel;

    juce::String m_bpmText = "- BPM";
    juce::String m_keyText = "-";
    juce::String m_energyText = "-";

    double m_progress = 0.0;
    double m_durationSec = 0.0;
    double m_positionSec = 0.0;
    bool m_playing = false;
    int m_energy = 5;
    bool m_seekDragging = false;

    juce::Rectangle<int> m_playBtnArea;
    juce::Rectangle<int> m_albumArtArea;
    juce::Rectangle<int> m_bpmBadgeArea;
    juce::Rectangle<int> m_keyBadgeArea;
    juce::Rectangle<int> m_energyBadgeArea;

    Core::AudioPlayer* m_audioPlayer = nullptr;
    Core::AudioFileReader* m_audioFileReader = nullptr;
    Core::StreamingPlayer* m_streamingPlayer = nullptr;
    juce::String m_currentFilePath;

    bool m_hoverPreviewActive = false;

    int m_timerDebugCounter = 0;

    juce::ListenerList<Listener> m_listeners;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NowPlayingBar)
};

}
