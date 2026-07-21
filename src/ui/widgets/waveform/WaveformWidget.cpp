#include <algorithm>
#include <cmath>
#include "WaveformWidget.h"
#include "../../styles/ColorPalette.h"
#include <spdlog/spdlog.h>
namespace BeatMate::UI {

namespace {
juce::Font flagFont() { return juce::Font("Segoe UI", 10.0f, juce::Font::bold); }
constexpr float kFlagH = 18.0f;
constexpr float kFlagY = 2.0f;
constexpr float kTimelineH = 14.0f;

juce::String formatRulerTime(double seconds)
{
    int mins = static_cast<int>(seconds / 60.0);
    int secs = static_cast<int>(seconds) % 60;
    return juce::String::formatted("%d:%02d", mins, secs);
}

juce::String formatPreciseTime(double seconds)
{
    int mins = static_cast<int>(seconds / 60.0);
    double rem = seconds - mins * 60.0;
    int wholeSecs = static_cast<int>(rem);
    int millis = static_cast<int>((rem - wholeSecs) * 1000.0);
    return juce::String::formatted("%02d:%02d.%03d", mins, wholeSecs, millis);
}
}

WaveformWidget::WaveformWidget(){
    setOpaque(true);
}
WaveformWidget::~WaveformWidget(){stopTimer();}
void WaveformWidget::setWaveformData(const std::vector<float>& peaks){
    m_peaks=peaks;m_useColoredWaveform=false;m_cacheValid=false;
    m_ampMax=0.001f;for(float p:m_peaks)m_ampMax=std::max(m_ampMax,std::abs(p));
    repaint();
}
void WaveformWidget::setColoredWaveformData(const std::vector<float>& bass,const std::vector<float>& mid,const std::vector<float>& treble){
    m_bassPeaks=bass;m_midPeaks=mid;m_treblePeaks=treble;m_useColoredWaveform=true;m_cacheValid=false;
    m_ampMax=0.001f;for(float p:m_peaks)m_ampMax=std::max(m_ampMax,std::abs(p));
    repaint();
}
void WaveformWidget::setPlayheadPosition(double position){
    double newPos=juce::jlimit(0.0,1.0,position);
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    int oldPixel = (ve > vs && getWidth() > 0) ? static_cast<int>((m_playheadPos - vs) / (ve - vs) * getWidth()) : -1;
    const double delta = newPos - m_playheadPos;
    m_playheadPos = newPos;
    if (m_zoom > 1.0 && !m_playing && m_draggingCueIndex < 0
        && delta > 1.0e-7 && delta < 0.02)
    {
        const double vis = 1.0 / m_zoom;
        m_followFrac = juce::jlimit(0.0, 0.5, (m_playheadPos - m_scrollOffset) / vis);
        m_playing = true;
        spdlog::info("[Waveform] auto-arm follow: playhead={:.4f} scroll={:.4f} frac={:.2f}",
                     m_playheadPos, m_scrollOffset, m_followFrac);
        startTimerHz(30);
    }
    if (m_zoom > 1.0 && m_playing && m_draggingCueIndex < 0) {
        double visWidth = 1.0 / m_zoom;
        double targetOffset = m_playheadPos - visWidth * m_followFrac;
        double newScroll = juce::jlimit(0.0, 1.0 - visWidth, targetOffset);
        if (std::abs(newScroll - m_scrollOffset) > 0.00001) {
            m_scrollOffset = newScroll;
            repaint();
            return;
        }
    } else if (m_zoom > 1.0) {
        double visWidth = 1.0 / m_zoom;
        if (m_playheadPos < m_scrollOffset || m_playheadPos > m_scrollOffset + visWidth) {
            spdlog::info("[Waveform] view jump (not playing): playhead={:.4f} scroll={:.4f}",
                         m_playheadPos, m_scrollOffset);
            m_scrollOffset = juce::jlimit(0.0, 1.0 - visWidth, m_playheadPos - visWidth * 0.3);
            repaint();
            return;
        }
    }
    vs = m_scrollOffset;
    ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    int newPixel = (ve > vs && getWidth() > 0) ? static_cast<int>((m_playheadPos - vs) / (ve - vs) * getWidth()) : -1;
    if (newPixel != oldPixel)
        repaint();
}
void WaveformWidget::setCuePoints(const std::vector<CuePoint>& cues){m_cuePoints=cues;m_hoveredCueIndex=-1;repaint();}
void WaveformWidget::setSections(const std::vector<Section>& sections){m_sections=sections;m_cacheValid=false;repaint();}
void WaveformWidget::setBeatGrid(const std::vector<double>& beats){m_beatGrid=beats;m_cacheValid=false;repaint();}
void WaveformWidget::setDuration(double seconds){
    if (std::abs(seconds - m_duration) < 0.01) return;
    m_duration = seconds;
    m_cacheValid = false;
    repaint();
}
void WaveformWidget::setZoom(double z){m_zoom=juce::jlimit(1.0,100.0,z);m_scrollOffset=juce::jlimit(0.0,std::max(0.0,1.0-1.0/m_zoom),m_scrollOffset);m_cacheValid=false;m_listeners.call([this](Listener&l){l.zoomChanged(m_zoom);});repaint();}
void WaveformWidget::setScrollOffset(double o){m_scrollOffset=juce::jlimit(0.0,std::max(0.0,1.0-1.0/m_zoom),o);repaint();}
void WaveformWidget::setPlaying(bool p){
    if (p != m_playing)
        spdlog::info("[Waveform] setPlaying({}) playhead={:.4f} scroll={:.4f} zoom={:.1f}",
                     p, m_playheadPos, m_scrollOffset, m_zoom);
    if (p && !m_playing && m_zoom > 1.0) {
        const double vis = 1.0 / m_zoom;
        m_followFrac = juce::jlimit(0.0, 0.5, (m_playheadPos - m_scrollOffset) / vis);
    }
    m_playing = p;
    if (p) startTimerHz(30); else stopTimer();
}
void WaveformWidget::timerCallback(){if(m_playing)repaint();}

void WaveformWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto boundsF = bounds.toFloat();
    int bw = std::max(1, bounds.getWidth());
    int bh = std::max(1, bounds.getHeight());
    const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int pw = std::max(1, static_cast<int>(std::ceil(bw * scale)));
    const int ph = std::max(1, static_cast<int>(std::ceil(bh * scale)));

