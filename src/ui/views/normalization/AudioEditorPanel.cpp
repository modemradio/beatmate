#include "AudioEditorPanel.h"
#include "../../styles/ColorPalette.h"
#include "../../widgets/ToastNotifier.h"
#include "../../../app/ServiceLocator.h"
#include "../../../core/audio/AudioFileReader.h"
#include "../../../core/audio/AudioFileWriter.h"
#include "../../../core/audio/AudioTrack.h"
#include "../../../core/audio/AudioPreview.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::UI {

namespace {
constexpr int kHeaderH = 56;
constexpr int kToolbarRowH = 36;
constexpr int kToolbarH = kToolbarRowH * 2 + 6;
constexpr int kFooterH = 30;
constexpr int kRulerW = 46;

struct DbTick { float db; bool major; };
const DbTick kDbTicks[] = {
    { 0.0f, true }, { -3.0f, false }, { -6.0f, true }, { -12.0f, false },
    { -18.0f, true }, { -24.0f, false }, { -36.0f, false }
};

float ampForDb(float db) { return std::pow(10.0f, db / 20.0f); }

juce::String dbText(float amp)
{
    if (amp <= 0.00001f) return juce::String::fromUTF8("-\xe2\x88\x9e dB");
    return juce::String(20.0f * std::log10(amp), 1) + " dB";
}
}

AudioEditorPanel::AudioEditorPanel()
{
    setWantsKeyboardFocus(true);

    auto mkBtn = [this](std::unique_ptr<juce::TextButton>& b, const juce::String& text,
                        juce::Colour c, std::function<void()> fn, const juce::String& tip = {}) {
        b = std::make_unique<juce::TextButton>(text);
        b->setColour(juce::TextButton::buttonColourId, c);
        b->setColour(juce::TextButton::textColourOffId,
                     c == Colors::bgLighter() ? Colors::textPrimary() : juce::Colours::white);
        if (tip.isNotEmpty()) b->setTooltip(tip);
        b->onClick = std::move(fn);
        addAndMakeVisible(*b);
    };

    mkBtn(m_backBtn, juce::String::fromUTF8("\xe2\x86\x90 Retour"), Colors::bgLighter(),
          [this] { stopPlayback(); if (onClose) onClose(); });
    mkBtn(m_playBtn, juce::String::fromUTF8("\xe2\x96\xb6 \xc3\x89""couter"), Colors::success().darker(0.2f),
          [this] { togglePlay(); },
          juce::String::fromUTF8("Lit la s\xc3\xa9lection (ou depuis le curseur). Espace."));
    mkBtn(m_cutBtn, juce::String::fromUTF8("Couper"), Colors::bgLighter(),
          [this] { opCut(); }, juce::String::fromUTF8("Supprime la s\xc3\xa9lection (Suppr)"));
    mkBtn(m_trimBtn, juce::String::fromUTF8("Rogner"), Colors::bgLighter(),
          [this] { opTrim(); }, juce::String::fromUTF8("Ne garde que la s\xc3\xa9lection"));
    mkBtn(m_silenceBtn, juce::String::fromUTF8("Silence"), Colors::bgLighter(),
          [this] { opSilence(); }, juce::String::fromUTF8("Remplace la s\xc3\xa9lection par du silence"));
    mkBtn(m_gainBtn, juce::String::fromUTF8("Gain\xe2\x80\xa6"), Colors::bgLighter(),
          [this] { opGain(); }, juce::String::fromUTF8("Applique un gain en dB \xc3\xa0 la s\xc3\xa9lection"));
    mkBtn(m_fadeInBtn, juce::String::fromUTF8("Fondu \xe2\x86\x97 \xe2\x96\xbe"), Colors::bgLighter(),
          [this] { showFadeMenu(true); },
          juce::String::fromUTF8("Fondu d'entr\xc3\xa9""e : choisissez la courbe"));
    mkBtn(m_fadeOutBtn, juce::String::fromUTF8("Fondu \xe2\x86\x98 \xe2\x96\xbe"), Colors::bgLighter(),
          [this] { showFadeMenu(false); },
          juce::String::fromUTF8("Fondu de sortie : choisissez la courbe"));
    mkBtn(m_normalizeBtn, juce::String::fromUTF8("Normaliser"), Colors::bgLighter(),
          [this] { opNormalize(); }, juce::String::fromUTF8("Cr\xc3\xaate \xc3\xa0 -0,3 dBFS"));
    mkBtn(m_reverseBtn, juce::String::fromUTF8("Inverser"), Colors::bgLighter(),
          [this] { opReverse(); }, juce::String::fromUTF8("Lecture invers\xc3\xa9""e de la s\xc3\xa9lection"));
    mkBtn(m_undoBtn, juce::String::fromUTF8("\xe2\x86\xb6 Annuler"), Colors::bgLighter(),
          [this] { applyStep(m_undo, m_redo); }, "Ctrl+Z");
    mkBtn(m_redoBtn, juce::String::fromUTF8("\xe2\x86\xb7 R\xc3\xa9tablir"), Colors::bgLighter(),
          [this] { applyStep(m_redo, m_undo); }, "Ctrl+Y");
    mkBtn(m_zoomInBtn, juce::String::fromUTF8("+"), Colors::bgLighter(),
          [this] { zoomBy(0.6); }, juce::String::fromUTF8("Zoom avant (touche +)"));
    mkBtn(m_zoomOutBtn, juce::String::fromUTF8("\xe2\x88\x92"), Colors::bgLighter(),
          [this] { zoomBy(1.6); }, juce::String::fromUTF8("Zoom arri\xc3\xa8re (touche -)"));
    mkBtn(m_zoomFitBtn, juce::String::fromUTF8("Zoom tout"), Colors::bgLighter(),
          [this] { zoomFit(); repaint(); });
    mkBtn(m_saveAsBtn, juce::String::fromUTF8("Enregistrer sous\xe2\x80\xa6"), Colors::bgLighter(),
          [this] { saveAs(); });
    mkBtn(m_saveBtn, juce::String::fromUTF8("Remplacer l'original"), Colors::primary(),
          [this] { saveReplace(); },
          juce::String::fromUTF8("L'original est d'abord sauvegard\xc3\xa9 dans audio_backups"));
}

