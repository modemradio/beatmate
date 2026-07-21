#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <cmath>
#include "FontLibrary.h"

namespace BeatMate::UI {

namespace Colors {

enum class Tok : int {
    Bg, BgSidebar, BgSurface, BgCard, BgElevated, BgLighter, BgLightest,
    Primary, PrimaryHover, PrimaryDark, Secondary, Accent, BorderFocus,
    Success, Warning, Error,
    TextPrimary, TextSecondary, TextMuted, TextDim, TextDisabled,
    WaveBass, WaveMid, WaveTreble,
    StemVocals, StemDrums, StemBass, StemOther,
    Cue1, Cue2, Cue3, Cue4, Cue5, Cue6, Cue7, Cue8,
    EnergyMedium, EnergyHigh,
    VuGreen, VuYellow, VuRed, GainReduction,
    Border, BorderLight,
    StarFilled, StarEmpty,
    ModuleHome, ModuleLibrary, ModuleAnalysis, ModuleHotcues, ModuleImport,
    ModuleExport, ModuleNormalize, ModulePrep, ModuleLive, ModuleStreaming,
    ModuleMixlab, ModuleSettings,
    Count
};

namespace detail {
inline std::atomic<juce::uint32>* table()
{
    static std::atomic<juce::uint32> t[static_cast<size_t>(Tok::Count)];
    static const bool initd = [&] {
        auto set = [&](Tok tok, juce::uint32 argb) {
            t[static_cast<size_t>(tok)].store(argb, std::memory_order_relaxed);
        };
        set(Tok::Bg, 0xFF0A0E14);          set(Tok::BgSidebar, 0xFF0C1117);
        set(Tok::BgSurface, 0xFF0F1419);   set(Tok::BgCard, 0xFF161B22);
        set(Tok::BgElevated, 0xFF1C222B);  set(Tok::BgLighter, 0xFF2E2E44);
        set(Tok::BgLightest, 0xFF3A3A52);
        set(Tok::Primary, 0xFF3B82F6);     set(Tok::PrimaryHover, 0xFF60A5FA);
        set(Tok::PrimaryDark, 0xFF2563EB); set(Tok::Secondary, 0xFF8B5CF6);
        set(Tok::Accent, 0xFF06B6D4);      set(Tok::BorderFocus, 0xFF3B82F6);
        set(Tok::Success, 0xFF10B981);     set(Tok::Warning, 0xFFF59E0B);
        set(Tok::Error, 0xFFEF4444);
        set(Tok::TextPrimary, 0xFFF1F5F9); set(Tok::TextSecondary, 0xFF64748B);
        set(Tok::TextMuted, 0xFF475569);   set(Tok::TextDim, 0xFF334155);
        set(Tok::TextDisabled, 0xFF1E293B);
        set(Tok::WaveBass, 0xFFEF4444);    set(Tok::WaveMid, 0xFF22C55E);
        set(Tok::WaveTreble, 0xFF3B82F6);
        set(Tok::StemVocals, 0xFFF472B6);  set(Tok::StemDrums, 0xFF10B981);
        set(Tok::StemBass, 0xFFF59E0B);    set(Tok::StemOther, 0xFF8B5CF6);
        set(Tok::Cue1, 0xFFEF4444);        set(Tok::Cue2, 0xFFF97316);
        set(Tok::Cue3, 0xFFF59E0B);        set(Tok::Cue4, 0xFF22C55E);
        set(Tok::Cue5, 0xFF06B6D4);        set(Tok::Cue6, 0xFF3B82F6);
        set(Tok::Cue7, 0xFF8B5CF6);        set(Tok::Cue8, 0xFFEC4899);
        set(Tok::EnergyMedium, 0xFFF59E0B); set(Tok::EnergyHigh, 0xFFEF4444);
        set(Tok::VuGreen, 0xFF22C55E);     set(Tok::VuYellow, 0xFFFCD34D);
        set(Tok::VuRed, 0xFFEF4444);       set(Tok::GainReduction, 0xFFFB923C);
        set(Tok::Border, 0xFF1E293B);      set(Tok::BorderLight, 0xFF334155);
        set(Tok::StarFilled, 0xFFF59E0B);  set(Tok::StarEmpty, 0xFF1E293B);
        set(Tok::ModuleHome, 0xFF3B82F6);      set(Tok::ModuleLibrary, 0xFF22D3EE);
        set(Tok::ModuleAnalysis, 0xFF8B5CF6);  set(Tok::ModuleHotcues, 0xFFEC4899);
        set(Tok::ModuleImport, 0xFF10B981);    set(Tok::ModuleExport, 0xFFF97316);
        set(Tok::ModuleNormalize, 0xFF14B8A6); set(Tok::ModulePrep, 0xFFEAB308);
        set(Tok::ModuleLive, 0xFFEF4444);      set(Tok::ModuleStreaming, 0xFF60A5FA);
        set(Tok::ModuleMixlab, 0xFF6366F1);    set(Tok::ModuleSettings, 0xFF94A3B8);
        return true;
    }();
    juce::ignoreUnused(initd);
    return t;
}
} // namespace detail

inline juce::Colour token(Tok t)
{
    return juce::Colour(detail::table()[static_cast<size_t>(t)].load(std::memory_order_relaxed));
}

inline void setToken(Tok t, juce::uint32 argb)
{
    detail::table()[static_cast<size_t>(t)].store(argb, std::memory_order_relaxed);
}

inline void setToken(Tok t, juce::Colour c) { setToken(t, c.getARGB()); }

namespace Dynamic {
    inline void setAccentColor(juce::uint32 argb) {
        setToken(Tok::Primary, argb);
        setToken(Tok::PrimaryHover, juce::Colour(argb).brighter(0.25f).getARGB());
        setToken(Tok::PrimaryDark, juce::Colour(argb).darker(0.25f).getARGB());
        setToken(Tok::Accent, juce::Colour(argb).withAlpha(0.85f).getARGB());
        setToken(Tok::BorderFocus, argb);
    }
}

inline juce::Colour bg()             { return token(Tok::Bg); }
inline juce::Colour bgSidebar()      { return token(Tok::BgSidebar); }
inline juce::Colour bgSurface()      { return token(Tok::BgSurface); }
inline juce::Colour bgCard()         { return token(Tok::BgCard); }
inline juce::Colour bgElevated()     { return token(Tok::BgElevated); }

inline juce::Colour bgDarkest()      { return bg(); }
inline juce::Colour bgDarker()       { return bgSidebar(); }
inline juce::Colour bgDark()         { return bgSurface(); }
inline juce::Colour bgMedium()       { return bgCard(); }
inline juce::Colour bgLight()        { return bgElevated(); }
inline juce::Colour bgLighter()      { return token(Tok::BgLighter); }
inline juce::Colour bgLightest()     { return token(Tok::BgLightest); }

inline juce::Colour glassWhite()     { return juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.03f); }
inline juce::Colour glassBorder()    { return juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.06f); }
inline juce::Colour glassHover()     { return juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, 0.08f); }

