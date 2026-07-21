#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

enum class WaveformStyle : int {
    Bars = 0,
    Lines = 1,
    Filled = 2,
    ThreeBand = 3,
    Spectrum = 4,
    Classic = 5
};

NLOHMANN_JSON_SERIALIZE_ENUM(WaveformStyle, {
    { WaveformStyle::Bars, "Bars" },
    { WaveformStyle::Lines, "Lines" },
    { WaveformStyle::Filled, "Filled" },
    { WaveformStyle::ThreeBand, "ThreeBand" },
    { WaveformStyle::Spectrum, "Spectrum" },
    { WaveformStyle::Classic, "Classic" }
})

enum class WaveformColorMode : int {
    Single = 0,
    ThreeBand = 1,
    Rainbow = 2,
    Heat = 3,
    Custom = 4
};

NLOHMANN_JSON_SERIALIZE_ENUM(WaveformColorMode, {
    { WaveformColorMode::Single, "Single" },
    { WaveformColorMode::ThreeBand, "ThreeBand" },
    { WaveformColorMode::Rainbow, "Rainbow" },
    { WaveformColorMode::Heat, "Heat" },
    { WaveformColorMode::Custom, "Custom" }
})

struct InterfaceSettings {
    std::string accentColor = "#6366F1";
    std::string backgroundColor = "#1A1A2E";
    std::string surfaceColor = "#16213E";
    std::string textColor = "#E0E0E0";

    WaveformStyle waveformStyle = WaveformStyle::ThreeBand;
    WaveformColorMode waveformColorMode = WaveformColorMode::ThreeBand;
    std::string waveformColor = "#4FC3F7";
    std::string waveformLowColor = "#F44336";
    std::string waveformMidColor = "#4CAF50";
    std::string waveformHighColor = "#2196F3";
    float waveformOpacity = 0.9f;
    bool showWaveformGrid = true;
    bool showBeatMarkers = true;
    bool showCueMarkers = true;

    bool reducedMotion = false;
    bool highContrast = false;
    bool largeText = false;
    float textScale = 1.0f;         // 0.8 - 2.0
    bool screenReaderSupport = false;

    bool showSidebar = true;
    bool showToolbar = true;
    bool showStatusBar = true;
    bool showMiniPlayer = false;
    int sidebarWidth = 250;
    std::string defaultView = "library";  // "library", "analysis", "performance", "planner"
    bool compactMode = false;

    std::string visibleColumns = "title,artist,album,bpm,key,energy,duration,rating";
    std::string columnOrder = "title,artist,album,bpm,key,energy,duration,rating";

    std::string fontFamily = "Inter";
    int fontSize = 13;

    float animationSpeed = 1.0f;    // multiplier (0 = off, 2 = fast)
    bool smoothScrolling = true;

    bool showTooltips = true;
    int tooltipDelayMs = 500;

    bool showAlbumArt = true;
    int albumArtSize = 48;          // pixels
    bool showBPMDecimal = true;
    bool showKeyColor = true;
    bool showEnergyBar = true;

    int deckCount = 2;              // 2 or 4
    bool showDeckWaveform = true;
    bool showDeckSpectrum = false;
    bool showDeckPhaseMeters = true;

    InterfaceSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InterfaceSettings,
        accentColor, backgroundColor, surfaceColor, textColor,
        waveformStyle, waveformColorMode, waveformColor,
        waveformLowColor, waveformMidColor, waveformHighColor,
        waveformOpacity, showWaveformGrid, showBeatMarkers, showCueMarkers,
        reducedMotion, highContrast, largeText, textScale, screenReaderSupport,
        showSidebar, showToolbar, showStatusBar, showMiniPlayer,
        sidebarWidth, defaultView, compactMode,
        visibleColumns, columnOrder,
        fontFamily, fontSize,
        animationSpeed, smoothScrolling,
        showTooltips, tooltipDelayMs,
        showAlbumArt, albumArtSize, showBPMDecimal, showKeyColor, showEnergyBar,
        deckCount, showDeckWaveform, showDeckSpectrum, showDeckPhaseMeters
    )
};

} // namespace BeatMate::Models::Settings
