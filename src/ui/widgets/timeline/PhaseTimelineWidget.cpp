#include "PhaseTimelineWidget.h"

#include <algorithm>
#include <cmath>

namespace BeatMate::UI::Widgets {


EventPhase PhaseTimelineWidget::roleToPhase(const std::string& role)
{
    if (role == "Opener") return EventPhase::WarmUp;
    if (role == "Peak")   return EventPhase::Peak;
    if (role == "Closer") return EventPhase::Closing;
    if (role == "Filler") return EventPhase::Buildup;
    return EventPhase::Buildup;
}

juce::String PhaseTimelineWidget::phaseName(EventPhase p)
{
    switch (p) {
        case EventPhase::WarmUp:   return "Warm Up";
        case EventPhase::Buildup:  return "Buildup";
        case EventPhase::Peak:     return "Peak";
        case EventPhase::CoolDown: return "Cool Down";
        case EventPhase::Closing:  return "Closing";
    }
    return {};
}

juce::Colour PhaseTimelineWidget::phaseColour(EventPhase p)
{
    switch (p) {
        case EventPhase::WarmUp:   return juce::Colour(0xff3b82f6);
        case EventPhase::Buildup:  return juce::Colour(0xff10b981);
        case EventPhase::Peak:     return juce::Colour(0xfff59e0b);
        case EventPhase::CoolDown: return juce::Colour(0xff8b5cf6);
        case EventPhase::Closing:  return juce::Colour(0xffef4444);
    }
    return juce::Colours::grey;
}


PhaseTimelineWidget::PhaseTimelineWidget()
{
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(true, false);

    m_slots.reserve(5);
    for (int i = 0; i < kNumLanes; ++i) {
        PhaseSlot s;
        s.phase = static_cast<EventPhase>(i);
        s.startMinutes = (m_eventDurationMinutes / kNumLanes) * i;
        s.durationMinutes = m_eventDurationMinutes / kNumLanes;
        m_slots.push_back(s);
    }
}

void PhaseTimelineWidget::setSlots(const std::vector<PhaseSlot>& slots)
{
    m_slots = slots;
    repaint();
}

void PhaseTimelineWidget::setEventDurationMinutes(double minutes)
{
    m_eventDurationMinutes = std::max(1.0, minutes);
    repaint();
}

void PhaseTimelineWidget::setTrackLookup(std::function<Models::Track(int64_t)> lookup)
{
    m_trackLookup = std::move(lookup);
    repaint();
}


juce::Rectangle<int> PhaseTimelineWidget::timelineArea() const
{
    auto b = getLocalBounds();
    b.removeFromLeft(kLaneLabelW);
    b.removeFromTop(kTimeAxisH);
    return b;
}

juce::Rectangle<int> PhaseTimelineWidget::laneArea(int laneIndex) const
{
    auto area = timelineArea();
    const int laneH = std::max(kLaneMinH, area.getHeight() / kNumLanes);
    const int totalH = laneH * kNumLanes;
    const int yTop = area.getY() + std::max(0, (area.getHeight() - totalH) / 2);
    return juce::Rectangle<int>(area.getX(), yTop + laneIndex * laneH,
                                area.getWidth(), laneH);
}

int PhaseTimelineWidget::laneAtY(int y) const
{
    for (int i = 0; i < kNumLanes; ++i) {
        auto la = laneArea(i);
        if (y >= la.getY() && y < la.getBottom())
            return i;
    }
    return -1;
}

double PhaseTimelineWidget::minutesAtX(int x) const
{
    auto area = timelineArea();
    if (area.getWidth() <= 0) return 0.0;
    double n = double(x - area.getX()) / double(area.getWidth());
    n = juce::jlimit(0.0, 1.0, n);
    return n * m_eventDurationMinutes;
}

int PhaseTimelineWidget::xAtMinutes(double minutes) const
{
    auto area = timelineArea();
    double n = juce::jlimit(0.0, 1.0, minutes / std::max(1.0, m_eventDurationMinutes));
    return area.getX() + int(std::round(n * area.getWidth()));
}

double PhaseTimelineWidget::trackDurationMinutes(int64_t trackId) const
{
    if (m_trackLookup) {
        auto t = m_trackLookup(trackId);
        if (t.duration > 0.0) return t.duration / 60.0;
    }
    return 4.0;
}

juce::Rectangle<int> PhaseTimelineWidget::trackRect(int slotIndex, int trackIndex) const
{
    if (slotIndex < 0 || slotIndex >= (int)m_slots.size()) return {};
    const auto& slot = m_slots[slotIndex];
    if (trackIndex < 0 || trackIndex >= (int)slot.trackIds.size()) return {};

    auto lane = laneArea(static_cast<int>(slot.phase));
    lane.reduce(0, kTrackPadY);

    double cursor = slot.startMinutes;
    for (int i = 0; i < trackIndex; ++i)
        cursor += trackDurationMinutes(slot.trackIds[i]);
    const double dur = trackDurationMinutes(slot.trackIds[trackIndex]);

    const int x1 = xAtMinutes(cursor);
    const int x2 = xAtMinutes(cursor + dur);
    return juce::Rectangle<int>(x1 + 1, lane.getY() + 2,
                                std::max(18, x2 - x1 - 2),
                                lane.getHeight() - 4);
}

PhaseTimelineWidget::HitResult
PhaseTimelineWidget::hitTestTrack(juce::Point<int> p) const
{
    for (int s = 0; s < (int)m_slots.size(); ++s) {
        for (int t = 0; t < (int)m_slots[s].trackIds.size(); ++t) {
            if (trackRect(s, t).contains(p))
                return { s, t };
        }
    }
    return {};
}


void PhaseTimelineWidget::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1d24));

    drawTimeAxis(g, { 0, 0, getWidth(), kTimeAxisH });
    drawLanes(g);
    drawSlotTracks(g);
    if (m_dragging) drawDragGhost(g);
}

