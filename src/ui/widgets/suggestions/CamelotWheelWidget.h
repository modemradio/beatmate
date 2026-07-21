#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <string>
#include <array>

namespace BeatMate::UI::Widgets
{
    // Interactive Camelot wheel: outer ring = B (major), inner = A (minor); doubles as library key heatmap
    class CamelotWheelWidget : public juce::Component
    {
    public:
        CamelotWheelWidget();
        ~CamelotWheelWidget() override = default;

        // Empty string clears the selection
        void setCurrentKey(const std::string& camelot);

        // Keys "1A".."12B"; missing entries treated as 0
        void setKeyDensity(const std::map<std::string, int>& densityByKey);

        std::function<void(const std::string& camelot)> onKeyClicked;

        // Empty string on exit
        std::function<void(const std::string& camelot)> onKeyHovered;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;

    private:
        struct Hit
        {
            int  number = 0;   // 1..12
            bool isMajor = false; // true = B (outer), false = A (inner)
            bool valid = false;
        };

        Hit hitTest(juce::Point<float> p) const;
        static std::string codeOf(int number, bool isMajor);
        static bool parseCamelot(const std::string& s, int& number, bool& isMajor);

        enum class Relation
        {
            None,
            Current,
            Adjacent,      // same letter, +/- 1 number
            Relative,      // same number, other letter
            EnergyBoost,   // +3 same letter
            Tritone        // +/- 6
        };

        Relation relationTo(int number, bool isMajor) const;

        int  currentNumber_  = 0;     // 0 means no selection
        bool currentIsMajor_ = false;

        Hit hovered_;

        // index = (number-1)*2 + (isMajor?1:0)
        std::array<int, 24> density_{};
        int maxDensity_ = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CamelotWheelWidget)
    };
} // namespace BeatMate::UI::Widgets
