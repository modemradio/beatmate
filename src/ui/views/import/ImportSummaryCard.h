#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../IRetranslatable.h"

namespace BeatMate::UI {

class ImportSummaryCard : public juce::Component, public IRetranslatable {
public:
    struct Counter {
        juce::String label;
        int count = 0;
        juce::Colour colour;
    };
    struct ErrorLine {
        juce::String file;
        juce::String reason;
    };

    ImportSummaryCard();

    void showSummary(std::vector<Counter> counters, std::vector<ErrorLine> errors,
                     bool offerAnalyze);
    void clearSummary();
    bool hasSummary() const { return m_visible; }

    std::function<void()> onAnalyzeNow;
    std::function<void()> onViewLibrary;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void retranslateUi() override;

private:
    bool m_visible = false;
    std::vector<Counter> m_counters;
    std::vector<ErrorLine> m_errors;
    std::unique_ptr<juce::TextButton> m_analyzeBtn;
    std::unique_ptr<juce::TextButton> m_libraryBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportSummaryCard)
};

} // namespace BeatMate::UI