    const double vis = 1.0 / m_zoom;
    const double span = std::min(1.0, vis * 3.0);
    const int spanFactor = std::max(1, static_cast<int>(std::round(span / vis)));
    const int cachePw = std::max(1, pw * spanFactor);
    const bool rangeOk = m_scrollOffset >= m_cacheStart - 1.0e-9
                      && m_scrollOffset + vis <= m_cacheStart + m_cacheSpanNorm + 1.0e-9;

    if (!m_cacheValid || m_waveformCache.isNull()
        || m_waveformCache.getWidth() != cachePw
        || m_waveformCache.getHeight() != ph
        || std::abs(m_cacheScale - scale) > 0.001f
        || !rangeOk)
    {
        m_cacheStart = juce::jlimit(0.0, std::max(0.0, 1.0 - span), m_scrollOffset - vis);
        m_cacheSpanNorm = span;

        m_waveformCache = juce::Image(juce::Image::ARGB, cachePw, ph, false);
        juce::Graphics cg(m_waveformCache);
        cg.addTransform(juce::AffineTransform::scale(scale));

        const int cacheWLogical = bw * spanFactor;
        cg.setColour(Colors::bg());
        cg.fillRect(0.0f, 0.0f, static_cast<float>(cacheWLogical), boundsF.getHeight());

        const double savedScroll = m_scrollOffset;
        const double savedZoom = m_zoom;
        m_scrollOffset = m_cacheStart;
        m_zoom = 1.0 / span;
        m_cacheWidthLogical = cacheWLogical;

        if (m_useColoredWaveform && !m_bassPeaks.empty())
            drawColoredWaveform(cg);
        else if (!m_peaks.empty())
            drawWaveform(cg);

        m_cacheWidthLogical = 0;
        m_scrollOffset = savedScroll;
        m_zoom = savedZoom;

        m_cacheScale = scale;
        m_cacheValid = true;
    }

    {
        const int imgW = m_waveformCache.getWidth();
        const double rel = (m_scrollOffset - m_cacheStart) / m_cacheSpanNorm;
        int srcX = static_cast<int>(std::round(rel * imgW));
        int srcW = static_cast<int>(std::round((vis / m_cacheSpanNorm) * imgW));
        srcX = juce::jlimit(0, std::max(0, imgW - 1), srcX);
        srcW = juce::jlimit(1, imgW - srcX, srcW);
        g.drawImage(m_waveformCache, 0, 0, bw, bh, srcX, 0, srcW, m_waveformCache.getHeight());
    }

    drawSections(g);
    drawBeatGrid(g);

    {
        float midY = boundsF.getCentreY();
        g.setColour(juce::Colour(0x20FFFFFF));
        g.fillRect(0.0f, midY - 0.5f, boundsF.getWidth(), 1.0f);
    }

    drawSelection(g);
    drawTimeline(g);

    g.setColour(Colors::border());
    g.drawRoundedRectangle(boundsF.reduced(0.5f), 4.0f, 1.0f);

    drawCuePoints(g);
    drawPlayhead(g);
}

void WaveformWidget::drawBackground(juce::Graphics& g)
{
    int w = getWidth(), h = getHeight();
    float wf = static_cast<float>(w), hf = static_cast<float>(h);

    juce::ColourGradient bg(
        juce::Colour(0xFF0C0C18), 0.0f, 0.0f,
        juce::Colour(0xFF060610), 0.0f, hf, false);
    g.setGradientFill(bg);
    g.fillRect(0, 0, w, h);

    g.setColour(juce::Colour(0x08FFFFFF));
    for (int i = 1; i < 4; ++i) {
        float y = hf * i / 4.0f;
        g.fillRect(0.0f, y, wf, 1.0f);
    }
}

