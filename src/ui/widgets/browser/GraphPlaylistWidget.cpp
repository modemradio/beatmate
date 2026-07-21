#include "GraphPlaylistWidget.h"
#include "../../styles/ColorPalette.h"
#include <cmath>

namespace BeatMate::UI {

GraphPlaylistWidget::GraphPlaylistWidget()
{
    startTimerHz(30);
}

GraphPlaylistWidget::~GraphPlaylistWidget()
{
    stopTimer();
}

int GraphPlaylistWidget::addTrack(const juce::String& title, double bpm, const juce::String& key)
{
    TrackNode node;
    node.title = title;
    node.bpm = bpm;
    node.key = key;

    juce::Random rng;
    float cx = static_cast<float>(getWidth()) * 0.5f;
    float cy = static_cast<float>(getHeight()) * 0.5f;
    if (cx < 1.0f) cx = 200.0f;
    if (cy < 1.0f) cy = 200.0f;
    node.position = { cx + rng.nextFloat() * 100.0f - 50.0f,
                      cy + rng.nextFloat() * 100.0f - 50.0f };
    node.velocity = { 0.0f, 0.0f };

    nodes_.push_back(node);
    return static_cast<int>(nodes_.size()) - 1;
}

void GraphPlaylistWidget::addEdge(int from, int to, float score)
{
    if (from >= 0 && from < static_cast<int>(nodes_.size()) &&
        to >= 0 && to < static_cast<int>(nodes_.size()) && from != to)
    {
        edges_.push_back({ from, to, juce::jlimit(0.0f, 1.0f, score) });
    }
}

void GraphPlaylistWidget::clear()
{
    nodes_.clear();
    edges_.clear();
    selectedNode_ = -1;
    draggedNode_ = -1;
    repaint();
}

void GraphPlaylistWidget::timerCallback()
{
    applyForceDirectedLayout();
    repaint();
}

void GraphPlaylistWidget::applyForceDirectedLayout()
{
    if (nodes_.size() < 2) return;

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float centreX = w * 0.5f;
    float centreY = h * 0.5f;

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        for (size_t j = i + 1; j < nodes_.size(); ++j)
        {
            auto diff = nodes_[i].position - nodes_[j].position;
            float dist = diff.getDistanceFromOrigin();
            if (dist < 1.0f) dist = 1.0f;

            float force = 2000.0f / (dist * dist);
            auto dir = diff / dist;

            nodes_[i].velocity = nodes_[i].velocity + dir * force;
            nodes_[j].velocity = nodes_[j].velocity - dir * force;
        }
    }

    for (const auto& edge : edges_)
    {
        auto& a = nodes_[static_cast<size_t>(edge.fromIndex)];
        auto& b = nodes_[static_cast<size_t>(edge.toIndex)];
        auto diff = b.position - a.position;
        float dist = diff.getDistanceFromOrigin();
        if (dist < 1.0f) dist = 1.0f;

        float idealDist = 80.0f + (1.0f - edge.score) * 120.0f;
        float force = (dist - idealDist) * 0.01f;
        auto dir = diff / dist;

        a.velocity = a.velocity + dir * force;
        b.velocity = b.velocity - dir * force;
    }

    for (auto& node : nodes_)
    {
        auto diff = juce::Point<float>(centreX, centreY) - node.position;
        node.velocity = node.velocity + diff * 0.001f;
    }

    for (size_t i = 0; i < nodes_.size(); ++i)
    {
        if (static_cast<int>(i) == draggedNode_) continue;

        nodes_[i].velocity = nodes_[i].velocity * 0.85f;
        nodes_[i].position = nodes_[i].position + nodes_[i].velocity * 0.1f;

        nodes_[i].position.x = juce::jlimit(nodeRadius_, w - nodeRadius_, nodes_[i].position.x);
        nodes_[i].position.y = juce::jlimit(nodeRadius_, h - nodeRadius_, nodes_[i].position.y);
    }
}

int GraphPlaylistWidget::hitTestNode(juce::Point<float> pos) const
{
    for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; --i)
    {
        if (pos.getDistanceFrom(nodes_[static_cast<size_t>(i)].position) <= nodeRadius_)
            return i;
    }
    return -1;
}

void GraphPlaylistWidget::paint(juce::Graphics& g)
{
    g.setColour(Colors::bgDarker());
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);

    for (const auto& edge : edges_)
    {
        const auto& a = nodes_[static_cast<size_t>(edge.fromIndex)];
        const auto& b = nodes_[static_cast<size_t>(edge.toIndex)];

        juce::Colour edgeCol;
        if (edge.score > 0.8f)
            edgeCol = Colors::success();
        else if (edge.score >= 0.5f)
            edgeCol = Colors::warning();
        else
            edgeCol = Colors::error();

        float thickness = 1.0f + edge.score * 3.0f;
        g.setColour(edgeCol.withAlpha(0.4f));
        g.drawLine(a.position.x, a.position.y, b.position.x, b.position.y, thickness);
    }

    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i)
    {
        const auto& node = nodes_[static_cast<size_t>(i)];
        bool selected = (i == selectedNode_);

        auto nodeRect = juce::Rectangle<float>(nodeRadius_ * 2.0f, nodeRadius_ * 2.0f)
                            .withCentre(node.position);

        g.setColour(selected ? Colors::primary() : Colors::bgLightest());
        g.fillEllipse(nodeRect);

        g.setColour(selected ? Colors::primaryHover() : Colors::borderLight());
        g.drawEllipse(nodeRect, selected ? 2.0f : 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(node.bpm, 0), nodeRect.reduced(2.0f),
                   juce::Justification::centredTop);

        g.setColour(Colors::success());
        g.setFont(juce::Font(9.0f));
        g.drawText(node.key, nodeRect.reduced(2.0f),
                   juce::Justification::centredBottom);
    }
}

void GraphPlaylistWidget::resized()
{
    repaint();
}

void GraphPlaylistWidget::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.getPosition().toFloat();
    int hit = hitTestNode(pos);

    if (hit >= 0)
    {
        selectedNode_ = hit;
        draggedNode_ = hit;
        listeners_.call([hit](Listener& l) { l.trackNodeSelected(hit); });
    }
    else
    {
        selectedNode_ = -1;
    }
    repaint();
}

void GraphPlaylistWidget::mouseDrag(const juce::MouseEvent& e)
{
    if (draggedNode_ >= 0)
    {
        nodes_[static_cast<size_t>(draggedNode_)].position = e.getPosition().toFloat();
        nodes_[static_cast<size_t>(draggedNode_)].velocity = { 0.0f, 0.0f };
        repaint();
    }
}

} // namespace BeatMate::UI
