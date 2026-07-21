#include "RealtimeCoordinator.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Suggestions {

RealtimeCoordinator::RealtimeCoordinator(std::shared_ptr<SuggestionOrchestrator> orchestrator)
    : orchestrator_(std::move(orchestrator)) {
}

RealtimeCoordinator::~RealtimeCoordinator() { stop(); }

void RealtimeCoordinator::start(int updateIntervalMs) {
    startTimer(updateIntervalMs);
    running_ = true;
    spdlog::info("RealtimeCoordinator: Started with {}ms interval", updateIntervalMs);
}

void RealtimeCoordinator::stop() {
    stopTimer();
    running_ = false;
}

void RealtimeCoordinator::setCurrentTrack(const Models::Track& track) {
    currentTrack_ = track;
    onUpdate(); // Immediate update on track change
}

void RealtimeCoordinator::timerCallback() {
    onUpdate();
}

void RealtimeCoordinator::onUpdate() {
    if (currentTrack_.id == 0) return;
    auto suggestions = orchestrator_->getSuggestions(currentTrack_, 10);
    if (callback_) callback_(suggestions);
    if (suggestionsUpdatedCallback_) suggestionsUpdatedCallback_(static_cast<int>(suggestions.size()));
}

} // namespace BeatMate::Services::Suggestions
