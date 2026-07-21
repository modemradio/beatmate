#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "EventPlanner.h"

namespace BeatMate::Services::Preparation {

// Génère un calendrier iCalendar (RFC 5545) à partir des dates de l'agenda,
juce::String buildIcsCalendar(const std::vector<EventPlan>& events);

// Lien "Ajouter à Google Agenda" pour un événement (ouvre le navigateur).
juce::String googleCalendarUrl(const EventPlan& e);

// Importe des événements depuis un fichier .ics (VEVENT basiques).
std::vector<EventPlan> parseIcsCalendar(const juce::String& icsText);

} // namespace BeatMate::Services::Preparation