AudioEditorPanel::~AudioEditorPanel() { stopPlayback(); }

void AudioEditorPanel::loadFile(const juce::String& path)
{
    stopPlayback();
    m_path = path;
    m_pcm.clear();
    m_undo.clear();
    m_redo.clear();
    m_selStart = m_selEnd = -1;
    m_cursor = 0;
    m_dirty = false;
    m_loading = true;
    repaint();

    juce::Component::SafePointer<AudioEditorPanel> safe(this);
    juce::Thread::launch([safe, path]
    {
        Core::AudioFileReader reader;
        auto track = reader.readFile(path.toStdString());
        std::vector<float> pcm;
        int sr = 44100, ch = 2;
        if (track && track->isLoaded())
        {
            sr = track->getSampleRate();
            ch = track->getChannels();
            pcm.resize(track->getTotalSamples());
            const float* raw = track->getRawData();
            std::copy(raw, raw + pcm.size(), pcm.begin());
        }
        juce::MessageManager::callAsync([safe, pcm = std::move(pcm), sr, ch]() mutable
        {
            if (safe == nullptr) return;
            safe->m_loading = false;
            safe->m_pcm = std::move(pcm);
            safe->m_sampleRate = sr;
            safe->m_channels = juce::jmax(1, ch);
            safe->rebuildPyramid();
            safe->zoomFit();
            safe->updateEditButtons();
            if (safe->m_pcm.empty())
                Widgets::ToastNotifier::getInstance().show(
                    juce::String::fromUTF8("\xc3\x89""diteur audio"),
                    juce::String::fromUTF8("Impossible de lire ce fichier."),
                    Widgets::ToastNotifier::Kind::Error, 6000);
            safe->repaint();
        });
    });
}

void AudioEditorPanel::rebuildPyramid()
{
    const size_t frames = numFrames();
    const size_t blocks = (frames + kBlock - 1) / kBlock;
    m_pyrMin.assign(blocks, 0.0f);
    m_pyrMax.assign(blocks, 0.0f);
    m_pyrRms.assign(blocks, 0.0f);
    for (size_t b = 0; b < blocks; ++b)
    {
        float lo = 1.0f, hi = -1.0f;
        double sq = 0.0;
        const size_t f0 = b * kBlock;
        const size_t f1 = std::min(frames, f0 + (size_t) kBlock);
        for (size_t f = f0; f < f1; ++f)
        {
            float v = 0.0f;
            for (int c = 0; c < m_channels; ++c)
                v += m_pcm[f * (size_t) m_channels + (size_t) c];
            v /= (float) m_channels;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sq += (double) v * v;
        }
        m_pyrMin[b] = lo;
        m_pyrMax[b] = hi;
        m_pyrRms[b] = f1 > f0 ? (float) std::sqrt(sq / (double) (f1 - f0)) : 0.0f;
    }
    m_lvlKeyS = m_lvlKeyE = -2;
}

void AudioEditorPanel::refreshLevels()
{
    size_t s = 0, e = 0;
    selectionRange(s, e);
    if ((int64_t) s == m_lvlKeyS && (int64_t) e == m_lvlKeyE) return;
    m_lvlKeyS = (int64_t) s;
    m_lvlKeyE = (int64_t) e;
    m_peakAmp = 0.0f;
    m_rmsAmp = 0.0f;
    if (m_pcm.empty() || e <= s) return;

    const size_t ch = (size_t) m_channels;
    double sq = 0.0;
    float pk = 0.0f;
    for (size_t i = s * ch; i < e * ch && i < m_pcm.size(); ++i)
    {
        const float a = std::abs(m_pcm[i]);
        pk = std::max(pk, a);
        sq += (double) m_pcm[i] * m_pcm[i];
    }
    const double n = (double) ((e - s) * ch);
    m_peakAmp = pk;
    m_rmsAmp = n > 0.0 ? (float) std::sqrt(sq / n) : 0.0f;
}