void PhaseTimelineWidget::drawTimeAxis(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff12141a));
    g.fillRect(area);

    g.setColour(juce::Colour(0xff2a2f3a));
    g.drawLine((float)kLaneLabelW, (float)area.getBottom() - 0.5f,
               (float)getWidth(),  (float)area.getBottom() - 0.5f, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.setFont(juce::Font(11.0f));

    double step = 15.0;
    if (m_eventDurationMinutes <= 60.0)   step = 5.0;
    else if (m_eventDurationMinutes <= 180.0) step = 10.0;
    else if (m_eventDurationMinutes <= 360.0) step = 30.0;
    else step = 60.0;

    auto tl = timelineArea();
    for (double m = 0.0; m <= m_eventDurationMinutes + 0.001; m += step) {
        int x = xAtMinutes(m);
        g.setColour(juce::Colour(0xff2a2f3a));
        g.drawLine((float)x, (float)area.getY() + 14.0f,
                   (float)x, (float)area.getBottom(), 1.0f);

        juce::String label;
        const int hh = int(m) / 60;
        const int mm = int(m) % 60;
        if (hh > 0) label = juce::String::formatted("%dh%02d", hh, mm);
        else        label = juce::String::formatted("%dm", mm);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawText(label,
                   juce::Rectangle<int>(x - 24, area.getY(), 48, 14),
                   juce::Justification::centred, false);

        g.setColour(juce::Colours::white.withAlpha(0.04f));
        g.drawLine((float)x, (float)tl.getY(),
                   (float)x, (float)tl.getBottom(), 1.0f);
    }
}

void PhaseTimelineWidget::drawLanes(juce::Graphics& g)
{
    for (int i = 0; i < kNumLanes; ++i) {
        auto lane = laneArea(i);
        const auto ph = static_cast<EventPhase>(i);
        const auto col = phaseColour(ph);

        g.setColour((i % 2 == 0) ? juce::Colour(0xff202430)
                                 : juce::Colour(0xff1c2028));
        g.fillRect(lane);

        if (m_dragging && m_targetLane == i) {
            g.setColour(col.withAlpha(0.18f));
            g.fillRect(lane);
            g.setColour(col.withAlpha(0.9f));
            g.drawRect(lane, 2);
        }

        juce::Rectangle<int> label(0, lane.getY(), kLaneLabelW, lane.getHeight());
        g.setColour(col.withAlpha(0.18f));
        g.fillRect(label.reduced(3, 2));

        g.setColour(col);
        g.fillRect(label.getX() + 3, label.getY() + 2, 3, label.getHeight() - 4);

        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(phaseName(ph),
                   label.reduced(12, 0),
                   juce::Justification::centredLeft, false);

        g.setColour(juce::Colour(0xff2a2f3a));
        g.drawLine((float)lane.getX(), (float)lane.getBottom() - 0.5f,
                   (float)lane.getRight(), (float)lane.getBottom() - 0.5f, 1.0f);
    }
}

void PhaseTimelineWidget::drawSlotTracks(juce::Graphics& g)
{
    for (int s = 0; s < (int)m_slots.size(); ++s) {
        const auto& slot = m_slots[s];
        const auto col = phaseColour(slot.phase);

        for (int t = 0; t < (int)slot.trackIds.size(); ++t) {
            if (m_dragging && s == m_dragSlot && t == m_dragIndex)
                continue;

            auto r = trackRect(s, t);
            if (r.isEmpty()) continue;

            juce::Colour fill = col.withAlpha(0.85f);
            juce::Colour border = col.brighter(0.3f);

            g.setColour(fill);
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
            g.setColour(border);
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.2f);

            juce::String title = juce::String(slot.trackIds[t]);
            if (m_trackLookup) {
                auto tr = m_trackLookup(slot.trackIds[t]);
                if (!tr.title.empty()) {
                    title = juce::String(tr.title);
                    if (!tr.artist.empty())
                        title = juce::String(tr.artist) + " - " + title;
                }
            }

            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(title, r.reduced(6, 2),
                       juce::Justification::centredLeft, true);
        }
    }
}