inline juce::Colour primary()        { return token(Tok::Primary); }
inline juce::Colour primaryHover()   { return token(Tok::PrimaryHover); }
inline juce::Colour primaryDark()    { return token(Tok::PrimaryDark); }
inline juce::Colour secondary()      { return token(Tok::Secondary); }
inline juce::Colour secondaryHover() { return token(Tok::Secondary).brighter(0.2f); }
inline juce::Colour accent()         { return token(Tok::Accent); }

inline juce::Colour success()        { return token(Tok::Success); }
inline juce::Colour warning()        { return token(Tok::Warning); }
inline juce::Colour error()          { return token(Tok::Error); }
inline juce::Colour info()           { return primary(); }

inline juce::Colour textPrimary()    { return token(Tok::TextPrimary); }
inline juce::Colour textSecondary()  { return token(Tok::TextSecondary); }
inline juce::Colour textMuted()      { return token(Tok::TextMuted); }
inline juce::Colour textDim()        { return token(Tok::TextDim); }
inline juce::Colour textDisabled()   { return token(Tok::TextDisabled); }

inline juce::Colour waveformBass()    { return token(Tok::WaveBass); }
inline juce::Colour waveformMid()     { return token(Tok::WaveMid); }
inline juce::Colour waveformTreble()  { return token(Tok::WaveTreble); }
inline juce::Colour waveformDefault() { return accent(); }

