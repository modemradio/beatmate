#include "ScatterMapWidget.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

ScatterMapWidget::ScatterMapWidget()
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void ScatterMapWidget::addTrack(const juce::String& title, const juce::String& artist,
                                 double bpm, float energy, const juce::String& genre,
                                 juce::Colour colour)
{
    tracks_.push_back({ title, artist, bpm, energy, genre, colour });
    repaint();
}

void ScatterMapWidget::clearTracks()
{
    tracks_.clear();
    selectedTrack_ = -1;
    hoveredTrack_ = -1;
    repaint();
}

juce::Point<float> ScatterMapWidget::trackToScreen(const TrackPoint& t) const
{
    float w = static_cast<float>(getWidth()) - margin_ * 2.0f;
    float h = static_cast<float>(getHeight()) - margin_ * 2.0f;

    float xNorm = static_cast<float>((t.bpm - bpmMin_) / (bpmMax_ - bpmMin_));
    float yNorm = (t.energy - energyMin_) / (energyMax_ - energyMin_);

    float x = margin_ + xNorm * w;
    float y = margin_ + (1.0f - yNorm) * h;

    return { x, y };
}

int ScatterMapWidget::hitTestTrack(juce::Point<float> pos) const
{
    for (int i = static_cast<int>(tracks_.size()) - 1; i >= 0; --i)
    {
        auto screenPos = trackToScreen(tracks_[static_cast<size_t>(i)]);
        if (pos.getDistanceFrom(screenPos) <= pointRadius_ + 4.0f)
            return i;
    }
    return -1;
}

void ScatterMapWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(Colors::bgDarker());
    g.fillRoundedRectangle(bounds, 12.0f);

    float plotLeft = margin_;
    float plotTop = margin_;
    float plotRight = bounds.getWidth() - margin_;
    float plotBottom = bounds.getHeight() - margin_;

    g.setColour(Colors::bgLightest().withAlpha(0.2f));
    for (int i = 0; i <= 5; ++i)
    {
        float t = static_cast<float>(i) / 5.0f;
        float x = plotLeft + t * (plotRight - plotLeft);
        float y = plotTop + t * (plotBottom - plotTop);
        g.drawVerticalLine(static_cast<int>(x), plotTop, plotBottom);
        g.drawHorizontalLine(static_cast<int>(y), plotLeft, plotRight);
    }

    g.setColour(Colors::textMuted());
    g.setFont(juce::Font(9.0f));

    g.drawText("BPM", bounds.removeFromBottom(14.0f), juce::Justification::centred);
    for (int i = 0; i <= 5; ++i)
    {
        float t = static_cast<float>(i) / 5.0f;
        float x = plotLeft + t * (plotRight - plotLeft);
        double bpm = bpmMin_ + t * (bpmMax_ - bpmMin_);
        g.drawText(juce::String(static_cast<int>(bpm)),
                   static_cast<int>(x) - 15, static_cast<int>(plotBottom) + 2, 30, 14,
                   juce::Justification::centred);
    }

    for (int i = 0; i <= 5; ++i)
    {
        float t = static_cast<float>(i) / 5.0f;
        float y = plotBottom - t * (plotBottom - plotTop);
        float energy = energyMin_ + t * (energyMax_ - energyMin_);
        g.drawText(juce::String(energy, 0),
                   2, static_cast<int>(y) - 7, static_cast<int>(margin_) - 6, 14,
                   juce::Justification::centredRight);
    }

    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i)
    {
        const auto& t = tracks_[static_cast<size_t>(i)];
        auto pos = trackToScreen(t);
        bool sel = (i == selectedTrack_);
        bool hov = (i == hoveredTrack_);

        float r = pointRadius_;
        if (sel || hov) r += 2.0f;

        if (sel)
        {
            g.setColour(t.colour.withAlpha(0.2f));
            g.fillEllipse(pos.x - r * 2.0f, pos.y - r * 2.0f, r * 4.0f, r * 4.0f);
        }

        g.setColour(t.colour.withAlpha(sel ? 1.0f : 0.75f));
        g.fillEllipse(pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

        g.setColour(juce::Colours::white.withAlpha(sel ? 0.8f : 0.3f));
        g.drawEllipse(pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.0f);

        if (hov)
        {
            juce::String tip = t.title + "\n" + t.artist + "\n"
                              + juce::String(t.bpm, 1) + " BPM | E:" + juce::String(t.energy, 1)
                              + "\n" + t.genre;

            float tipW = 160.0f, tipH = 56.0f;
            float tipX = pos.x + 10.0f;
            float tipY = pos.y - tipH - 4.0f;
            if (tipX + tipW > bounds.getRight()) tipX = pos.x - tipW - 10.0f;
            if (tipY < bounds.getY()) tipY = pos.y + 10.0f;

            auto tipBounds = juce::Rectangle<float>(tipX, tipY, tipW, tipH);
            g.setColour(Colors::bgDark().withAlpha(0.95f));
            g.fillRoundedRectangle(tipBounds, 6.0f);
            g.setColour(Colors::borderLight());
            g.drawRoundedRectangle(tipBounds, 6.0f, 1.0f);

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.0f));
            g.drawFittedText(tip, tipBounds.reduced(6.0f).toNearestInt(),
                             juce::Justification::topLeft, 4);
        }
    }
}

void ScatterMapWidget::mouseMove(const juce::MouseEvent& e)
{
    int hit = hitTestTrack(e.getPosition().toFloat());
    if (hit != hoveredTrack_)
    {
        hoveredTrack_ = hit;
        repaint();
    }
}

void ScatterMapWidget::mouseDown(const juce::MouseEvent& e)
{
    int hit = hitTestTrack(e.getPosition().toFloat());
    if (hit != selectedTrack_)
    {
        selectedTrack_ = hit;
        if (hit >= 0)
            listeners_.call([hit](Listener& l) { l.scatterTrackSelected(hit); });
        repaint();
    }
}

} // namespace BeatMate::UI
