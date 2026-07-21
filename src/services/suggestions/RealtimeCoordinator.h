#pragma once
#include <juce_events/juce_events.h>
#include <vector>
#include <memory>
#include <functional>
#include <juce_core/juce_core.h>
#include "SuggestionOrchestrator.h"

namespace BeatMate::Services::Suggestions {

using SuggestionUpdateCallback = std::function<void(const std::vector<RecommendationResult>&)>;

class RealtimeCoordinator : private juce::Timer {
public:
    explicit RealtimeCoordinator(std::shared_ptr<SuggestionOrchestrator> orchestrator);
    ~RealtimeCoordinator() override;

    void start(int updateIntervalMs = 5000);
    void stop();
    void setCurrentTrack(const Models::Track& track);
    void setUpdateCallback(SuggestionUpdateCallback callback) { callback_ = std::move(callback); }
    void setSuggestionsUpdatedCallback(std::function<void(int)> cb) { suggestionsUpdatedCallback_ = std::move(cb); }

private:
    void timerCallback() override;
    void onUpdate();

    std::shared_ptr<SuggestionOrchestrator> orchestrator_;
    Models::Track currentTrack_;
    SuggestionUpdateCallback callback_;
    std::function<void(int)> suggestionsUpdatedCallback_;
    bool running_ = false;
};

}