inline juce::Colour stemVocals()     { return token(Tok::StemVocals); }
inline juce::Colour stemDrums()      { return token(Tok::StemDrums); }
inline juce::Colour stemBass()       { return token(Tok::StemBass); }
inline juce::Colour stemOther()      { return token(Tok::StemOther); }

inline juce::Colour cue(int index) {
    const int i = juce::jlimit(0, 7, index);
    return token(static_cast<Tok>(static_cast<int>(Tok::Cue1) + i));
}

inline juce::Colour energyLow()      { return primary(); }
inline juce::Colour energyMedium()   { return token(Tok::EnergyMedium); }
inline juce::Colour energyHigh()     { return token(Tok::EnergyHigh); }

inline juce::Colour energy(float v01)
{
    v01 = juce::jlimit(0.0f, 1.0f, v01);
    if (v01 < 0.5f)
        return energyLow().interpolatedWith(energyMedium(), v01 * 2.0f);
    return energyMedium().interpolatedWith(energyHigh(), (v01 - 0.5f) * 2.0f);
}

inline juce::Colour camelot(int number, bool isMinor)
{
    const int n = ((number - 1) % 12 + 12) % 12;
    const float hue = std::fmod(0.38f + static_cast<float>(n) / 12.0f, 1.0f);
    return juce::Colour::fromHSV(hue, isMinor ? 0.52f : 0.72f, isMinor ? 0.82f : 0.95f, 1.0f);
}

inline juce::Colour vuGreen()        { return token(Tok::VuGreen); }
inline juce::Colour vuYellow()       { return token(Tok::VuYellow); }
inline juce::Colour vuRed()          { return token(Tok::VuRed); }
inline juce::Colour gainReduction()  { return token(Tok::GainReduction); }

inline juce::Colour border()         { return token(Tok::Border); }
inline juce::Colour borderLight()    { return token(Tok::BorderLight); }
inline juce::Colour borderFocus()    { return token(Tok::BorderFocus); }

inline juce::Colour starFilled()     { return token(Tok::StarFilled); }
inline juce::Colour starEmpty()      { return token(Tok::StarEmpty); }
inline juce::Colour starBorder()     { return token(Tok::StarFilled); }

inline juce::Colour bpmBadge()       { return primary(); }
inline juce::Colour keyBadge()       { return secondary(); }
inline juce::Colour energyBadge()    { return token(Tok::EnergyMedium); }

inline juce::Colour moduleHome()      { return token(Tok::ModuleHome); }
inline juce::Colour moduleLibrary()   { return token(Tok::ModuleLibrary); }
inline juce::Colour moduleAnalysis()  { return token(Tok::ModuleAnalysis); }
inline juce::Colour moduleHotcues()   { return token(Tok::ModuleHotcues); }
inline juce::Colour moduleImport()    { return token(Tok::ModuleImport); }
inline juce::Colour moduleExport()    { return token(Tok::ModuleExport); }
inline juce::Colour moduleNormalize() { return token(Tok::ModuleNormalize); }
inline juce::Colour modulePrep()      { return token(Tok::ModulePrep); }
inline juce::Colour moduleLive()      { return token(Tok::ModuleLive); }
inline juce::Colour moduleStreaming() { return token(Tok::ModuleStreaming); }
inline juce::Colour moduleMixlab()    { return token(Tok::ModuleMixlab); }
inline juce::Colour moduleSettings()  { return token(Tok::ModuleSettings); }

} // namespace Colors

namespace Spacing {
    constexpr int xs = 4;
    constexpr int sm = 8;
    constexpr int md = 12;
    constexpr int lg = 16;
    constexpr int xl = 24;
    constexpr int xxl = 32;
}

namespace Radius {
    constexpr float sm = 4.0f;
    constexpr float md = 6.0f;
    constexpr float lg = 10.0f;
    constexpr float pill = 999.0f;
}

namespace Elevation {
    inline juce::DropShadow low()  { return juce::DropShadow(juce::Colour(0x50000000), 6,  { 0, 2 }); }
    inline juce::DropShadow mid()  { return juce::DropShadow(juce::Colour(0x66000000), 12, { 0, 4 }); }
    inline juce::DropShadow high() { return juce::DropShadow(juce::Colour(0x80000000), 24, { 0, 8 }); }
}

