#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

namespace BeatMate::UI {

// Éditeur audio façon WavePad : waveform zoomable, sélection à la souris,
class AudioEditorPanel : public juce::Component,
                         private juce::Timer
{
public:
    AudioEditorPanel();
    ~AudioEditorPanel() override;

    void loadFile(const juce::String& path);

    std::function<void()> onClose;
    std::function<void(const juce::String& savedPath)> onFileSaved;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    std::vector<float> m_pcm;
    int m_sampleRate = 44100;
    int m_channels = 2;
    juce::String m_path;
    bool m_loading = false;
    bool m_dirty = false;

    static constexpr int kBlock = 256;   // frames par bloc de pyramide
    std::vector<float> m_pyrMin, m_pyrMax;
    std::vector<float> m_pyrRms;
    void rebuildPyramid();

    float m_peakAmp = 0.0f, m_rmsAmp = 0.0f;
    int64_t m_lvlKeyS = -2, m_lvlKeyE = -2;
    void refreshLevels();

    size_t numFrames() const { return m_channels > 0 ? m_pcm.size() / (size_t) m_channels : 0; }

    double m_viewStart = 0.0;    // frame de départ affichée
    double m_framesPerPx = 0.0;  // zoom (0 = fit)
    int64_t m_selStart = -1, m_selEnd = -1;   // sélection normalisée start<end
    int64_t m_cursor = 0;
    bool m_draggingSelection = false;

    juce::Rectangle<int> waveBounds() const;
    double frameForX(int x) const;
    int xForFrame(double frame) const;
    void clampView();
    void zoomFit();
    void zoomBy(double factor);   // <1 = zoom avant, >1 = zoom arrière
    bool hasSelection() const { return m_selStart >= 0 && m_selEnd > m_selStart; }
    void selectionRange(size_t& s, size_t& e) const;   // sélection ou tout

    struct EditStep {
        size_t startFrame = 0;
        std::vector<float> oldData;   // ce qui était là (entrelacé)
        size_t newFrames = 0;         // longueur actuelle de la zone remplacée
    };
    std::vector<EditStep> m_undo, m_redo;
    void pushUndo(size_t startFrame, std::vector<float>&& oldData, size_t newFrames);
    void applyStep(std::vector<EditStep>& from, std::vector<EditStep>& to);

    void opCut();
    void opTrim();
    void opSilence();
    void opGain();
    void opNormalize();
    void opReverse();

    enum class FadeCurve { Linear = 0, EqualPower, Exponential, Logarithmic, SCurve, Fast, Slow };
    void applyFade(bool fadeIn, FadeCurve curve);
    void showFadeMenu(bool fadeIn);
    FadeCurve m_lastCurve = FadeCurve::EqualPower;
    void updateEditButtons();

    bool m_playing = false;
    double m_playStartSec = 0.0;
    juce::int64 m_playAnchorMs = 0;
    double m_playLenSec = 0.0;
    void togglePlay();
    void stopPlayback();
    void timerCallback() override;

    void saveAs();
    void saveReplace();
    bool writeTo(const juce::File& dest);

    juce::String fmtTime(double seconds) const;

    std::unique_ptr<juce::TextButton> m_backBtn, m_playBtn,
        m_cutBtn, m_trimBtn, m_silenceBtn, m_gainBtn, m_fadeInBtn, m_fadeOutBtn,
        m_normalizeBtn, m_reverseBtn, m_undoBtn, m_redoBtn,
        m_zoomInBtn, m_zoomOutBtn, m_zoomFitBtn, m_saveAsBtn, m_saveBtn;
    std::unique_ptr<juce::FileChooser> m_chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEditorPanel)
};

} // namespace BeatMate::UI
