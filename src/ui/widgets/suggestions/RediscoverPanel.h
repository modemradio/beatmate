#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

#include "../../../models/Track.h"

namespace BeatMate::Services::Library      { class TrackDatabase; }
namespace BeatMate::Services::DJSoftware    { class SendToDJRouter; }
namespace BeatMate::Services::Suggestions   { class SmartSuggestEngine; }
namespace BeatMate::Services::Preparation   { class CamelotMoveClassifier; }

namespace BeatMate::UI::Widgets {

class RediscoverPanel : public juce::Component,
                        private juce::Timer {
public:
    RediscoverPanel();
    ~RediscoverPanel() override;

    void setDatabase(Services::Library::TrackDatabase* db);
    void setRouter(Services::DJSoftware::SendToDJRouter* router);
    void setSmartEngine(Services::Suggestions::SmartSuggestEngine* engine);

    void onCurrentTrackChanged();

    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    enum class Mode {
        NeverPlayed      = 1,
        ForgottenOver90d = 2,
        LeastPlayed      = 3,
        LeastPlayedGenre = 4
    };

    struct Entry {
        Models::Track track;
    };

    class Row;
    class RowContainer;

    void timerCallback() override;

    void rebuildRows();
    std::vector<Entry> fetchEntries() const;
    void populateGenreCombo();
    Mode currentMode() const;
    juce::String currentGenre() const;
    bool passesCompatibilityFilter(const Models::Track& t) const;

    Services::Library::TrackDatabase*           db_ = nullptr;
    Services::DJSoftware::SendToDJRouter*       router_ = nullptr;
    Services::Suggestions::SmartSuggestEngine*  smartEngine_ = nullptr;
    std::unique_ptr<Services::Preparation::CamelotMoveClassifier> classifier_;

    std::vector<Entry> entries_;

    std::unique_ptr<juce::Label>        header_;
    std::unique_ptr<juce::Label>        modeLbl_;
    std::unique_ptr<juce::ComboBox>     modeCb_;
    std::unique_ptr<juce::Label>        genreLbl_;
    std::unique_ptr<juce::ComboBox>     genreCb_;
    std::unique_ptr<juce::ToggleButton> compatToggle_;
    std::unique_ptr<juce::TextButton>   refreshBtn_;

    std::unique_ptr<juce::Viewport>     viewport_;
    std::unique_ptr<RowContainer>       container_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RediscoverPanel)
};

} // namespace BeatMate::UI::Widgets