void PhaseTimelineWidget::drawDragGhost(juce::Graphics& g)
{
    if (m_ghostRect.isEmpty()) return;

    auto phase = (m_targetLane >= 0 && m_targetLane < kNumLanes)
                   ? static_cast<EventPhase>(m_targetLane)
                   : (m_dragSlot >= 0 ? m_slots[m_dragSlot].phase : EventPhase::WarmUp);
    auto col = phaseColour(phase);

    g.setColour(col.withAlpha(0.55f));
    g.fillRoundedRectangle(m_ghostRect.toFloat(), 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawRoundedRectangle(m_ghostRect.toFloat(), 5.0f, 1.5f);

    juce::String title = juce::String(m_dragTrackId);
    if (m_trackLookup) {
        auto tr = m_trackLookup(m_dragTrackId);
        if (!tr.title.empty()) title = juce::String(tr.title);
    }
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText(title, m_ghostRect.reduced(6, 2),
               juce::Justification::centredLeft, true);
}

void PhaseTimelineWidget::resized()
{
    repaint();
}


void PhaseTimelineWidget::mouseDown(const juce::MouseEvent& e)
{
    auto hit = hitTestTrack(e.getPosition());
    if (hit.slotIndex < 0) return;

    m_dragging = true;
    m_dragSlot = hit.slotIndex;
    m_dragIndex = hit.trackIndex;
    m_dragTrackId = m_slots[hit.slotIndex].trackIds[hit.trackIndex];

    m_dragStartRect = trackRect(hit.slotIndex, hit.trackIndex);
    m_dragStartMouse = e.getPosition();
    m_dragMouseOffset = e.getPosition() - m_dragStartRect.getTopLeft();
    m_ghostRect = m_dragStartRect;
    m_targetLane = static_cast<int>(m_slots[hit.slotIndex].phase);

    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void PhaseTimelineWidget::mouseDrag(const juce::MouseEvent& e)
{
    if (!m_dragging) return;

    auto p = e.getPosition();
    m_ghostRect.setPosition(p.x - m_dragMouseOffset.x,
                            p.y - m_dragMouseOffset.y);

    int lane = -1;
    for (int i = 0; i < kNumLanes; ++i)
        if (laneArea(i).contains(juce::Point<int>(std::max(p.x, kLaneLabelW + 1), p.y)))
            { lane = i; break; }

    if (lane >= 0) {
        m_targetLane = lane;
        auto la = laneArea(lane).reduced(0, kTrackPadY);
        m_ghostRect.setY(la.getY() + 2);
        m_ghostRect.setHeight(la.getHeight() - 4);
    }

    repaint();
}

void PhaseTimelineWidget::mouseUp(const juce::MouseEvent& e)
{
    if (!m_dragging) { setMouseCursor(juce::MouseCursor::NormalCursor); return; }

    const bool validTarget = (m_targetLane >= 0 && m_targetLane < kNumLanes)
                          && m_dragSlot >= 0
                          && m_dragIndex >= 0;

    if (validTarget) {
        const double newMinute = minutesAtX(m_ghostRect.getX());
        const auto targetPhase = static_cast<EventPhase>(m_targetLane);

        int destSlot = -1;
        for (int i = 0; i < (int)m_slots.size(); ++i) {
            if (m_slots[i].phase == targetPhase) { destSlot = i; break; }
        }
        if (destSlot < 0) {
            PhaseSlot ns;
            ns.phase = targetPhase;
            ns.startMinutes = newMinute;
            ns.durationMinutes = std::max(5.0, trackDurationMinutes(m_dragTrackId));
            m_slots.push_back(ns);
            destSlot = (int)m_slots.size() - 1;
        }

        if (m_dragSlot >= 0 && m_dragSlot < (int)m_slots.size()) {
            auto& srcIds = m_slots[m_dragSlot].trackIds;
            if (m_dragIndex >= 0 && m_dragIndex < (int)srcIds.size())
                srcIds.erase(srcIds.begin() + m_dragIndex);
        }

        auto& destIds = m_slots[destSlot].trackIds;

        double cursor = m_slots[destSlot].startMinutes;
        int insertIdx = (int)destIds.size();
        for (int i = 0; i < (int)destIds.size(); ++i) {
            const double d = trackDurationMinutes(destIds[i]);
            if (newMinute < cursor + d * 0.5) { insertIdx = i; break; }
            cursor += d;
        }
        destIds.insert(destIds.begin() + insertIdx, m_dragTrackId);

        const double totalNeeded = [&](){
            double acc = 0.0;
            for (auto id : destIds) acc += trackDurationMinutes(id);
            return acc;
        }();
        m_slots[destSlot].durationMinutes =
            std::max(m_slots[destSlot].durationMinutes, totalNeeded);

        if (onSlotsChanged) onSlotsChanged();
    }

    m_dragging = false;
    m_dragSlot = -1;
    m_dragIndex = -1;
    m_dragTrackId = 0;
    m_ghostRect = {};
    m_targetLane = -1;

    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

}
