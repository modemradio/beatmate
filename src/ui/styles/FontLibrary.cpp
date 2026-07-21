#include "FontLibrary.h"
#include <BeatMateFontsData.h>

namespace BeatMate::UI::Fonts {

namespace {
juce::Typeface::Ptr load(const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor(data, size);
}
} // namespace

juce::Typeface::Ptr ui(Weight weight)
{
    switch (weight)
    {
        case Weight::Medium:
        {
            static juce::Typeface::Ptr t = load(BeatMateFontsData::InterMedium_ttf,
                                                BeatMateFontsData::InterMedium_ttfSize);
            return t;
        }
        case Weight::SemiBold:
        {
            static juce::Typeface::Ptr t = load(BeatMateFontsData::InterSemiBold_ttf,
                                                BeatMateFontsData::InterSemiBold_ttfSize);
            return t;
        }
        case Weight::Bold:
        {
            static juce::Typeface::Ptr t = load(BeatMateFontsData::InterBold_ttf,
                                                BeatMateFontsData::InterBold_ttfSize);
            return t;
        }
        case Weight::Regular:
        default:
        {
            static juce::Typeface::Ptr t = load(BeatMateFontsData::InterRegular_ttf,
                                                BeatMateFontsData::InterRegular_ttfSize);
            return t;
        }
    }
}

juce::Typeface::Ptr mono(Weight weight)
{
    if (weight == Weight::Regular)
    {
        static juce::Typeface::Ptr t = load(BeatMateFontsData::JetBrainsMonoRegular_ttf,
                                            BeatMateFontsData::JetBrainsMonoRegular_ttfSize);
        return t;
    }
    static juce::Typeface::Ptr t = load(BeatMateFontsData::JetBrainsMonoMedium_ttf,
                                        BeatMateFontsData::JetBrainsMonoMedium_ttfSize);
    return t;
}

juce::FontOptions uiFont(float height, Weight weight)
{
    return juce::FontOptions(ui(weight)).withHeight(height);
}

juce::FontOptions monoFont(float height, Weight weight)
{
    return juce::FontOptions(mono(weight)).withHeight(height);
}

} // namespace BeatMate::UI::Fonts
