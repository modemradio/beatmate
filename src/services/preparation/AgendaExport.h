#pragma once
#include <string>
#include <vector>
#include "EventPlanner.h"

namespace BeatMate::Services::Preparation {

std::string buildAgendaHtml(const std::vector<EventPlan>& events);
std::string buildAgendaCsv(const std::vector<EventPlan>& events);
std::string buildAgendaPdf(const std::vector<EventPlan>& events);
std::string buildAgendaDocx(const std::vector<EventPlan>& events);

}