juce::Rectangle<int> AudioEditorPanel::waveBounds() const
{
    return getLocalBounds().reduced(16)
        .withTrimmedTop(kHeaderH + kToolbarH + 8)
        .withTrimmedBottom(kFooterH + 8)
        .withTrimmedLeft(kRulerW)
        .withTrimmedRight(kRulerW);
}

void AudioEditorPanel::zoomFit()
{
    m_viewStart = 0.0;
    const auto wb = waveBounds();
    m_framesPerPx = wb.getWidth() > 0 ? (double) numFrames() / wb.getWidth() : 0.0;
}

void AudioEditorPanel::zoomBy(double factor)
{
    if (m_pcm.empty()) return;
    const auto wb = waveBounds();
    // Ancre : centre de la sélection, sinon le curseur, sinon le centre visible.
    double anchorFrame;
    if (hasSelection())
        anchorFrame = (double) (m_selStart + m_selEnd) * 0.5;
    else if (m_cursor > 0)
        anchorFrame = (double) m_cursor;
    else
        anchorFrame = m_viewStart + m_framesPerPx * wb.getWidth() * 0.5;
    const double anchorX = (anchorFrame - m_viewStart) / juce::jmax(0.0001, m_framesPerPx);
    m_framesPerPx *= factor;
    clampView();
    m_viewStart = anchorFrame - anchorX * m_framesPerPx;
    clampView();
    repaint();
}

void AudioEditorPanel::clampView()
{
    const auto wb = waveBounds();
    if (m_framesPerPx <= 0.0) { zoomFit(); return; }
    const double minFpp = 0.05;
    const double maxFpp = wb.getWidth() > 0 ? (double) numFrames() / wb.getWidth() : 1.0;
    m_framesPerPx = juce::jlimit(minFpp, juce::jmax(minFpp, maxFpp), m_framesPerPx);
    const double visible = m_framesPerPx * wb.getWidth();
    m_viewStart = juce::jlimit(0.0, juce::jmax(0.0, (double) numFrames() - visible), m_viewStart);
}

double AudioEditorPanel::frameForX(int x) const
{
    const auto wb = waveBounds();
    return m_viewStart + (double) (x - wb.getX()) * m_framesPerPx;
}

int AudioEditorPanel::xForFrame(double frame) const
{
    const auto wb = waveBounds();
    return wb.getX() + (int) ((frame - m_viewStart) / juce::jmax(0.0001, m_framesPerPx));
}

void AudioEditorPanel::selectionRange(size_t& s, size_t& e) const
{
    if (hasSelection())
    {
        s = (size_t) juce::jlimit((int64_t) 0, (int64_t) numFrames(), m_selStart);
        e = (size_t) juce::jlimit((int64_t) 0, (int64_t) numFrames(), m_selEnd);
    }
    else { s = 0; e = numFrames(); }
}

void AudioEditorPanel::mouseDown(const juce::MouseEvent& e)
{
    if (! waveBounds().contains(e.getPosition())) return;
    stopPlayback();
    const auto f = (int64_t) juce::jlimit(0.0, (double) numFrames(), frameForX(e.x));
    m_selStart = m_selEnd = f;
    m_cursor = f;
    m_draggingSelection = true;
    repaint();
}

void AudioEditorPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (! m_draggingSelection) return;
    m_selEnd = (int64_t) juce::jlimit(0.0, (double) numFrames(), frameForX(e.x));
    repaint();
}

void AudioEditorPanel::mouseUp(const juce::MouseEvent&)
{
    if (! m_draggingSelection) return;
    m_draggingSelection = false;
    if (m_selEnd < m_selStart) std::swap(m_selStart, m_selEnd);
    if (m_selEnd - m_selStart < 32) { m_selStart = m_selEnd = -1; }  // simple clic = curseur
    repaint();
}

void AudioEditorPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! waveBounds().contains(e.getPosition())) return;
    const double anchor = frameForX(e.x);
    const double factor = w.deltaY > 0 ? 0.8 : 1.25;
    m_framesPerPx *= factor;
    clampView();
    m_viewStart = anchor - (double) (e.x - waveBounds().getX()) * m_framesPerPx;
    clampView();
    repaint();
}

bool AudioEditorPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    const auto ch = key.getTextCharacter();
    if (ch == '+' || ch == '=' || key.getKeyCode() == juce::KeyPress::numberPadAdd)
    { zoomBy(0.6); return true; }
    if (ch == '-' || key.getKeyCode() == juce::KeyPress::numberPadSubtract)
    { zoomBy(1.6); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { opCut(); return true; }
    if (key == juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 0)) { applyStep(m_undo, m_redo); return true; }
    if (key == juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 0)) { applyStep(m_redo, m_undo); return true; }
    if (key == juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        m_selStart = 0;
        m_selEnd = (int64_t) numFrames();
        repaint();
        return true;
    }
    return false;
}

void AudioEditorPanel::pushUndo(size_t startFrame, std::vector<float>&& oldData, size_t newFrames)
{
    EditStep step;
    step.startFrame = startFrame;
    step.oldData = std::move(oldData);
    step.newFrames = newFrames;
    m_undo.push_back(std::move(step));
    if (m_undo.size() > 40) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
    m_lvlKeyS = m_lvlKeyE = -2;
    updateEditButtons();
}

