#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "../../../services/suggestions/SmartSuggestEngine.h"
#include "CamelotWheelWidget.h"

#include <string>

namespace BeatMate::Services::Suggestions { class DJProfileService; }

namespace BeatMate::UI::Widgets {

// Reusable panel that lists SmartSuggest results for the current deck track.
class SuggestionPanel : public juce::Component {
public:
    SuggestionPanel();
    ~SuggestionPanel() override;

    // Branche le moteur (non possedant) ; refresh() a la charge de l'hote
    void setEngine(Services::Suggestions::SmartSuggestEngine* engine);

    void refresh();

    void setCurrentCamelotKey(const std::string& camelot);

    void setPlaceholderVisible(bool visible);

    void setProfileContext(Services::Suggestions::DJProfileService* svc,
                           std::string activeProfileName);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class SuggestionCard;
    class CardContainer;

    void rebuildCards();

    void startExploration(const Services::Suggestions::Suggestion& s);
    void exitExploration();

    Services::Suggestions::SmartSuggestEngine* engine_ = nullptr;
    Services::Suggestions::DJProfileService* profileService_ = nullptr;
    std::string activeProfileName_;

    bool toggleFavorite(int64_t trackId);

    void skipTrack(int64_t trackId);

    bool isFavorite(int64_t trackId) const;

    bool toggleAssociation(int64_t trackId);
    bool isAssociated(int64_t trackId) const;
    void reloadAssociationsFromProfile();

    std::vector<Services::Suggestions::Suggestion> results_;
    bool placeholder_ = true;
    std::atomic<uint64_t> refreshGen_ { 0 };

    std::string curKey_;
    std::string wheelFocusKey_;
    double curBpm_    = 0.0;
    double curEnergy_ = 0.0;

    std::unordered_map<int64_t, juce::String> explainById_;

    bool    exploring_       = false;
    int64_t explorationId_   = 0;
    int64_t liveTrackId_     = 0;
    juce::String explorationTitle_;

    // Weights (sum = 1.0) — harmonic, bpm, energy, style, trending, structure
    std::array<double, 6> weights_ = {0.35, 0.25, 0.20, 0.10, 0.05, 0.05};

    std::unique_ptr<juce::Label> header_;

    std::unique_ptr<juce::Label>      exploreLbl_;
    std::unique_ptr<juce::TextButton> exploreBackBtn_;
    std::unique_ptr<juce::TextButton> refreshBtn_;
    std::unique_ptr<CamelotWheelWidget> wheel_;

    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<CardContainer> container_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SuggestionPanel)
};

} // namespace BeatMate::UI::Widgets