void WaveformWidget::drawWaveform(juce::Graphics& g)
{
    if (m_peaks.empty()) return;

    int w = m_cacheWidthLogical > 0 ? m_cacheWidthLogical : getWidth();
    int h = getHeight();
    float midY = static_cast<float>(h) * 0.5f;
    float halfH = midY - 2.0f;

    double visStart = m_scrollOffset;
    double visEnd = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    size_t dataSize = m_peaks.size();
    int startSample = static_cast<int>(visStart * static_cast<double>(dataSize));
    int endSample = static_cast<int>(visEnd * static_cast<double>(dataSize));
    int range = endSample - startSample;
    if (range <= 0) return;

    float xStep = static_cast<float>(w) / static_cast<float>(range);

    float barW, gap;
    if (xStep <= 1.5f) {
        barW = std::max(1.0f, xStep);
        gap = 0.0f;
    } else if (xStep <= 4.0f) {
        barW = xStep - 1.0f;
        gap = 1.0f;
    } else {
        barW = std::max(2.0f, xStep * 0.7f);
        gap = xStep - barW;
    }

    for (int i = 0; i < range; ++i)
    {
        int idx = juce::jlimit(0, static_cast<int>(dataSize) - 1, startSample + i);
        float peak = std::abs(m_peaks[static_cast<size_t>(idx)]);
        float x = static_cast<float>(i) * xStep + gap * 0.5f;

        if (peak < 0.003f) continue;

        float peakH = peak * halfH;

        float t = std::min(peak * 1.2f, 1.0f);
        juce::Colour barColor;
        if (t < 0.33f)
            barColor = juce::Colour(0xFF0D2847).interpolatedWith(juce::Colour(0xFF1565C0), t / 0.33f);
        else if (t < 0.66f)
            barColor = juce::Colour(0xFF1565C0).interpolatedWith(juce::Colour(0xFF00B0FF), (t - 0.33f) / 0.33f);
        else
            barColor = juce::Colour(0xFF00B0FF).interpolatedWith(juce::Colour(0xFFE0F7FA), (t - 0.66f) / 0.34f);

        g.setColour(barColor);
        g.fillRect(x, midY - peakH, barW, peakH);

        g.setColour(barColor.withMultipliedBrightness(0.6f));
        g.fillRect(x, midY, barW, peakH);

        if (peakH > 3.0f) {
            g.setColour(barColor.brighter(0.5f).withAlpha(0.9f));
            g.fillRect(x, midY - peakH, barW, 1.0f);
            g.fillRect(x, midY + peakH - 1.0f, barW, 1.0f);
        }
    }

    g.setColour(juce::Colour(0x18FFFFFF));
    g.fillRect(0.0f, midY - 0.5f, static_cast<float>(w), 1.0f);
}

void WaveformWidget::drawColoredWaveform(juce::Graphics& g)
{
    int w = m_cacheWidthLogical > 0 ? m_cacheWidthLogical : getWidth();
    int h = getHeight();
    float midY = static_cast<float>(h) * 0.5f;
    float halfH = midY - 2.0f;

    double visStart = m_scrollOffset;
    double visEnd = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);

    size_t dataSize = m_bassPeaks.size();
    if (dataSize == 0) return;

    int startSample = static_cast<int>(visStart * static_cast<double>(dataSize));
    int endSample = static_cast<int>(visEnd * static_cast<double>(dataSize));
    int range = endSample - startSample;
    if (range <= 0) return;

    float xStep = static_cast<float>(w) / static_cast<float>(range);

    float barW, gap;
    if (xStep <= 1.5f) {
        barW = std::max(1.0f, xStep);
        gap = 0.0f;
    } else if (xStep <= 4.0f) {
        barW = xStep - 1.0f;
        gap = 1.0f;
    } else {
        barW = std::max(2.0f, xStep * 0.7f);
        gap = xStep - barW;
    }

    const juce::Colour bassC(0xFFC02626), midC(0xFF4EB648), highC(0xFF0F88CA);
    const float bR = bassC.getFloatRed(), bG = bassC.getFloatGreen(), bB = bassC.getFloatBlue();
    const float mR = midC.getFloatRed(),  mG = midC.getFloatGreen(),  mB = midC.getFloatBlue();
    const float hR = highC.getFloatRed(), hG = highC.getFloatGreen(), hB = highC.getFloatBlue();

    const float ampMax = m_ampMax;

    for (int i = 0; i < range; ++i)
    {
        int idx = juce::jlimit(0, static_cast<int>(dataSize) - 1, startSample + i);
        size_t si = static_cast<size_t>(idx);
        float x = static_cast<float>(i) * xStep + gap * 0.5f;

        float bassVal = (si < m_bassPeaks.size()) ? std::abs(m_bassPeaks[si]) : 0.0f;
        float midVal  = (si < m_midPeaks.size())  ? std::abs(m_midPeaks[si])  : 0.0f;
        float trebVal = (si < m_treblePeaks.size()) ? std::abs(m_treblePeaks[si]) : 0.0f;
        float amp     = (si < m_peaks.size()) ? std::abs(m_peaks[si])
                                              : std::max({ bassVal, midVal, trebVal });

        if (amp < 0.003f) continue;

        float peakH = std::min(1.0f, amp / ampMax) * halfH;

        float wSum = bassVal + midVal + trebVal;
        if (wSum < 1e-6f) { bassVal = midVal = trebVal = 1.0f; wSum = 3.0f; }
        float wB = bassVal / wSum, wM = midVal / wSum, wH = trebVal / wSum;

        float r  = bR * wB + mR * wM + hR * wH;
        float gr = bG * wB + mG * wM + hG * wH;
        float b  = bB * wB + mB * wM + hB * wH;

        float loud = std::min(1.0f, amp / ampMax);
        float bri  = 0.62f + 0.38f * loud;
        float lift = loud * loud * 0.22f;
        r  = std::min(1.0f, r  * bri + lift);
        gr = std::min(1.0f, gr * bri + lift);
        b  = std::min(1.0f, b  * bri + lift);
        juce::Colour barColor = juce::Colour::fromFloatRGBA(r, gr, b, 1.0f);

        g.setColour(barColor);
        g.fillRect(x, midY - peakH, barW, peakH);

        g.setColour(barColor.withMultipliedBrightness(0.62f));
        g.fillRect(x, midY, barW, peakH);

        if (peakH > 3.0f) {
            g.setColour(barColor.interpolatedWith(juce::Colours::white, 0.35f).withAlpha(0.9f));
            g.fillRect(x, midY - peakH, barW, 1.0f);
            g.fillRect(x, midY + peakH - 1.0f, barW, 1.0f);
        }

        if (loud > 0.72f) {
            float glowA = (loud - 0.72f) / 0.28f * 0.10f;
            g.setColour(barColor.withAlpha(glowA));
            g.fillRect(x - 0.5f, midY - peakH - 0.5f, barW + 1.0f, peakH * 2.0f + 1.0f);
        }
    }

    g.setColour(juce::Colour(0x18FFFFFF));
    g.fillRect(0.0f, midY - 0.5f, static_cast<float>(w), 1.0f);
}