void AudioEditorPanel::updateEditButtons()
{
    if (m_undoBtn)
    {
        m_undoBtn->setEnabled(! m_undo.empty());
        m_undoBtn->setButtonText(m_undo.empty()
            ? juce::String::fromUTF8("\xe2\x86\xb6 Annuler")
            : juce::String::fromUTF8("\xe2\x86\xb6 Annuler (") + juce::String((int) m_undo.size()) + ")");
    }
    if (m_redoBtn) m_redoBtn->setEnabled(! m_redo.empty());
}

void AudioEditorPanel::applyStep(std::vector<EditStep>& from, std::vector<EditStep>& to)
{
    if (from.empty()) return;
    stopPlayback();
    EditStep step = std::move(from.back());
    from.pop_back();

    const size_t ch = (size_t) m_channels;
    const size_t start = step.startFrame * ch;
    const size_t curLen = step.newFrames * ch;

    EditStep inverse;
    inverse.startFrame = step.startFrame;
    inverse.newFrames = step.oldData.size() / ch;
    inverse.oldData.assign(m_pcm.begin() + (long) start, m_pcm.begin() + (long) (start + curLen));

    m_pcm.erase(m_pcm.begin() + (long) start, m_pcm.begin() + (long) (start + curLen));
    m_pcm.insert(m_pcm.begin() + (long) start, step.oldData.begin(), step.oldData.end());

    to.push_back(std::move(inverse));
    m_selStart = m_selEnd = -1;
    m_cursor = (int64_t) step.startFrame;
    rebuildPyramid();
    clampView();
    m_dirty = true;
    updateEditButtons();
    repaint();
}

