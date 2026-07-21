#include "CrowdEnergyMeter.h"

#include "../../styles/ColorPalette.h"
#include "../../../services/config/I18n.h"
#include "../../../services/history/SessionHistoryRecorder.h"
#include "../../../services/library/TrackDatabase.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace BeatMate::UI::Widgets {

namespace {

constexpr int kPollIntervalMs = 15 * 1000;

const char* zoneNameFor(float energy) {
    if (energy >= 7.0f) return "Peak";
    if (energy >= 5.0f) return "Main";
    return "Warm-up";
}

juce::Colour zoneColour(float energy) {
    if (energy >= 7.0f) return Colors::error();
    if (energy >= 5.0f) return Colors::warning();
    return Colors::success();
}

} // namespace

CrowdEnergyMeter::CrowdEnergyMeter() {
    m_statusLabel = std::make_unique<juce::Label>("crowdEnergyStatus",
        BM_TJ("crowdEnergy.empty"));
    m_statusLabel->setColour(juce::Label::textColourId, Colors::textPrimary());
    m_statusLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    m_statusLabel->setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f).withStyle("Bold")));
    m_statusLabel->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*m_statusLabel);

    startTimer(kPollIntervalMs);
}

CrowdEnergyMeter::~CrowdEnergyMeter() = default;

void CrowdEnergyMeter::setRecorder(
    std::shared_ptr<Services::History::SessionHistoryRecorder> recorder) {
    m_recorder = std::move(recorder);
    refresh();
}

void CrowdEnergyMeter::setDatabase(
    std::shared_ptr<Services::Library::TrackDatabase> db) {
    m_db = std::move(db);
    refresh();
}

void CrowdEnergyMeter::timerCallback() {
    refresh();
}

void CrowdEnergyMeter::refresh() {
    m_samples.clear();
    m_currentEnergy = 0.0f;

    if (!m_recorder || !m_db) {
        m_statusLabel->setText(BM_TJ("crowdEnergy.empty"),
                               juce::dontSendNotification);
        repaint();
        return;
    }

    auto events = m_recorder->currentSessionEvents();
    if (events.empty()) {
        m_statusLabel->setText(BM_TJ("crowdEnergy.empty"),
                               juce::dontSendNotification);
        repaint();
        return;
    }

    // Hydrate energies in one SQL roundtrip.
    std::string ids;
    ids.reserve(events.size() * 8);
    for (size_t i = 0; i < events.size(); ++i) {
        if (i) ids.push_back(',');
        ids += std::to_string(events[i].trackId);
    }
    std::unordered_map<int64_t, float> energyById;
    try {
        auto rows = m_db->getTracksByQuery(
            "SELECT * FROM tracks WHERE id IN (" + ids + ")");
        for (auto& t : rows) energyById.emplace(t.id, t.energy);
    } catch (...) {
        // Render with whatever we have (likely zero samples).
    }

    m_samples.reserve(events.size());
    for (const auto& ev : events) {
        Sample s;
        s.playedAt = ev.playedAt;
        auto it = energyById.find(ev.trackId);
        s.energy  = it != energyById.end() ? it->second : 0.0f;
        m_samples.push_back(s);
    }

    if (!m_samples.empty()) m_currentEnergy = m_samples.back().energy;

    juce::String txt = juce::String::formatted(
        BM_T("crowdEnergy.statusFmt").c_str(),
        juce::String(m_currentEnergy, 1).toRawUTF8(),
        zoneNameFor(m_currentEnergy));
    m_statusLabel->setText(txt, juce::dontSendNotification);

    repaint();
}

void CrowdEnergyMeter::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    g.setColour(Colors::bgSurface());
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(Colors::borderLight());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Reserve bottom strip for the status label (positioned in resized()).
    auto plot = bounds.reduced(8.0f, 6.0f);
    plot.removeFromBottom(18.0f);
    if (plot.getHeight() < 10.0f || plot.getWidth() < 10.0f) return;

    auto yForEnergy = [&plot](float e) {
        const float clamped = juce::jlimit(0.0f, 10.0f, e);
        return plot.getBottom() - (clamped / 10.0f) * plot.getHeight();
    };

    const float yWarmTop = yForEnergy(5.0f);
    const float yMainTop = yForEnergy(7.0f);
    const float yTop     = plot.getY();
    const float yBottom  = plot.getBottom();

    g.setColour(Colors::success().withAlpha(0.18f));
    g.fillRect(juce::Rectangle<float>(plot.getX(), yWarmTop, plot.getWidth(), yBottom - yWarmTop));

    g.setColour(Colors::warning().withAlpha(0.18f));
    g.fillRect(juce::Rectangle<float>(plot.getX(), yMainTop, plot.getWidth(), yWarmTop - yMainTop));

    g.setColour(Colors::error().withAlpha(0.20f));
    g.fillRect(juce::Rectangle<float>(plot.getX(), yTop,    plot.getWidth(), yMainTop - yTop));

    if (m_samples.size() < 2) {
        if (m_samples.empty()) {
            g.setColour(Colors::textSecondary());
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
            g.drawText(juce::String::fromUTF8("Aucune donn\xc3\xa9""e de session active"),
                       plot, juce::Justification::centred);
        } else {
            const auto& s = m_samples.front();
            const float x = plot.getCentreX();
            const float y = yForEnergy(s.energy);
            g.setColour(Colors::textPrimary());
            g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }
        return;
    }

    const int64_t t0 = m_samples.front().playedAt;
    const int64_t t1 = m_samples.back().playedAt;
    const double  span = std::max<int64_t>(1, t1 - t0);

    auto xForTime = [&](int64_t ts) {
        const double frac = (double)(ts - t0) / span;
        return (float)(plot.getX() + frac * plot.getWidth());
    };

    juce::Path path;
    path.startNewSubPath(xForTime(m_samples.front().playedAt),
                         yForEnergy(m_samples.front().energy));
    for (size_t i = 1; i < m_samples.size(); ++i) {
        path.lineTo(xForTime(m_samples[i].playedAt),
                    yForEnergy(m_samples[i].energy));
    }
    g.setColour(Colors::textPrimary().withAlpha(0.85f));
    g.strokePath(path, juce::PathStrokeType(1.6f));

    for (const auto& s : m_samples) {
        const float x = xForTime(s.playedAt);
        const float y = yForEnergy(s.energy);
        g.setColour(zoneColour(s.energy));
        g.fillEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }

    const float xCur = xForTime(m_samples.back().playedAt);
    g.setColour(Colors::textPrimary().withAlpha(0.9f));
    g.drawLine(xCur, plot.getY(), xCur, plot.getBottom(), 1.5f);
}

void CrowdEnergyMeter::resized() {
    auto bounds = getLocalBounds().reduced(8, 6);
    if (m_statusLabel)
        m_statusLabel->setBounds(bounds.removeFromBottom(16));
}

} // namespace BeatMate::UI::Widgets