void WaveformWidget::drawBeatGrid(juce::Graphics& g)
{
    if (m_beatGrid.empty()) return;

    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    float h = static_cast<float>(getHeight());
    float w = static_cast<float>(getWidth());

    double interval = 0.0;
    if (m_beatGrid.size() >= 2)
        interval = (m_beatGrid.back() - m_beatGrid.front()) /
                   static_cast<double>(m_beatGrid.size() - 1);
    const double pxPerBeat = interval > 0.0 ? interval / (ve - vs) * static_cast<double>(w)
                                            : static_cast<double>(w);

    const bool drawBeats = pxPerBeat >= 6.0;
    const bool drawMeasures = pxPerBeat >= 3.0;
    if (!drawBeats && !drawMeasures) return;

    int beatCount = 0;
    for (double beat : m_beatGrid)
    {
        if (beat >= vs && beat <= ve)
        {
            float fx = static_cast<float>((beat - vs) / (ve - vs) * static_cast<double>(w));

            if (beatCount % 4 == 0)
            {
                g.setColour(juce::Colour(0x10FFFFFF));
                g.fillRect(fx - 1.5f, 0.0f, 3.0f, h);
                g.setColour(juce::Colour(0x38FFFFFF));
                g.fillRect(fx - 0.5f, 0.0f, 1.0f, h);
            }
            else if (drawBeats)
            {
                g.setColour(juce::Colour(0x12FFFFFF));
                g.fillRect(fx - 0.25f, 0.0f, 0.5f, h);
            }
        }
        ++beatCount;
    }
}

void WaveformWidget::drawSections(juce::Graphics& g)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());

    for (auto& sec : m_sections)
    {
        if (sec.end < vs || sec.start > ve) continue;
        float x1 = static_cast<float>((std::max(sec.start, vs) - vs) / (ve - vs) * static_cast<double>(w));
        float x2 = static_cast<float>((std::min(sec.end, ve) - vs) / (ve - vs) * static_cast<double>(w));

        juce::ColourGradient secGrad(
            sec.color.withAlpha(0.08f), x1, 0.0f,
            sec.color.withAlpha(0.02f), x1, h, false);
        g.setGradientFill(secGrad);
        g.fillRect(x1, 0.0f, x2 - x1, h);

        g.setColour(sec.color.withAlpha(0.25f));
        g.fillRect(x1, 0.0f, 2.0f, h);

        g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::bold));
        auto textW = g.getCurrentFont().getStringWidthFloat(sec.label) + 10.0f;
        g.setColour(sec.color.withAlpha(0.15f));
        g.fillRoundedRectangle(x1 + 4.0f, 3.0f, textW, 14.0f, 3.0f);
        g.setColour(sec.color.withAlpha(0.8f));
        g.drawText(sec.label, static_cast<int>(x1) + 9, 3, static_cast<int>(textW) - 10, 14,
                   juce::Justification::centredLeft);
    }
}