namespace Type {
    inline juce::Font display()        { return Fonts::uiFont(24.0f, Fonts::Weight::Bold); }
    inline juce::Font heading()        { return Fonts::uiFont(16.0f, Fonts::Weight::SemiBold); }
    inline juce::Font subheading()     { return Fonts::uiFont(13.0f, Fonts::Weight::SemiBold); }
    inline juce::Font body()           { return Fonts::uiFont(13.0f); }
    inline juce::Font label()          { return Fonts::uiFont(11.0f, Fonts::Weight::Medium); }
    inline juce::Font caption()        { return Fonts::uiFont(9.0f); }
    inline juce::Font monospaceSmall() { return Fonts::monoFont(10.0f); }
    inline juce::Font mono(float size = 12.0f)    { return Fonts::monoFont(size); }
    inline juce::Font numeric(float size = 12.0f) { return Fonts::monoFont(size, Fonts::Weight::Medium); }
}

namespace TrackColors {
    inline juce::Colour palette(int i) {
        static const juce::Colour kPalette[] = {
            juce::Colour(0xFF3B82F6), juce::Colour(0xFF8B5CF6),
            juce::Colour(0xFFEC4899), juce::Colour(0xFFEF4444),
            juce::Colour(0xFFF59E0B), juce::Colour(0xFFFCD34D),
            juce::Colour(0xFF22C55E), juce::Colour(0xFF06B6D4)
        };
        return kPalette[((i % 8) + 8) % 8];
    }
}

namespace ProDraw {

    inline void viewBackground(juce::Graphics& g, int w, int h)
    {
        g.setColour(Colors::bg());
        g.fillRect(0, 0, w, h);
        g.setColour(Colors::primary().withAlpha(0.025f));
        g.fillEllipse(-60.0f, -60.0f, 220.0f, 220.0f);
    }