void AudioEditorPanel::opCut()
{
    if (! hasSelection() || m_pcm.empty()) return;
    stopPlayback();
    size_t s, e;
    selectionRange(s, e);
    const size_t ch = (size_t) m_channels;
    std::vector<float> removed(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    m_pcm.erase(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    pushUndo(s, std::move(removed), 0);
    m_selStart = m_selEnd = -1;
    m_cursor = (int64_t) s;
    rebuildPyramid();
    clampView();
    repaint();
}

void AudioEditorPanel::opTrim()
{
    if (! hasSelection() || m_pcm.empty()) return;
    stopPlayback();
    size_t s, e;
    selectionRange(s, e);
    const size_t ch = (size_t) m_channels;
    std::vector<float> old = m_pcm;
    std::vector<float> kept(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    m_pcm = std::move(kept);
    pushUndo(0, std::move(old), numFrames());
    m_selStart = m_selEnd = -1;
    m_cursor = 0;
    rebuildPyramid();
    zoomFit();
    repaint();
}

void AudioEditorPanel::opSilence()
{
    if (! hasSelection() || m_pcm.empty()) return;
    stopPlayback();
    size_t s, e;
    selectionRange(s, e);
    const size_t ch = (size_t) m_channels;
    std::vector<float> old(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    std::fill(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch), 0.0f);
    pushUndo(s, std::move(old), e - s);
    rebuildPyramid();
    repaint();
}

void AudioEditorPanel::opGain()
{
    if (m_pcm.empty()) return;
    auto* win = new juce::AlertWindow(
        juce::String::fromUTF8("Gain"),
        juce::String::fromUTF8("Gain \xc3\xa0 appliquer (dB, n\xc3\xa9gatif = plus faible) :"),
        juce::MessageBoxIconType::NoIcon, this);
    win->addTextEditor("db", "3.0", "dB");
    win->addButton(juce::String::fromUTF8("Appliquer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    win->addButton(juce::String::fromUTF8("Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<AudioEditorPanel> safe(this);
    win->enterModalState(true, juce::ModalCallbackFunction::create([safe, win](int r)
    {
        std::unique_ptr<juce::AlertWindow> w(win);
        if (r != 1 || safe == nullptr || safe->m_pcm.empty()) return;
        const double db = w->getTextEditorContents("db").getDoubleValue();
        const float lin = (float) std::pow(10.0, db / 20.0);
        safe->stopPlayback();
        size_t s, e;
        safe->selectionRange(s, e);
        const size_t ch = (size_t) safe->m_channels;
        std::vector<float> old(safe->m_pcm.begin() + (long) (s * ch),
                               safe->m_pcm.begin() + (long) (e * ch));
        for (size_t i = s * ch; i < e * ch; ++i)
            safe->m_pcm[i] = juce::jlimit(-1.0f, 1.0f, safe->m_pcm[i] * lin);
        safe->pushUndo(s, std::move(old), e - s);
        safe->rebuildPyramid();
        safe->repaint();
    }), false);
}

void AudioEditorPanel::showFadeMenu(bool fadeIn)
{
    juce::PopupMenu m;
    m.addSectionHeader(fadeIn ? juce::String::fromUTF8("Fondu d'entr\xc3\xa9""e")
                              : juce::String::fromUTF8("Fondu de sortie"));
    m.addItem(1, juce::String::fromUTF8("Lin\xc3\xa9""aire"));
    m.addItem(2, juce::String::fromUTF8("Puissance constante (recommand\xc3\xa9)"));
    m.addItem(3, juce::String::fromUTF8("Exponentiel (d\xc3\xa9part doux)"));
    m.addItem(4, juce::String::fromUTF8("Logarithmique (d\xc3\xa9part rapide)"));
    m.addItem(5, juce::String::fromUTF8("Courbe en S (progressif)"));
    m.addItem(6, juce::String::fromUTF8("Rapide"));
    m.addItem(7, juce::String::fromUTF8("Lent"));

    juce::Component::SafePointer<AudioEditorPanel> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(
                        fadeIn ? m_fadeInBtn.get() : m_fadeOutBtn.get()),
        [safe, fadeIn](int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->applyFade(fadeIn, static_cast<FadeCurve>(r - 1));
        });
}

void AudioEditorPanel::applyFade(bool fadeIn, FadeCurve curve)
{
    if (m_pcm.empty()) return;
    stopPlayback();
    size_t s, e;
    selectionRange(s, e);
    if (e <= s) return;

    m_lastCurve = curve;
    const size_t ch = (size_t) m_channels;
    std::vector<float> old(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    const double n = (double) (e - s);

    auto shape = [curve](double x) -> double {
        x = juce::jlimit(0.0, 1.0, x);
        switch (curve)
        {
            case FadeCurve::EqualPower:  return std::sin(x * juce::MathConstants<double>::halfPi);
            case FadeCurve::Exponential: return x * x;
            case FadeCurve::Logarithmic: return std::sqrt(x);
            case FadeCurve::SCurve:      return 0.5 - 0.5 * std::cos(x * juce::MathConstants<double>::pi);
            case FadeCurve::Fast:        return std::pow(x, 0.4);
            case FadeCurve::Slow:        return std::pow(x, 2.5);
            case FadeCurve::Linear:
            default:                     return x;
        }
    };

    for (size_t f = s; f < e; ++f)
    {
        const double t = (double) (f - s) / n;
        const float g = (float) shape(fadeIn ? t : 1.0 - t);
        for (size_t c = 0; c < ch; ++c) m_pcm[f * ch + c] *= g;
    }
    pushUndo(s, std::move(old), e - s);
    rebuildPyramid();
    updateEditButtons();
    repaint();
}

void AudioEditorPanel::opNormalize()
{
    if (m_pcm.empty()) return;
    stopPlayback();
    float peak = 0.0f;
    for (float v : m_pcm) peak = std::max(peak, std::abs(v));
    if (peak < 1.0e-6f) return;
    const float target = std::pow(10.0f, -0.3f / 20.0f);   // -0,3 dBFS
    const float gain = target / peak;
    std::vector<float> old = m_pcm;
    for (auto& v : m_pcm) v *= gain;
    pushUndo(0, std::move(old), numFrames());
    rebuildPyramid();
    repaint();
    Widgets::ToastNotifier::getInstance().show(
        juce::String::fromUTF8("Normalis\xc3\xa9"),
        juce::String::fromUTF8("Cr\xc3\xaate amen\xc3\xa9""e \xc3\xa0 -0,3 dBFS (gain ")
            + juce::String(20.0f * std::log10(gain), 1) + " dB)",
        Widgets::ToastNotifier::Kind::Success, 4000);
}

void AudioEditorPanel::opReverse()
{
    if (m_pcm.empty()) return;
    stopPlayback();
    size_t s, e;
    selectionRange(s, e);
    if (e <= s) return;
    const size_t ch = (size_t) m_channels;
    std::vector<float> old(m_pcm.begin() + (long) (s * ch), m_pcm.begin() + (long) (e * ch));
    for (size_t i = 0, j = (e - s) - 1; i < j; ++i, --j)
        for (size_t c = 0; c < ch; ++c)
            std::swap(m_pcm[(s + i) * ch + c], m_pcm[(s + j) * ch + c]);
    pushUndo(s, std::move(old), e - s);
    rebuildPyramid();
    repaint();
}

void AudioEditorPanel::togglePlay()
{
    if (m_playing) { stopPlayback(); return; }
    if (m_pcm.empty()) return;

    size_t s, e;
    if (hasSelection()) selectionRange(s, e);
    else { s = (size_t) juce::jlimit((int64_t) 0, (int64_t) numFrames(), m_cursor); e = numFrames(); }
    if (e <= s) return;

    // Écrit la zone dans un WAV temporaire puis la joue via AudioPreview.
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("beatmate_editor_preview.wav");
    Core::AudioTrack seg;
    const size_t ch = (size_t) m_channels;
    seg.loadData(m_pcm.data() + s * ch, (e - s) * ch, m_sampleRate, m_channels);
    Core::AudioFileWriter writer;
    if (! writer.writeWAV(seg, tmp.getFullPathName().toStdString()))
        return;

    auto* preview = g_serviceLocator ? g_serviceLocator->tryGet<Core::AudioPreview>() : nullptr;
    if (! preview) return;
    const double lenSec = (double) (e - s) / (double) m_sampleRate;
    preview->previewTrack(tmp.getFullPathName().toStdString(), 0.0, lenSec);
    m_playing = true;
    m_playStartSec = (double) s / (double) m_sampleRate;
    m_playLenSec = lenSec;
    m_playAnchorMs = juce::Time::getCurrentTime().toMilliseconds();
    m_playBtn->setButtonText(juce::String::fromUTF8("\xe2\x96\xa0 Stop"));
    startTimerHz(30);
}

void AudioEditorPanel::stopPlayback()
{
    if (! m_playing) return;
    m_playing = false;
    stopTimer();
    if (auto* preview = g_serviceLocator ? g_serviceLocator->tryGet<Core::AudioPreview>() : nullptr)
        preview->stop();
    if (m_playBtn) m_playBtn->setButtonText(juce::String::fromUTF8("\xe2\x96\xb6 \xc3\x89""couter"));
    repaint();
}

void AudioEditorPanel::timerCallback()
{
    if (! m_playing) return;
    const double elapsed = (double) (juce::Time::getCurrentTime().toMilliseconds() - m_playAnchorMs) / 1000.0;
    if (elapsed >= m_playLenSec) { stopPlayback(); return; }
    repaint(waveBounds());
}

bool AudioEditorPanel::writeTo(const juce::File& dest)
{
    Core::AudioTrack track;
    track.loadData(m_pcm.data(), m_pcm.size(), m_sampleRate, m_channels);
    Core::AudioFileWriter writer;
    Core::WriteOptions opts;
    opts.bitRate = 320;
    opts.bitDepth = 24;
    return writer.writeFile(track, dest.getFullPathName().toStdString(), opts);
}

void AudioEditorPanel::saveAs()
{
    if (m_pcm.empty()) return;
    juce::File src(m_path);
    m_chooser = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Enregistrer sous"),
        src.getSiblingFile(src.getFileNameWithoutExtension() + " (edit)" + src.getFileExtension()),
        "*.wav;*.mp3;*.flac;*.ogg");
    juce::Component::SafePointer<AudioEditorPanel> safe(this);
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File() || safe == nullptr) return;
            const bool ok = safe->writeTo(f);
            Widgets::ToastNotifier::getInstance().show(
                ok ? juce::String::fromUTF8("Fichier enregistr\xc3\xa9")
                   : juce::String::fromUTF8("\xc3\x89""chec de l'enregistrement"),
                f.getFileName(),
                ok ? Widgets::ToastNotifier::Kind::Success : Widgets::ToastNotifier::Kind::Error, 5000);
            if (ok)
            {
                safe->m_dirty = false;
                if (safe->onFileSaved) safe->onFileSaved(f.getFullPathName());
            }
        });
}

void AudioEditorPanel::saveReplace()
{
    if (m_pcm.empty() || m_path.isEmpty()) return;
    juce::File src(m_path);

    juce::Component::SafePointer<AudioEditorPanel> safe(this);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon,
        juce::String::fromUTF8("Remplacer l'original"),
        juce::String::fromUTF8("Le fichier original sera d'abord copi\xc3\xa9 dans audio_backups, "
                               "puis remplac\xc3\xa9 par la version \xc3\xa9""dit\xc3\xa9""e. Continuer ?"),
        juce::String::fromUTF8("Remplacer"), juce::String::fromUTF8("Annuler"), nullptr,
        juce::ModalCallbackFunction::create([safe, src](int r)
        {
            if (r != 1 || safe == nullptr) return;
            auto backupDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("BeatMate").getChildFile("audio_backups");
            backupDir.createDirectory();
            auto backup = backupDir.getChildFile(
                juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + "_" + src.getFileName());
            if (! src.copyFileTo(backup))
            {
                Widgets::ToastNotifier::getInstance().show(
                    juce::String::fromUTF8("\xc3\x89""chec"),
                    juce::String::fromUTF8("Impossible de sauvegarder l'original."),
                    Widgets::ToastNotifier::Kind::Error, 6000);
                return;
            }
            const bool ok = safe->writeTo(src);
            Widgets::ToastNotifier::getInstance().show(
                ok ? juce::String::fromUTF8("Original remplac\xc3\xa9")
                   : juce::String::fromUTF8("\xc3\x89""chec de l'enregistrement"),
                ok ? juce::String::fromUTF8("Sauvegarde : ") + backup.getFileName() : src.getFileName(),
                ok ? Widgets::ToastNotifier::Kind::Success : Widgets::ToastNotifier::Kind::Error, 7000);
            if (ok)
            {
                safe->m_dirty = false;
                if (safe->onFileSaved) safe->onFileSaved(src.getFullPathName());
            }
        }));
}