float WaveformWidget::cueScreenX(const CuePoint& cue) const
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    return static_cast<float>((cue.position - vs) / (ve - vs) * static_cast<double>(getWidth()));
}

float WaveformWidget::flagWidthFor(const CuePoint& cue) const
{
    juce::String text = juce::String(cue.number);
    if (cue.label.isNotEmpty())
        text += " " + cue.label;
    float tw = juce::GlyphArrangement::getStringWidth(flagFont(), text) + 16.0f;
    return juce::jlimit(26.0f, 150.0f, tw);
}

juce::Rectangle<float> WaveformWidget::flagBoundsFor(const CuePoint& cue, float fx) const
{
    const float fw = flagWidthFor(cue);
    const float w = static_cast<float>(getWidth());
    if (fx + fw > w - 3.0f)
        return { fx - fw, kFlagY, fw, kFlagH };
    return { fx, kFlagY, fw, kFlagH };
}

int WaveformWidget::cueIndexAt(juce::Point<int> pos) const
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    float best = 8.0f;
    int bestIdx = -1;
    for (int i = 0; i < static_cast<int>(m_cuePoints.size()); ++i)
    {
        const auto& cue = m_cuePoints[static_cast<size_t>(i)];
        if (cue.position < vs || cue.position > ve) continue;
        float fx = cueScreenX(cue);
        float dist = std::abs(static_cast<float>(pos.x) - fx);
        if (dist < best)
        {
            best = dist;
            bestIdx = i;
        }
        if (flagBoundsFor(cue, fx).contains(pos.toFloat()))
            return i;
    }
    return bestIdx;
}

void WaveformWidget::drawCuePoints(juce::Graphics& g)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    float h = static_cast<float>(getHeight());

    for (int i = 0; i < static_cast<int>(m_cuePoints.size()); ++i)
    {
        const auto& cue = m_cuePoints[static_cast<size_t>(i)];
        if (cue.position < vs || cue.position > ve) continue;

        const float fx = cueScreenX(cue);
        const bool isDragging = (m_draggingCueIndex == i);
        const bool isHovered = (m_hoveredCueIndex == i) && !m_dragging;

        juce::Colour cueCol = cue.color.withMultipliedSaturation(1.2f);
        if (cueCol.getBrightness() < 0.45f)
            cueCol = cueCol.interpolatedWith(juce::Colours::white, 0.45f);
        if (isHovered)
            cueCol = cueCol.brighter(0.25f);

        g.setColour(cueCol.withAlpha(isHovered || isDragging ? 0.30f : 0.18f));
        g.fillRect(fx - 5.0f, 0.0f, 10.0f, h);

        const float lineW = isDragging ? 4.0f : 3.0f;
        g.setColour(juce::Colour(0xC0000000));
        g.fillRect(fx - lineW * 0.5f - 1.0f, 0.0f, lineW + 2.0f, h);
        g.setColour(isDragging ? cueCol.brighter(0.3f) : cueCol);
        g.fillRect(fx - lineW * 0.5f, 0.0f, lineW, h);
        g.setColour(juce::Colours::white.withAlpha(0.70f));
        g.fillRect(fx - 0.5f, 0.0f, 1.0f, h);

        {
            juce::Path anchorTri;
            anchorTri.addTriangle(fx - 6.0f, h, fx + 6.0f, h, fx, h - 9.0f);
            g.setColour(juce::Colour(0xC0000000));
            g.strokePath(anchorTri, juce::PathStrokeType(1.5f));
            g.setColour(cueCol);
            g.fillPath(anchorTri);
        }

        auto flag = flagBoundsFor(cue, fx);
        if (isDragging)
            flag = flag.expanded(2.0f, 1.5f);
        const bool flipped = flag.getX() < fx - 0.5f;

        juce::Path flagPath;
        const float bevel = 5.0f;
        if (!flipped)
        {
            flagPath.startNewSubPath(flag.getX(), flag.getY());
            flagPath.lineTo(flag.getRight(), flag.getY());
            flagPath.lineTo(flag.getRight(), flag.getBottom() - bevel);
            flagPath.lineTo(flag.getRight() - bevel, flag.getBottom());
            flagPath.lineTo(flag.getX(), flag.getBottom());
            flagPath.closeSubPath();
        }
        else
        {
            flagPath.startNewSubPath(flag.getX(), flag.getY());
            flagPath.lineTo(flag.getRight(), flag.getY());
            flagPath.lineTo(flag.getRight(), flag.getBottom());
            flagPath.lineTo(flag.getX() + bevel, flag.getBottom());
            flagPath.lineTo(flag.getX(), flag.getBottom() - bevel);
            flagPath.closeSubPath();
        }

        g.setColour(juce::Colour(0xB0000000));
        g.strokePath(flagPath, juce::PathStrokeType(2.5f));
        g.setColour(cueCol);
        g.fillPath(flagPath);
        if (isHovered)
        {
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.strokePath(flagPath, juce::PathStrokeType(1.2f));
        }

        juce::String text = juce::String(cue.number);
        if (cue.label.isNotEmpty())
            text += " " + cue.label;
        g.setFont(flagFont());
        const int textX = static_cast<int>(flag.getX()) + (flipped ? 6 : 7);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawText(text, textX + 1, static_cast<int>(flag.getY()) + 1,
                   static_cast<int>(flag.getWidth()) - 10, static_cast<int>(flag.getHeight()),
                   juce::Justification::centredLeft, true);
        g.setColour(juce::Colours::white);
        g.drawText(text, textX, static_cast<int>(flag.getY()),
                   static_cast<int>(flag.getWidth()) - 10, static_cast<int>(flag.getHeight()),
                   juce::Justification::centredLeft, true);

        if (isDragging && m_duration > 0.0)
        {
            const juce::String timeText = formatPreciseTime(cue.position * m_duration);
            juce::Font timeFont("Consolas", 11.0f, juce::Font::bold);
            const float tw = juce::GlyphArrangement::getStringWidth(timeFont, timeText) + 14.0f;
            float bx = juce::jlimit(2.0f, static_cast<float>(getWidth()) - tw - 2.0f, fx - tw * 0.5f);
            const float by = flag.getBottom() + 6.0f;
            g.setColour(juce::Colour(0xE8000000));
            g.fillRoundedRectangle(bx, by, tw, 18.0f, 4.0f);
            g.setColour(cueCol.withAlpha(0.8f));
            g.drawRoundedRectangle(bx, by, tw, 18.0f, 4.0f, 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(timeFont);
            g.drawText(timeText, static_cast<int>(bx), static_cast<int>(by),
                       static_cast<int>(tw), 18, juce::Justification::centred);
        }

        juce::Path triBot;
        triBot.addTriangle(fx - 4.0f, h, fx + 4.0f, h, fx, h - 6.0f);
        g.setColour(juce::Colour(0xCC000000));
        g.fillPath(triBot);
        juce::Path triBot2;
        triBot2.addTriangle(fx - 3.0f, h, fx + 3.0f, h, fx, h - 5.0f);
        g.setColour(cueCol);
        g.fillPath(triBot2);
    }
}