    inline void glassPanel(juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 10.0f)
    {
        g.setColour(juce::Colour(0x0CFFFFFF));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(juce::Colour(0x0AFFFFFF));
        g.fillRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), 1.5f, radius);
        g.setColour(Colors::glassBorder());
        g.drawRoundedRectangle(bounds, radius, 0.8f);
        g.setColour(juce::Colour(0x08000000));
        g.fillRect(bounds.getX() + 4.0f, bounds.getBottom() - 1.0f, bounds.getWidth() - 8.0f, 1.0f);
    }

    inline void shadowCard(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        Elevation::mid().drawForRectangle(g, bounds);
    }

    inline void shadowPopover(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        Elevation::high().drawForRectangle(g, bounds);
    }

    inline void sectionHeader(juce::Graphics& g, const juce::String& text, int x, int y,
                               juce::Colour accentColor = Colors::primary())
    {
        g.setColour(accentColor.withAlpha(0.7f));
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 2), 3.0f, 14.0f, 1.5f);
        g.setFont(Fonts::uiFont(11.0f, Fonts::Weight::SemiBold));
        g.setColour(Colors::textSecondary());
        g.drawText(text, x + 10, y, 300, 18, juce::Justification::centredLeft);
    }

    inline void separator(juce::Graphics& g, float x, float y, float width)
    {
        juce::ColourGradient sepGrad(
            juce::Colour(0x00FFFFFF), x, y,
            Colors::border().withAlpha(0.5f), x + width * 0.5f, y, false);
        g.setGradientFill(sepGrad);
        g.fillRect(x, y, width * 0.5f, 1.0f);

        juce::ColourGradient sepGrad2(
            Colors::border().withAlpha(0.5f), x + width * 0.5f, y,
            juce::Colour(0x00FFFFFF), x + width, y, false);
        g.setGradientFill(sepGrad2);
        g.fillRect(x + width * 0.5f, y, width * 0.5f, 1.0f);
    }

    inline void badge(juce::Graphics& g, const juce::String& text, float x, float y,
                       float w, float h, juce::Colour color)
    {
        g.setColour(color.withAlpha(0.12f));
        g.fillRoundedRectangle(x, y, w, h, h * 0.5f);
        g.setColour(color);
        g.setFont(Fonts::uiFont(h * 0.6f, Fonts::Weight::SemiBold));
        g.drawText(text, static_cast<int>(x), static_cast<int>(y),
                   static_cast<int>(w), static_cast<int>(h), juce::Justification::centred);
    }

    inline void scoreCircle(juce::Graphics& g, float cx, float cy, float radius,
                             float score, const juce::String& label = "")
    {
        float startAngle = juce::MathConstants<float>::pi * 0.75f;
        float endAngle = juce::MathConstants<float>::pi * 2.25f;
        float angle = startAngle + score * (endAngle - startAngle);

        juce::Path bgArc;
        bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(Colors::border());
        g.strokePath(bgArc, juce::PathStrokeType(4.0f));

        if (score > 0.0f)
        {
            juce::Path valArc;
            valArc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, angle, true);
            juce::ColourGradient arcGrad(
                Colors::error(), cx - radius, cy,
                Colors::success(), cx + radius, cy, false);
            arcGrad.addColour(0.5, Colors::warning());
            g.setGradientFill(arcGrad);
            g.strokePath(valArc, juce::PathStrokeType(4.0f));
        }

        g.setColour(Colors::textPrimary());
        g.setFont(Fonts::uiFont(radius * 0.55f, Fonts::Weight::Bold));
        g.drawText(juce::String(static_cast<int>(score * 100)) + "%",
                   static_cast<int>(cx - radius), static_cast<int>(cy - radius * 0.3f),
                   static_cast<int>(radius * 2), static_cast<int>(radius * 0.6f),
                   juce::Justification::centred);

        if (label.isNotEmpty())
        {
            g.setFont(Fonts::uiFont(radius * 0.25f));
            g.setColour(Colors::textMuted());
            g.drawText(label,
                       static_cast<int>(cx - radius), static_cast<int>(cy + radius * 0.3f),
                       static_cast<int>(radius * 2), static_cast<int>(radius * 0.3f),
                       juce::Justification::centred);
        }
    }

    inline void statusPill(juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<float> bounds, juce::Colour color,
                           bool pulse = false)
    {
        const float r = bounds.getHeight() * 0.5f;
        g.setColour(color.withAlpha(0.14f));
        g.fillRoundedRectangle(bounds, r);
        g.setColour(color.withAlpha(0.35f));
        g.drawRoundedRectangle(bounds, r, 0.8f);

        const float dotR = bounds.getHeight() * 0.22f;
        const float dotCx = bounds.getX() + r;
        const float dotCy = bounds.getCentreY();
        if (pulse)
        {
            g.setColour(color.withAlpha(0.30f));
            g.fillEllipse(dotCx - dotR * 1.9f, dotCy - dotR * 1.9f, dotR * 3.8f, dotR * 3.8f);
        }
        g.setColour(color);
        g.fillEllipse(dotCx - dotR, dotCy - dotR, dotR * 2.0f, dotR * 2.0f);

        const int textX = static_cast<int>(bounds.getX() + r * 2.0f + 2.0f);
        g.setColour(color);
        g.setFont(Fonts::uiFont(bounds.getHeight() * 0.52f, Fonts::Weight::SemiBold));
        g.drawText(text, textX, static_cast<int>(bounds.getY()),
                   static_cast<int>(bounds.getRight()) - textX - 8,
                   static_cast<int>(bounds.getHeight()), juce::Justification::centredLeft, true);
    }

    inline void emptyState(juce::Graphics& g, juce::Rectangle<int> area,
                           const juce::String& glyph, const juce::String& title,
                           const juce::String& detail,
                           juce::Colour accentColor = Colors::primary())
    {
        auto centre = area.getCentre();
        const float ring = 30.0f;
        juce::Rectangle<float> badgeArea(static_cast<float>(centre.x) - ring,
                                         static_cast<float>(centre.y) - ring - 34.0f,
                                         ring * 2.0f, ring * 2.0f);
        g.setColour(accentColor.withAlpha(0.10f));
        g.fillEllipse(badgeArea.expanded(5.0f));
        g.setColour(accentColor.withAlpha(0.18f));
        g.fillEllipse(badgeArea);
        g.setColour(accentColor.withAlpha(0.55f));
        g.drawEllipse(badgeArea, 1.2f);
        g.setColour(accentColor);
        g.setFont(Fonts::uiFont(26.0f));
        g.drawText(glyph, badgeArea.toNearestInt(), juce::Justification::centred);

        juce::Rectangle<int> txt(area.getX(), centre.y + 6, area.getWidth(), 60);
        g.setColour(Colors::textPrimary());
        g.setFont(Fonts::uiFont(14.0f, Fonts::Weight::SemiBold));
        g.drawText(title, txt.removeFromTop(24), juce::Justification::centredTop);
        if (detail.isNotEmpty())
        {
            g.setColour(Colors::textSecondary());
            g.setFont(Fonts::uiFont(11.0f));
            g.drawFittedText(detail, txt, juce::Justification::centredTop, 3);
        }
    }

    inline void focusRing(juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 6.0f)
    {
        g.setColour(Colors::borderFocus().withAlpha(0.85f));
        g.drawRoundedRectangle(bounds.reduced(0.75f), radius, 1.6f);
        g.setColour(Colors::borderFocus().withAlpha(0.18f));
        g.drawRoundedRectangle(bounds.expanded(1.5f), radius + 1.5f, 1.5f);
    }

    inline void vignette(juce::Graphics& g, float w, float h, float size = 14.0f)
    {
        juce::ColourGradient topVig(juce::Colour(0x18000000), 0.0f, 0.0f,
            juce::Colour(0x00000000), 0.0f, size, false);
        g.setGradientFill(topVig);
        g.fillRect(0.0f, 0.0f, w, size);

        juce::ColourGradient botVig(juce::Colour(0x00000000), 0.0f, h - size,
            juce::Colour(0x18000000), 0.0f, h, false);
        g.setGradientFill(botVig);
        g.fillRect(0.0f, h - size, w, size);
    }

    inline void keDot(juce::Graphics& g, juce::Rectangle<float> bounds,
                      int camelotNumber, bool isMinor, float energy01,
                      const juce::String& centerText = {})
    {
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const bool hasKey = camelotNumber >= 1 && camelotNumber <= 12;
        const juce::Colour keyCol = hasKey ? Colors::camelot(camelotNumber, isMinor)
                                           : Colors::textMuted();

        g.setColour(keyCol.withAlpha(hasKey ? 0.30f : 0.18f));
        g.drawEllipse(cx - r + 1.0f, cy - r + 1.0f, (r - 1.0f) * 2.0f, (r - 1.0f) * 2.0f, 1.6f);

        const float e = juce::jlimit(0.0f, 1.0f, energy01);
        if (e > 0.0f)
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, r - 1.0f, r - 1.0f, 0.0f,
                              0.0f, juce::MathConstants<float>::twoPi * e, true);
            g.setColour(Colors::energy(e));
            g.strokePath(arc, juce::PathStrokeType(1.8f));
        }

        if (centerText.isNotEmpty() && r >= 9.0f)
        {
            g.setColour(Colors::textPrimary());
            g.setFont(Fonts::monoFont(r * 0.62f, Fonts::Weight::Medium));
            g.drawText(centerText, bounds.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            g.setColour(keyCol);
            g.fillEllipse(cx - r * 0.28f, cy - r * 0.28f, r * 0.56f, r * 0.56f);
        }
    }

    inline void energyThread(juce::Graphics& g, juce::Rectangle<float> bounds,
                             float progress01, juce::Colour base = Colors::primary())
    {
        g.setColour(Colors::border().withAlpha(0.6f));
        g.fillRect(bounds);
        const float p = juce::jlimit(0.0f, 1.0f, progress01);
        const float w = bounds.getWidth() * p;
        if (w <= 0.5f)
            return;
        juce::ColourGradient grad(base, bounds.getX(), bounds.getY(),
                                  Colors::energy(p), bounds.getX() + w, bounds.getY(), false);
        g.setGradientFill(grad);
        g.fillRect(bounds.withWidth(w));
        g.setColour(Colors::textPrimary().withAlpha(0.9f));
        g.fillRect(bounds.getX() + w - 1.5f, bounds.getY() - 1.0f, 3.0f, bounds.getHeight() + 2.0f);
    }

} // namespace ProDraw

} // namespace BeatMate::UI
