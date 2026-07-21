#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "../../styles/ColorPalette.h"

namespace BeatMate::UI::AnalysisColumns {

enum class Col { Check = 0, Title, Artist, Status, Bpm, Key, Energy, Lufs, Progress, Count };

struct ColumnSpec {
    Col col;
    int width;
    const char* i18nKey;
    juce::Justification::Flags justification;
    bool sortable;
};

inline const std::array<ColumnSpec, 9>& specs()
{
    static const std::array<ColumnSpec, 9> s{{
        { Col::Check,     30, "",                    juce::Justification::centred,      false },
        { Col::Title,      0, "analysis.col.title",  juce::Justification::centredLeft,  true  },
        { Col::Artist,   160, "analysis.col.artist", juce::Justification::centredLeft,  true  },
        { Col::Status,   110, "analysis.col.status", juce::Justification::centredLeft,  true  },
        { Col::Bpm,       90, "analysis.col.bpm",    juce::Justification::centred,      true  },
        { Col::Key,       70, "analysis.col.key",    juce::Justification::centred,      true  },
        { Col::Energy,   110, "analysis.col.energy", juce::Justification::centred,      true  },
        { Col::Lufs,      70, "analysis.col.lufs",   juce::Justification::centred,      true  },
        { Col::Progress,  90, "analysis.col.progress", juce::Justification::centred,    true  },
    }};
    return s;
}

inline juce::Rectangle<int> columnBounds(Col col, int totalWidth, int height)
{
    int fixed = 0;
    for (const auto& sp : specs())
        fixed += sp.width;
    const int flexW = juce::jmax(120, totalWidth - fixed);

    int x = 0;
    for (const auto& sp : specs()) {
        const int w = (sp.width == 0) ? flexW : sp.width;
        if (sp.col == col)
            return { x, 0, w, height };
        x += w;
    }
    return {};
}

inline Col columnAt(int x, int totalWidth)
{
    for (const auto& sp : specs()) {
        auto b = columnBounds(sp.col, totalWidth, 1);
        if (x >= b.getX() && x < b.getRight())
            return sp.col;
    }
    return Col::Count;
}

inline juce::Colour energyColour(float value01)
{
    value01 = juce::jlimit(0.0f, 1.0f, value01);
    if (value01 < 0.5f)
        return Colors::success().interpolatedWith(Colors::energyMedium(), value01 * 2.0f);
    return Colors::energyMedium().interpolatedWith(Colors::energyHigh(), (value01 - 0.5f) * 2.0f);
}

inline juce::Colour camelotColour(const juce::String& camelot)
{
    const int num = camelot.initialSectionContainingOnly("0123456789").getIntValue();
    if (num < 1 || num > 12)
        return Colors::keyBadge();
    const float hue = static_cast<float>(num - 1) / 12.0f;
    const bool major = camelot.endsWithIgnoreCase("B");
    return juce::Colour::fromHSV(hue, major ? 0.62f : 0.52f, major ? 0.92f : 0.78f, 1.0f);
}

} // namespace BeatMate::UI::AnalysisColumns
