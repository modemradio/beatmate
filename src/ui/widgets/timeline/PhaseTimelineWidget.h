#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../models/Track.h"

namespace BeatMate::UI::Widgets {

enum class EventPhase : int {
    WarmUp   = 0,
    Buildup  = 1,
    Peak     = 2,
    CoolDown = 3,
    Closing  = 4
};

struct PhaseSlot {
    EventPhase phase = EventPhase::WarmUp;
    double startMinutes    = 0.0;  // absolute minute from event start
    double durationMinutes = 0.0;
    std::vector<int64_t> trackIds; // tracks assigned to this slot
};

class PhaseTimelineWidget : public juce::Component
{
public:
    PhaseTimelineWidget();
    ~PhaseTimelineWidget() override = default;

    void setSlots(const std::vector<PhaseSlot>& slots);
    std::vector<PhaseSlot> getSlots() const { return m_slots; }

    void setEventDurationMinutes(double minutes);
    double getEventDurationMinutes() const { return m_eventDurationMinutes; }

    void setTrackLookup(std::function<Models::Track(int64_t)> lookup);

    static EventPhase roleToPhase(const std::string& role);
    static juce::String phaseName(EventPhase p);
    static juce::Colour phaseColour(EventPhase p);

    std::function<void()> onSlotsChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    static constexpr int kNumLanes     = 5;
    static constexpr int kTimeAxisH    = 24;
    static constexpr int kLaneLabelW   = 90;
    static constexpr int kLaneMinH     = 48;
    static constexpr int kTrackPadY    = 4;

    juce::Rectangle<int> timelineArea() const;
    juce::Rectangle<int> laneArea(int laneIndex) const;
    int laneAtY(int y) const;                      // -1 if outside
    double minutesAtX(int x) const;                // clamped
    int xAtMinutes(double minutes) const;
    double trackDurationMinutes(int64_t trackId) const;

    struct HitResult { int slotIndex = -1; int trackIndex = -1; };
    HitResult hitTestTrack(juce::Point<int> p) const;

    juce::Rectangle<int> trackRect(int slotIndex, int trackIndex) const;

    void drawTimeAxis(juce::Graphics& g, juce::Rectangle<int> area);
    void drawLanes(juce::Graphics& g);
    void drawSlotTracks(juce::Graphics& g);
    void drawDragGhost(juce::Graphics& g);

    std::vector<PhaseSlot> m_slots;
    double m_eventDurationMinutes = 240.0; // 4h default
    std::function<Models::Track(int64_t)> m_trackLookup;

    bool m_dragging = false;
    int  m_dragSlot = -1;
    int  m_dragIndex = -1;
    int64_t m_dragTrackId = 0;
    juce::Point<int> m_dragStartMouse;
    juce::Rectangle<int> m_dragStartRect;
    juce::Point<int> m_dragMouseOffset;  // offset from rect top-left to mouse
    juce::Rectangle<int> m_ghostRect;
    int m_targetLane = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseTimelineWidget)
};

} // namespace BeatMate::UI::Widgets
