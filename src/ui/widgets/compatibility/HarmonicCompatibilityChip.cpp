#include "HarmonicCompatibilityChip.h"

namespace BeatMate::UI::Widgets {

namespace {
juce::Colour scoreBackground(int score) {
    if (score >= 85) return juce::Colour::fromRGB( 80, 200, 120);
    if (score >= 70) return juce::Colour::fromRGB(160, 200,  80);
    if (score >= 55) return juce::Colour::fromRGB(230, 190,  60);
    if (score >= 40) return juce::Colour::fromRGB(230, 140,  60);
    return             juce::Colour::fromRGB(220,  70,  70);
}
}

HarmonicCompatibilityChip::HarmonicCompatibilityChip() {
    setInterceptsMouseClicks(false, false);
}

void HarmonicCompatibilityChip::setTracks(const Models::Track& from,
                                          const Models::Track& to) {
    const auto result = m_classifier.classify(from.camelotKey, to.camelotKey);
    m_moveLabel   = result.label;
    m_moveColor   = result.color;
    m_bpmDelta    = to.bpm - from.bpm;
    m_energyDelta = (int)to.energy - (int)from.energy;
    m_hasData     = true;
    repaint();
}

void HarmonicCompatibilityChip::setScore(int score0to100) {
    m_score = juce::jlimit(0, 100, score0to100);
    repaint();
}

void HarmonicCompatibilityChip::clear() {
    m_hasData = false;
    m_score = 0;
    m_moveLabel.clear();
    repaint();
}

void HarmonicCompatibilityChip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    const float radius = bounds.getHeight() * 0.5f;

    auto bg = scoreBackground(m_score).withAlpha(0.92f);
    g.setColour(bg.darker(0.6f));
    g.fillRoundedRectangle(bounds.translated(0, 1), radius);
    g.setColour(bg);
    g.fillRoundedRectangle(bounds, radius);

    auto scoreArea = bounds.removeFromLeft(bounds.getHeight());
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(scoreArea.getHeight() * 0.55f).withStyle("Bold")));
    g.drawText(juce::String(m_score), scoreArea.toNearestInt(),
               juce::Justification::centred, false);

    if (!m_hasData) return;

    bounds.removeFromLeft(4.0f);
    const float dotR = 6.0f;
    auto dotBounds = bounds.removeFromLeft(dotR * 2 + 4.0f).reduced(2.0f);
    g.setColour(m_moveColor);
    g.fillEllipse(dotBounds.withSizeKeepingCentre(dotR * 2, dotR * 2));

    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    auto labelArea = bounds.removeFromLeft(bounds.getWidth() * 0.55f);
    g.drawText(m_moveLabel, labelArea.toNearestInt(),
               juce::Justification::centredLeft, true);

    juce::String right;
    if (std::abs(m_bpmDelta) < 0.5) right = "MATCH";
    else                            right = juce::String(m_bpmDelta, 1) + " BPM";
    if      (m_energyDelta >  0) right += " " + juce::String(juce::CharPointer_UTF8("\xe2\x86\x91"));
    else if (m_energyDelta <  0) right += " " + juce::String(juce::CharPointer_UTF8("\xe2\x86\x93"));

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(right, bounds.toNearestInt(),
               juce::Justification::centredRight, true);
}

} // namespace BeatMate::UI::Widgets