void WaveformWidget::drawPlayhead(juce::Graphics& g)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    if (m_playheadPos < vs || m_playheadPos > ve) return;

    float fx = static_cast<float>((m_playheadPos - vs) / (ve - vs) * getWidth());
    float h = static_cast<float>(getHeight());

    {
        juce::ColourGradient glowL(
            juce::Colour(0x00FFFFFF), fx - 20.0f, 0.0f,
            juce::Colour(0x14FFFFFF), fx, 0.0f, false);
        g.setGradientFill(glowL);
        g.fillRect(fx - 20.0f, 0.0f, 20.0f, h);

        juce::ColourGradient glowR(
            juce::Colour(0x14FFFFFF), fx, 0.0f,
            juce::Colour(0x00FFFFFF), fx + 20.0f, 0.0f, false);
        g.setGradientFill(glowR);
        g.fillRect(fx, 0.0f, 20.0f, h);
    }

    g.setColour(juce::Colour(0x40FFFFFF));
    g.fillRect(fx - 2.5f, 0.0f, 5.0f, h);

    g.setColour(juce::Colours::white);
    g.fillRect(fx - 0.75f, 0.0f, 1.5f, h);

    {
        juce::Path diamond;
        diamond.addTriangle(fx - 8.0f, 0.0f, fx + 8.0f, 0.0f, fx, 10.0f);
        g.setColour(juce::Colour(0x60000000));
        juce::Path shadowDiamond;
        shadowDiamond.addTriangle(fx - 8.0f + 1.0f, 1.0f, fx + 8.0f + 1.0f, 1.0f, fx + 1.0f, 11.0f);
        g.fillPath(shadowDiamond);
        g.setColour(juce::Colours::white);
        g.fillPath(diamond);
        g.setColour(juce::Colour(0x40FFFFFF));
        juce::Path hlDiamond;
        hlDiamond.addTriangle(fx - 5.0f, 1.0f, fx + 5.0f, 1.0f, fx, 6.0f);
        g.fillPath(hlDiamond);
    }

    {
        juce::Path diamond;
        diamond.addTriangle(fx - 8.0f, h, fx + 8.0f, h, fx, h - 10.0f);
        g.setColour(juce::Colours::white);
        g.fillPath(diamond);
    }
}

void WaveformWidget::drawSelection(juce::Graphics& g)
{
    if (!m_selecting && m_selStart == m_selEnd) return;
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());

    float x1 = static_cast<float>((m_selStart - vs) / (ve - vs) * static_cast<double>(w));
    float x2 = static_cast<float>((m_selEnd - vs) / (ve - vs) * static_cast<double>(w));

    float selX = std::min(x1, x2);
    float selW = std::abs(x2 - x1);

    juce::ColourGradient selGrad(
        Colors::primary().withAlpha(0.2f), selX, 0.0f,
        Colors::primary().withAlpha(0.08f), selX, h, false);
    g.setGradientFill(selGrad);
    g.fillRect(selX, 0.0f, selW, h);

    g.setColour(Colors::primary().withAlpha(0.8f));
    g.fillRect(x1 - 0.5f, 0.0f, 1.5f, h);
    g.fillRect(x2 - 0.5f, 0.0f, 1.5f, h);
}