juce::String AudioEditorPanel::fmtTime(double seconds) const
{
    const int mins = (int) (seconds / 60.0);
    const double secs = seconds - mins * 60.0;
    return juce::String(mins) + ":" + juce::String(secs, 2).paddedLeft('0', 5);
}

void AudioEditorPanel::paint(juce::Graphics& g)
{
    g.fillAll(Colors::bgDark());

    juce::File f(m_path);
    g.setColour(Colors::textPrimary());
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8("\xc3\x89""diteur audio")
               + (m_dirty ? juce::String::fromUTF8("  \xe2\x97\x8f") : juce::String()),
               20, 10, 400, 24, juce::Justification::centredLeft);
    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.5f));
    juce::String sub = f.getFileName();
    if (! m_pcm.empty())
        sub << "   \xc2\xb7   " << fmtTime((double) numFrames() / m_sampleRate)
            << "   \xc2\xb7   " << m_sampleRate << " Hz \xc2\xb7 "
            << (m_channels == 1 ? juce::String::fromUTF8("mono") : juce::String::fromUTF8("st\xc3\xa9r\xc3\xa9o"));
    g.drawText(sub, 20, 34, getWidth() - 200, 18, juce::Justification::centredLeft, true);

    const auto wb = waveBounds();
    const auto card = wb.expanded(kRulerW, 0);
    g.setColour(Colors::bgCard());
    g.fillRoundedRectangle(card.toFloat(), 8.0f);

    if (m_loading)
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(14.0f));
        g.drawText(juce::String::fromUTF8("Chargement du fichier\xe2\x80\xa6"), card, juce::Justification::centred);
        return;
    }
    if (m_pcm.empty())
    {
        g.setColour(Colors::textMuted());
        g.setFont(juce::Font(14.0f));
        g.drawText(juce::String::fromUTF8("Aucun fichier charg\xc3\xa9."), card, juce::Justification::centred);
        return;
    }

    if (hasSelection())
    {
        const int x0 = juce::jmax(wb.getX(), xForFrame((double) std::min(m_selStart, m_selEnd)));
        const int x1 = juce::jmin(wb.getRight(), xForFrame((double) std::max(m_selStart, m_selEnd)));
        g.setColour(Colors::primary().withAlpha(0.18f));
        g.fillRect(x0, wb.getY(), juce::jmax(1, x1 - x0), wb.getHeight());
        g.setColour(Colors::primary().withAlpha(0.6f));
        g.drawVerticalLine(x0, (float) wb.getY(), (float) wb.getBottom());
        g.drawVerticalLine(x1, (float) wb.getY(), (float) wb.getBottom());
    }

    const float cy = (float) wb.getCentreY();
    const float halfH = wb.getHeight() * 0.42f;
    const float clipY = ampForDb(-1.0f) * halfH;

    g.setColour(juce::Colours::red.withAlpha(0.07f));
    g.fillRect((float) wb.getX(), cy - halfH, (float) wb.getWidth(), halfH - clipY);
    g.fillRect((float) wb.getX(), cy + clipY, (float) wb.getWidth(), halfH - clipY);

    for (const auto& t : kDbTicks)
    {
        const float a = ampForDb(t.db) * halfH;
        g.setColour(juce::Colours::white.withAlpha(t.major ? 0.09f : 0.045f));
        g.drawHorizontalLine((int) (cy - a), (float) wb.getX(), (float) wb.getRight());
        g.drawHorizontalLine((int) (cy + a), (float) wb.getX(), (float) wb.getRight());
    }

    {
        juce::Graphics::ScopedSaveState clipSave(g);
        g.reduceClipRegion(wb.reduced(1));
        for (int px = 0; px < wb.getWidth(); ++px)
        {
            const double f0 = m_viewStart + px * m_framesPerPx;
            const double f1 = f0 + m_framesPerPx;
            const size_t b0 = (size_t) juce::jmax(0.0, f0 / kBlock);
            const size_t b1 = juce::jmin(m_pyrMin.size(),
                                         (size_t) juce::jmax(0.0, f1 / kBlock) + 1);
            if (b0 >= m_pyrMin.size()) break;
            float lo = 1.0f, hi = -1.0f, rms = 0.0f;
            for (size_t b = b0; b < b1; ++b)
            {
                lo = std::min(lo, m_pyrMin[b]);
                hi = std::max(hi, m_pyrMax[b]);
                rms = std::max(rms, m_pyrRms[b]);
            }
            if (hi < lo) continue;
            const float x = (float) (wb.getX() + px);
            const float y0 = cy - hi * halfH;
            const float y1 = cy - lo * halfH;
            const bool hot = std::max(hi, -lo) > ampForDb(-1.0f);
            g.setColour(hot ? Colors::error().withAlpha(0.9f) : Colors::accent().withAlpha(0.55f));
            g.fillRect(x, y0, 1.0f, juce::jmax(1.0f, y1 - y0));
            const float r = rms * halfH;
            g.setColour(hot ? Colors::error() : Colors::accent());
            g.fillRect(x, cy - r, 1.0f, juce::jmax(1.0f, r * 2.0f));
        }
    }

    g.setColour(Colors::glassBorder());
    g.drawHorizontalLine((int) cy, (float) wb.getX(), (float) wb.getRight());

    g.setFont(juce::Font(10.0f));
    for (const auto& t : kDbTicks)
    {
        const float a = ampForDb(t.db) * halfH;
        const juce::String label = juce::String((int) t.db);
        g.setColour(t.db >= -1.0f ? Colors::error().withAlpha(0.85f)
                                  : Colors::textMuted().withAlpha(t.major ? 0.95f : 0.6f));
        for (float sign : { -1.0f, 1.0f })
        {
            const int y = (int) (cy + sign * a) - 6;
            g.drawText(label, wb.getX() - kRulerW + 4, y, kRulerW - 8, 12, juce::Justification::centredRight);
            g.drawText(label, wb.getRight() + 4, y, kRulerW - 8, 12, juce::Justification::centredLeft);
        }
    }
    g.setColour(Colors::textMuted().withAlpha(0.5f));
    g.drawText("dB", wb.getX() - kRulerW + 4, wb.getBottom() - 14, kRulerW - 8, 12,
               juce::Justification::centredRight);
    g.drawText("dB", wb.getRight() + 4, wb.getBottom() - 14, kRulerW - 8, 12,
               juce::Justification::centredLeft);

    double cursorFrame = (double) m_cursor;
    if (m_playing)
    {
        const double elapsed = (double) (juce::Time::getCurrentTime().toMilliseconds() - m_playAnchorMs) / 1000.0;
        cursorFrame = (m_playStartSec + elapsed) * m_sampleRate;
    }
    const int cx = xForFrame(cursorFrame);
    if (cx >= wb.getX() && cx <= wb.getRight())
    {
        g.setColour(m_playing ? Colors::success() : Colors::textPrimary());
        g.drawVerticalLine(cx, (float) wb.getY(), (float) wb.getBottom());
    }

    refreshLevels();

    g.setColour(Colors::textSecondary());
    g.setFont(juce::Font(12.0f));
    juce::String info;
    if (hasSelection())
    {
        const double s = (double) std::min(m_selStart, m_selEnd) / m_sampleRate;
        const double e = (double) std::max(m_selStart, m_selEnd) / m_sampleRate;
        info << juce::String::fromUTF8("S\xc3\xa9lection : ") << fmtTime(s) << "  \xe2\x86\x92  " << fmtTime(e)
             << "   (" << fmtTime(e - s) << ")";
    }
    else
    {
        info << juce::String::fromUTF8("Curseur : ") << fmtTime((double) m_cursor / m_sampleRate)
             << juce::String::fromUTF8("   \xc2\xb7   Glissez pour s\xc3\xa9lectionner, molette pour zoomer");
    }
    g.drawText(info, card.getX(), card.getBottom() + 6, card.getWidth() - 340, 20,
               juce::Justification::centredLeft);

    juce::String levels;
    levels << juce::String::fromUTF8("Cr\xc3\xaate ") << dbText(m_peakAmp)
           << juce::String::fromUTF8("   \xc2\xb7   RMS ") << dbText(m_rmsAmp);
    const float peakDb = m_peakAmp > 0.00001f ? 20.0f * std::log10(m_peakAmp) : -120.0f;
    if (peakDb > -1.0f)
    {
        g.setColour(Colors::error());
        levels << juce::String::fromUTF8("   \xc2\xb7   trop fort (\xc3\xa9""cr\xc3\xaatage possible)");
    }
    else if (peakDb < -12.0f)
    {
        g.setColour(Colors::warning());
        levels << juce::String::fromUTF8("   \xc2\xb7   niveau faible");
    }
    else
    {
        g.setColour(Colors::success());
        levels << juce::String::fromUTF8("   \xc2\xb7   niveau correct");
    }
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(levels, card.getRight() - 340, card.getBottom() + 6, 340, 20,
               juce::Justification::centredRight);
}

