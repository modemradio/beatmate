#include "ChapterBuilderWidget.h"
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI {

ChapterBuilderWidget::ChapterBuilderWidget()
{
    addDefaultChapters();
}

void ChapterBuilderWidget::addDefaultChapters()
{
    chapters_.clear();
    chapters_.push_back({ "Warm-up",   juce::Colour(0xFF4A90E2), 1.0f, "Deep House",  120.0, 3.0f });
    chapters_.push_back({ "Build",     juce::Colour(0xFFFFD700), 1.0f, "Tech House",  126.0, 6.0f });
    chapters_.push_back({ "Peak",      juce::Colour(0xFFFF3838), 1.5f, "Techno",      130.0, 9.0f });
    chapters_.push_back({ "Wind-down", juce::Colour(0xFF00FFA3), 0.8f, "Melodic",     124.0, 4.0f });
}

void ChapterBuilderWidget::addChapter(const juce::String& name, juce::Colour colour, float duration)
{
    chapters_.push_back({ name, colour, juce::jmax(0.1f, duration), {}, 128.0, 5.0f });
    repaint();
}

void ChapterBuilderWidget::clearChapters()
{
    chapters_.clear();
    selectedChapter_ = -1;
    repaint();
}

float ChapterBuilderWidget::getTotalDuration() const
{
    float total = 0.0f;
    for (const auto& ch : chapters_)
        total += ch.duration;
    return juce::jmax(0.01f, total);
}

int ChapterBuilderWidget::hitTestEdge(float x) const
{
    if (chapters_.size() < 2) return -1;

    float totalDur = getTotalDuration();
    float w = static_cast<float>(getWidth());
    float accum = 0.0f;

    for (size_t i = 0; i < chapters_.size() - 1; ++i)
    {
        accum += chapters_[i].duration;
        float edgeX = (accum / totalDur) * w;
        if (std::abs(x - edgeX) < 6.0f)
            return static_cast<int>(i);
    }
    return -1;
}

int ChapterBuilderWidget::hitTestChapter(float x) const
{
    float totalDur = getTotalDuration();
    float w = static_cast<float>(getWidth());
    float accum = 0.0f;

    for (size_t i = 0; i < chapters_.size(); ++i)
    {
        float start = (accum / totalDur) * w;
        accum += chapters_[i].duration;
        float end = (accum / totalDur) * w;

        if (x >= start && x < end)
            return static_cast<int>(i);
    }
    return -1;
}

void ChapterBuilderWidget::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float w = bounds.getWidth();
    float h = bounds.getHeight();
    float totalDur = getTotalDuration();
    float accum = 0.0f;

    g.setColour(Colors::bgDark());
    g.fillRoundedRectangle(bounds, 8.0f);

    for (size_t i = 0; i < chapters_.size(); ++i)
    {
        const auto& ch = chapters_[i];
        float startX = (accum / totalDur) * w;
        accum += ch.duration;
        float endX = (accum / totalDur) * w;
        float chWidth = endX - startX;

        auto chBounds = juce::Rectangle<float>(startX, 0.0f, chWidth, h);

        bool selected = (static_cast<int>(i) == selectedChapter_);
        g.setColour(ch.colour.withAlpha(selected ? 0.5f : 0.25f));
        g.fillRect(chBounds);

        if (i > 0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawVerticalLine(static_cast<int>(startX), 0.0f, h);
        }

        if (chWidth > 40.0f)
        {
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(ch.name, chBounds.reduced(4.0f, 0.0f),
                       juce::Justification::centredTop);

            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.setFont(juce::Font(9.0f));
            juce::String info = juce::String(ch.bpm, 0) + " BPM | E:" + juce::String(ch.energy, 0);
            g.drawText(info, chBounds.reduced(4.0f, 0.0f),
                       juce::Justification::centredBottom);
        }

        if (selected)
        {
            g.setColour(ch.colour.withAlpha(0.8f));
            g.drawRect(chBounds, 2.0f);
        }
    }

    g.setColour(juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}

void ChapterBuilderWidget::resized()
{
    repaint();
}

void ChapterBuilderWidget::mouseDown(const juce::MouseEvent& e)
{
    float x = static_cast<float>(e.getPosition().x);

    dragEdgeIndex_ = hitTestEdge(x);
    dragStartX_ = x;

    if (dragEdgeIndex_ < 0)
    {
        int ch = hitTestChapter(x);
        if (ch != selectedChapter_)
        {
            selectedChapter_ = ch;
            listeners_.call([ch](Listener& l) { l.chapterSelected(ch); });
            repaint();
        }
    }
}

void ChapterBuilderWidget::mouseDrag(const juce::MouseEvent& e)
{
    if (dragEdgeIndex_ < 0) return;

    float x = static_cast<float>(e.getPosition().x);
    float dx = x - dragStartX_;
    dragStartX_ = x;

    float totalDur = getTotalDuration();
    float w = static_cast<float>(getWidth());
    float durationDelta = (dx / w) * totalDur;

    auto& left = chapters_[static_cast<size_t>(dragEdgeIndex_)];
    auto& right = chapters_[static_cast<size_t>(dragEdgeIndex_ + 1)];

    float newLeftDur = left.duration + durationDelta;
    float newRightDur = right.duration - durationDelta;

    if (newLeftDur >= 0.1f && newRightDur >= 0.1f)
    {
        left.duration = newLeftDur;
        right.duration = newRightDur;
        listeners_.call([this](Listener& l) { l.chapterResized(dragEdgeIndex_, chapters_[static_cast<size_t>(dragEdgeIndex_)].duration); });
        repaint();
    }
}

void ChapterBuilderWidget::mouseUp(const juce::MouseEvent&)
{
    dragEdgeIndex_ = -1;
}

} // namespace BeatMate::UI