void WaveformWidget::drawTimeline(juce::Graphics& g)
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());

    if (m_duration <= 0.0)
    {
        juce::ColourGradient sepGrad(
            juce::Colour(0x00FFFFFF), 0.0f, h - 1.0f,
            juce::Colour(0x18FFFFFF), w * 0.5f, h - 1.0f, false);
        g.setGradientFill(sepGrad);
        g.fillRect(0.0f, h - 1.0f, w * 0.5f, 1.0f);

        juce::ColourGradient sepGrad2(
            juce::Colour(0x18FFFFFF), w * 0.5f, h - 1.0f,
            juce::Colour(0x00FFFFFF), w, h - 1.0f, false);
        g.setGradientFill(sepGrad2);
        g.fillRect(w * 0.5f, h - 1.0f, w * 0.5f, 1.0f);
        return;
    }

    const float bandY = h - kTimelineH;
    g.setColour(Colors::bg().withAlpha(0.82f));
    g.fillRect(0.0f, bandY, w, kTimelineH);
    g.setColour(juce::Colour(0x22FFFFFF));
    g.fillRect(0.0f, bandY, w, 1.0f);

    const double vs = m_scrollOffset;
    const double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    const double visStartSec = vs * m_duration;
    const double visEndSec = ve * m_duration;
    const double visibleSec = std::max(0.001, visEndSec - visStartSec);

    static const double steps[] = { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };
    double step = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (double s : steps)
    {
        if (w * s / visibleSec >= 64.0)
        {
            step = s;
            break;
        }
    }

    g.setFont(juce::Font("Segoe UI", 9.0f, juce::Font::plain));
    for (double t = std::ceil(visStartSec / step) * step; t <= visEndSec; t += step)
    {
        const float fx = static_cast<float>((t - visStartSec) / visibleSec * static_cast<double>(w));
        g.setColour(juce::Colour(0x30FFFFFF));
        g.fillRect(fx, bandY, 1.0f, 4.0f);
        g.setColour(Colors::textMuted());
        g.drawText(formatRulerTime(t), static_cast<int>(fx) + 3, static_cast<int>(bandY) + 1,
                   44, static_cast<int>(kTimelineH) - 2, juce::Justification::centredLeft);
    }

    const double subStep = step / 5.0;
    if (w * subStep / visibleSec >= 9.0)
    {
        g.setColour(juce::Colour(0x16FFFFFF));
        for (double t = std::ceil(visStartSec / subStep) * subStep; t <= visEndSec; t += subStep)
        {
            const float fx = static_cast<float>((t - visStartSec) / visibleSec * static_cast<double>(w));
            g.fillRect(fx, bandY, 1.0f, 2.5f);
        }
    }
}