void AudioEditorPanel::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto header = area.removeFromTop(kHeaderH);
    m_backBtn->setBounds(header.removeFromRight(110).withTrimmedTop(10).withHeight(30));

    auto bar = area.removeFromTop(kToolbarH);
    auto row1 = bar.removeFromTop(kToolbarRowH);
    bar.removeFromTop(6);
    auto row2 = bar.removeFromTop(kToolbarRowH);

    auto putL = [](juce::Rectangle<int>& r, std::unique_ptr<juce::TextButton>& b, int w) {
        if (b) { b->setBounds(r.removeFromLeft(w).reduced(0, 3)); r.removeFromLeft(6); }
    };
    auto putR = [](juce::Rectangle<int>& r, std::unique_ptr<juce::TextButton>& b, int w) {
        if (b) { b->setBounds(r.removeFromRight(w).reduced(0, 3)); r.removeFromRight(6); }
    };

    putL(row1, m_playBtn, 110);
    putL(row1, m_cutBtn, 80);
    putL(row1, m_trimBtn, 80);
    putL(row1, m_silenceBtn, 80);
    putL(row1, m_gainBtn, 80);
    putL(row1, m_fadeInBtn, 108);
    putL(row1, m_fadeOutBtn, 108);
    putL(row1, m_normalizeBtn, 100);
    putL(row1, m_reverseBtn, 84);

    putL(row2, m_undoBtn, 120);
    putL(row2, m_redoBtn, 105);
    putR(row2, m_saveBtn, 170);
    putR(row2, m_saveAsBtn, 155);
    putR(row2, m_zoomFitBtn, 100);
    putR(row2, m_zoomInBtn, 42);
    putR(row2, m_zoomOutBtn, 42);

    updateEditButtons();
    clampView();
}

} // namespace BeatMate::UI