void WaveformWidget::mouseDown(const juce::MouseEvent& e)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    double pos = juce::jlimit(0.0, 1.0, vs + (static_cast<double>(e.x) / getWidth()) * (ve - vs));
    float w = static_cast<float>(getWidth());

    m_draggingCueIndex = -1;
    int hitIdx = cueIndexAt(e.getPosition());

    if (hitIdx >= 0)
    {
        auto& cue = m_cuePoints[static_cast<size_t>(hitIdx)];
        if (e.mods.isPopupMenu())
        {
            m_listeners.call([num = cue.number, sp = e.getScreenPosition()](Listener& l) {
                l.cuePointRightClicked(num, sp);
            });
            return;
        }
        m_draggingCueIndex = hitIdx;
        m_dragging = true;
        float fx = static_cast<float>((cue.position - vs) / (ve - vs) * static_cast<double>(w));
        m_dragStartOffset = static_cast<double>(e.x) - static_cast<double>(fx);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        m_listeners.call([num = cue.number](Listener& l) { l.cuePointClicked(num); });
        return;
    }

    if (e.mods.isAltDown() && !m_beatGrid.empty())
    {
        float beatBestDist = 10.0f;
        int beatBestIdx = -1;
        for (int i = 0; i < static_cast<int>(m_beatGrid.size()); ++i)
        {
            double bp = m_beatGrid[static_cast<size_t>(i)];
            if (bp < vs || bp > ve) continue;
            float bx = static_cast<float>((bp - vs) / (ve - vs) * static_cast<double>(w));
            float dist = std::abs(static_cast<float>(e.x) - bx);
            if (dist < beatBestDist) { beatBestDist = dist; beatBestIdx = i; }
        }
        if (beatBestIdx >= 0)
        {
            m_draggingBeatIndex = beatBestIdx;
            m_dragging = true;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    if (e.mods.isShiftDown())
    {
        m_selecting = true;
        m_selStart = pos;
        m_selEnd = pos;
    }
    else
    {
        m_playheadPos = pos;
        m_listeners.call([pos](Listener& l) { l.positionClicked(pos); });
        repaint();
    }
}

void WaveformWidget::mouseDrag(const juce::MouseEvent& e)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);

    if (m_dragging && m_draggingCueIndex >= 0 && m_draggingCueIndex < static_cast<int>(m_cuePoints.size()))
    {
        double correctedX = static_cast<double>(e.x) - m_dragStartOffset;
        double pos = juce::jlimit(0.0, 1.0, vs + (correctedX / getWidth()) * (ve - vs));
        m_cuePoints[static_cast<size_t>(m_draggingCueIndex)].position = pos;
        repaint();
        return;
    }

    if (m_dragging && m_draggingBeatIndex >= 0 &&
        m_draggingBeatIndex < static_cast<int>(m_beatGrid.size()))
    {
        double rawPos = juce::jlimit(0.0, 1.0,
                                     vs + (static_cast<double>(e.x) / getWidth()) * (ve - vs));
        double interval = 0.0;
        if (m_beatGrid.size() >= 2)
            interval = (m_beatGrid.back() - m_beatGrid.front()) /
                       static_cast<double>(m_beatGrid.size() - 1);
        double snapped = rawPos;
        if (interval > 1e-6)
        {
            const double step = interval * 0.5;
            const double anchor = m_beatGrid.front();
            snapped = anchor + std::round((rawPos - anchor) / step) * step;
            snapped = juce::jlimit(0.0, 1.0, snapped);
        }
        if (m_draggingBeatIndex > 0)
            snapped = std::max(snapped,
                               m_beatGrid[static_cast<size_t>(m_draggingBeatIndex - 1)] + 1e-6);
        if (m_draggingBeatIndex + 1 < static_cast<int>(m_beatGrid.size()))
            snapped = std::min(snapped,
                               m_beatGrid[static_cast<size_t>(m_draggingBeatIndex + 1)] - 1e-6);
        m_beatGrid[static_cast<size_t>(m_draggingBeatIndex)] = snapped;
        repaint();
        return;
    }

    double pos = juce::jlimit(0.0, 1.0, vs + (static_cast<double>(e.x) / getWidth()) * (ve - vs));
    if (m_selecting)
    {
        m_selEnd = pos;
        repaint();
    }
    m_listeners.call([pos](Listener& l) { l.positionChanged(pos); });
}

void WaveformWidget::mouseUp(const juce::MouseEvent&)
{
    if (m_dragging && m_draggingCueIndex >= 0 && m_draggingCueIndex < static_cast<int>(m_cuePoints.size()))
    {
        auto& cue = m_cuePoints[static_cast<size_t>(m_draggingCueIndex)];
        m_listeners.call([num = cue.number, pos = cue.position](Listener& l) { l.cuePointMoved(num, pos); });
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    if (m_dragging && m_draggingBeatIndex >= 0 &&
        m_draggingBeatIndex < static_cast<int>(m_beatGrid.size()))
    {
        const int idx  = m_draggingBeatIndex;
        const double p = m_beatGrid[static_cast<size_t>(idx)];
        m_listeners.call([idx, p](Listener& l) { l.beatMoved(idx, p); });
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    m_draggingCueIndex  = -1;
    m_draggingBeatIndex = -1;

    if (m_selecting)
    {
        m_selecting = false;
        m_listeners.call([this](Listener& l) {
            l.selectionChanged(std::min(m_selStart, m_selEnd), std::max(m_selStart, m_selEnd));
        });
    }
    m_dragging = false;
}

void WaveformWidget::mouseMove(const juce::MouseEvent& e)
{
    if (m_dragging)
        return;
    const int hovered = cueIndexAt(e.getPosition());
    if (hovered != m_hoveredCueIndex)
    {
        m_hoveredCueIndex = hovered;
        setMouseCursor(hovered >= 0 ? juce::MouseCursor::PointingHandCursor
                                    : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WaveformWidget::mouseExit(const juce::MouseEvent&)
{
    if (m_hoveredCueIndex != -1)
    {
        m_hoveredCueIndex = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void WaveformWidget::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    double vs = m_scrollOffset;
    double ve = std::min(1.0, m_scrollOffset + 1.0 / m_zoom);
    double mousePos = vs + (static_cast<double>(e.x) / getWidth()) * (ve - vs);

    double newZoom = m_zoom * (w.deltaY > 0 ? 1.25 : 1.0 / 1.25);
    newZoom = juce::jlimit(1.0, 100.0, newZoom);

    double newVisWidth = 1.0 / newZoom;
    double ratio = static_cast<double>(e.x) / getWidth();
    m_scrollOffset = juce::jlimit(0.0, std::max(0.0, 1.0 - newVisWidth), mousePos - newVisWidth * ratio);

    m_zoom = newZoom;
    m_cacheValid = false;
    m_listeners.call([this](Listener& l) { l.zoomChanged(m_zoom); });
    repaint();
}

} // namespace BeatMate::UI
