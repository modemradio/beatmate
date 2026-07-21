#include "ThemeEngine.h"
#include "ColorPalette.h"
#include "../../app/ServiceLocator.h"
#include "../../services/persistence/SettingsStore.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>

namespace BeatMate {
// Forward-declared to avoid pulling Application.h into UI.
extern ServiceLocator* g_serviceLocator;
} // namespace BeatMate

namespace BeatMate::UI {

namespace {
const std::map<juce::String, juce::Colour>& darkDefaults();

// Parse a theme JSON blob into the color map. Shared by DB and file paths.
static bool parseThemeJsonInto(const std::string& jsonStr,
                               std::map<juce::String, juce::Colour>& out)
{
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.contains("colors") || !j["colors"].is_object()) return false;
        std::map<juce::String, juce::Colour> loaded;
        for (auto& [k, v] : j["colors"].items()) {
            if (!v.is_string()) continue;
            juce::String hex = juce::String(v.get<std::string>()).trim();
            if (hex.startsWith("#")) hex = hex.substring(1);
            if (hex.startsWith("0x") || hex.startsWith("0X")) hex = hex.substring(2);
            const juce::int64 bits = hex.getHexValue64();
            if (hex.length() <= 6) loaded[juce::String(k)] = juce::Colour(0xFF000000u | (juce::uint32)bits);
            else                   loaded[juce::String(k)] = juce::Colour((juce::uint32)bits);
        }
        if (loaded.empty()) return false;
        out = std::move(loaded);
        return true;
    } catch (...) { return false; }
}
} // namespace

ThemeEngine& ThemeEngine::instance()
{
    static ThemeEngine s;
    return s;
}

ThemeEngine::ThemeEngine()
{
    initDarkTheme();
}

void ThemeEngine::reloadPersisted()
{
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            auto blob = store->getKV("theme.custom.json");
            if (blob.has_value()) {
                std::map<juce::String, juce::Colour> loaded;
                if (parseThemeJsonInto(*blob, loaded)) {
                    m_colors = std::move(loaded);
                    m_currentTheme = Custom;
                    pushTokens();
                    m_listeners.call([](Listener& l) { l.themeChanged(Custom); });
                    spdlog::info("[Theme] Loaded custom theme from DB (SettingsStore)");
                    return;
                }
            }
        }
    }
    auto customFile = defaultCustomThemeFile();
    if (customFile.existsAsFile() && m_currentTheme != Custom)
        loadCustomTheme(customFile.getFullPathName());
}

void ThemeEngine::pushTokens()
{
    using Colors::Tok;
    auto get = [this](const char* k, juce::Colour fb) {
        auto it = m_colors.find(juce::String(k));
        return it != m_colors.end() ? it->second : fb;
    };
    auto push = [&](Tok tok, const char* k, juce::Colour fb) {
        Colors::setToken(tok, get(k, fb));
    };

    const juce::Colour bg      = get("bg.darkest", juce::Colour(0xFF0A0E14));
    const juce::Colour primary = get("primary",    juce::Colour(0xFF3B82F6));
    const juce::Colour accent  = get("accent",     juce::Colour(0xFF06B6D4));
    const juce::Colour border  = get("border",     juce::Colour(0xFF1E293B));
    const juce::Colour warning = get("warning",    juce::Colour(0xFFF59E0B));
    const juce::Colour error   = get("error",      juce::Colour(0xFFEF4444));
    const juce::Colour bgLighter = get("bg.lighter", bg.brighter(0.25f));

    Colors::setToken(Tok::Bg, bg);
    push(Tok::BgSidebar,  "bg.sidebar",  bg.brighter(0.03f));
    push(Tok::BgSurface,  "bg.dark",     bg.brighter(0.05f));
    Colors::setToken(Tok::BgCard, get("bg.card", get("bg.medium", bg.brighter(0.08f))));
    push(Tok::BgElevated, "bg.light",    bg.brighter(0.12f));
    Colors::setToken(Tok::BgLighter, bgLighter);
    push(Tok::BgLightest, "bg.lightest", bgLighter.brighter(0.20f));

    Colors::setToken(Tok::Primary, primary);
    push(Tok::PrimaryHover, "primary.hover", primary.brighter(0.25f));
    push(Tok::PrimaryDark,  "primary.dark",  primary.darker(0.25f));
    push(Tok::Secondary,    "secondary",     accent);
    Colors::setToken(Tok::Accent, accent);
    push(Tok::BorderFocus,  "border.focus",  primary);

    push(Tok::Success, "success", juce::Colour(0xFF10B981));
    Colors::setToken(Tok::Warning, warning);
    Colors::setToken(Tok::Error, error);

    push(Tok::TextPrimary,   "text.primary",   juce::Colour(0xFFF1F5F9));
    push(Tok::TextSecondary, "text.secondary", juce::Colour(0xFF64748B));
    push(Tok::TextMuted,     "text.muted",     juce::Colour(0xFF475569));
    push(Tok::TextDim,       "text.dim",       juce::Colour(0xFF334155));
    push(Tok::TextDisabled,  "text.disabled",  border);

    push(Tok::WaveBass,   "waveform.low",  juce::Colour(0xFFEF4444));
    push(Tok::WaveMid,    "waveform.mid",  juce::Colour(0xFF22C55E));
    push(Tok::WaveTreble, "waveform.high", juce::Colour(0xFF3B82F6));

    push(Tok::StemVocals, "stems.vocals", juce::Colour(0xFFF472B6));
    push(Tok::StemDrums,  "stems.drums",  juce::Colour(0xFF10B981));
    push(Tok::StemBass,   "stems.bass",   juce::Colour(0xFFF59E0B));
    push(Tok::StemOther,  "stems.other",  juce::Colour(0xFF8B5CF6));

    static const char* cueKeys[8] = { "cue.1", "cue.2", "cue.3", "cue.4",
                                      "cue.5", "cue.6", "cue.7", "cue.8" };
    static const juce::uint32 cueDefaults[8] = {
        0xFFEF4444, 0xFFF97316, 0xFFF59E0B, 0xFF22C55E,
        0xFF06B6D4, 0xFF3B82F6, 0xFF8B5CF6, 0xFFEC4899
    };
    for (int i = 0; i < 8; ++i)
        Colors::setToken(static_cast<Tok>(static_cast<int>(Tok::Cue1) + i),
                         get(cueKeys[i], juce::Colour(cueDefaults[i])));

    push(Tok::EnergyMedium, "energy.medium", warning);
    push(Tok::EnergyHigh,   "energy.high",   error);

    push(Tok::VuGreen,  "vu.green",  juce::Colour(0xFF22C55E));
    push(Tok::VuYellow, "vu.yellow", juce::Colour(0xFFFCD34D));
    push(Tok::VuRed,    "vu.red",    juce::Colour(0xFFEF4444));
    push(Tok::GainReduction, "vu.gain", juce::Colour(0xFFFB923C));

    Colors::setToken(Tok::Border, border);
    push(Tok::BorderLight, "border.light", border.brighter(0.35f));

    push(Tok::StarFilled, "star.filled", warning);
    push(Tok::StarEmpty,  "star.empty",  border);

    push(Tok::ModuleHome,      "module.home",      primary);
    push(Tok::ModuleLibrary,   "module.library",   juce::Colour(0xFF22D3EE));
    push(Tok::ModuleAnalysis,  "module.analysis",  juce::Colour(0xFF8B5CF6));
    push(Tok::ModuleHotcues,   "module.hotcues",   juce::Colour(0xFFEC4899));
    push(Tok::ModuleImport,    "module.import",    juce::Colour(0xFF10B981));
    push(Tok::ModuleExport,    "module.export",    juce::Colour(0xFFF97316));
    push(Tok::ModuleNormalize, "module.normalize", juce::Colour(0xFF14B8A6));
    push(Tok::ModulePrep,      "module.prep",      juce::Colour(0xFFEAB308));
    push(Tok::ModuleLive,      "module.live",      juce::Colour(0xFFEF4444));
    push(Tok::ModuleStreaming, "module.streaming", juce::Colour(0xFF60A5FA));
    push(Tok::ModuleMixlab,    "module.mixlab",    juce::Colour(0xFF6366F1));
    push(Tok::ModuleSettings,  "module.settings",  juce::Colour(0xFF94A3B8));
}

void ThemeEngine::setTheme(Theme theme)
{
    m_currentTheme = theme;
    switch (theme) {
        case Dark:         initDarkTheme();         break;
        case Light:        initLightTheme();        break;
        case Nord:         initNordTheme();         break;
        case Dracula:      initDraculaTheme();      break;
        case HighContrast: initHighContrastTheme(); break;
        case Custom:
            // Custom = reload from persisted JSON if available.
            loadCustomTheme(defaultCustomThemeFile().getFullPathName());
            break;
    }
    m_listeners.call([theme](Listener& l) { l.themeChanged(theme); });
}

juce::File ThemeEngine::defaultCustomThemeFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("BeatMate").getChildFile("theme.json");
}

bool ThemeEngine::loadCustomTheme(const juce::String& path)
{
    juce::File file(path);
    if (!file.existsAsFile()) return false;
    try {
        std::ifstream in(file.getFullPathName().toStdString());
        nlohmann::json j;
        in >> j;
        if (!j.contains("colors") || !j["colors"].is_object()) return false;
        std::map<juce::String, juce::Colour> loaded;
        for (auto& [k, v] : j["colors"].items()) {
            if (!v.is_string()) continue;
            // Accept "#AARRGGBB", "#RRGGBB", or "0xAARRGGBB".
            juce::String hex = juce::String(v.get<std::string>()).trim();
            if (hex.startsWith("#")) hex = hex.substring(1);
            if (hex.startsWith("0x") || hex.startsWith("0X")) hex = hex.substring(2);
            const juce::int64 bits = hex.getHexValue64();
            if (hex.length() <= 6)      loaded[juce::String(k)] = juce::Colour(0xFF000000u | (juce::uint32)bits);
            else                        loaded[juce::String(k)] = juce::Colour((juce::uint32)bits);
        }
        if (loaded.empty()) return false;
        m_colors = std::move(loaded);
        m_currentTheme = Custom;
        pushTokens();
        m_listeners.call([](Listener& l) { l.themeChanged(Custom); });
        spdlog::info("[Theme] Custom theme loaded from {}", file.getFullPathName().toStdString());
        return true;
    } catch (const std::exception& e) {
        spdlog::warn("[Theme] Failed to parse custom theme {}: {}",
                      file.getFullPathName().toStdString(), e.what());
        return false;
    }
}

bool ThemeEngine::saveCustomTheme(const juce::String& path) const
{
    nlohmann::json j;
    j["colors"] = nlohmann::json::object();
    for (const auto& [k, c] : m_colors) {
        j["colors"][k.toStdString()] = ("#" + c.toDisplayString(true)).toStdString();
    }
    const std::string dumped = j.dump(2);

    // Primary: write to SettingsStore DB so all state lives in beatmate.db.
    bool dbOk = false;
    if (BeatMate::g_serviceLocator) {
        if (auto* store = BeatMate::g_serviceLocator->tryGet<Services::Persistence::SettingsStore>()) {
            dbOk = store->setKV("theme.custom.json", dumped);
            if (!dbOk) spdlog::warn("[Theme] SettingsStore write failed — falling back to JSON");
        }
    }

    // Secondary: keep the JSON file up-to-date as a backup/export. If the DB
    juce::File file(path);
    file.getParentDirectory().createDirectory();
    bool fileOk = false;
    try {
        std::ofstream out(file.getFullPathName().toStdString());
        out << dumped;
        fileOk = true;
    } catch (const std::exception& e) {
        spdlog::warn("[Theme] Cannot write theme file: {}", e.what());
    }
    return dbOk || fileOk;
}

void ThemeEngine::applyTheme(juce::LookAndFeel& lf)
{
    lf.setColour(juce::ResizableWindow::backgroundColourId, getColor("bg.darker"));
    lf.setColour(juce::TextButton::buttonColourId, getColor("bg.lighter"));
    lf.setColour(juce::TextButton::textColourOffId, getColor("text.secondary"));
    lf.setColour(juce::TextButton::textColourOnId, getColor("text.primary"));
    lf.setColour(juce::TextEditor::backgroundColourId, getColor("bg.light"));
    lf.setColour(juce::TextEditor::textColourId, getColor("text.primary"));
    lf.setColour(juce::TextEditor::outlineColourId, getColor("border"));
    lf.setColour(juce::TextEditor::focusedOutlineColourId, getColor("primary"));
    lf.setColour(juce::Label::textColourId, getColor("text.secondary"));
    lf.setColour(juce::ComboBox::backgroundColourId, getColor("bg.light"));
    lf.setColour(juce::ComboBox::textColourId, getColor("text.primary"));
    lf.setColour(juce::ComboBox::outlineColourId, getColor("border"));
    lf.setColour(juce::ListBox::backgroundColourId, getColor("bg.dark"));
    lf.setColour(juce::ListBox::textColourId, getColor("text.secondary"));
    lf.setColour(juce::ScrollBar::thumbColourId, getColor("bg.lighter"));
    lf.setColour(juce::Slider::thumbColourId, getColor("primary"));
    lf.setColour(juce::Slider::trackColourId, getColor("bg.lighter"));
    lf.setColour(juce::PopupMenu::backgroundColourId, getColor("bg.medium"));
    lf.setColour(juce::PopupMenu::textColourId, getColor("text.secondary"));
    lf.setColour(juce::PopupMenu::highlightedBackgroundColourId, getColor("primary"));
    lf.setColour(juce::PopupMenu::highlightedTextColourId, getColor("text.primary"));
}

juce::Colour ThemeEngine::getColor(const juce::String& key) const
{
    auto it = m_colors.find(key);
    if (it != m_colors.end())
        return it->second;
    const auto& defaults = darkDefaults();
    auto d = defaults.find(key);
    if (d != defaults.end())
        return d->second;
    return juce::Colour(0xFF161B22);
}

void ThemeEngine::setColor(const juce::String& key, const juce::Colour& value)
{
    m_colors[key] = value;
    pushTokens();
}

juce::StringArray ThemeEngine::availableThemes() const
{
    return { "Sombre", "Clair", "Nord", "Dracula", "Haut contraste",
             juce::String::fromUTF8("Personnalisé") };
}

void ThemeEngine::addListener(Listener* listener)    { m_listeners.add(listener); }
void ThemeEngine::removeListener(Listener* listener) { m_listeners.remove(listener); }

// Built-in themes. The color key namespace is shared across all themes so that

namespace {
const std::map<juce::String, juce::Colour>& darkDefaults()
{
    static const std::map<juce::String, juce::Colour> m = {
        {"bg.darkest",      juce::Colour(0xFF0A0E14)},
        {"bg.darker",       juce::Colour(0xFF0C1117)},
        {"bg.sidebar",      juce::Colour(0xFF0C1117)},
        {"bg.dark",         juce::Colour(0xFF0F1419)},
        {"bg.medium",       juce::Colour(0xFF161B22)},
        {"bg.card",         juce::Colour(0xFF161B22)},
        {"bg.light",        juce::Colour(0xFF1C222B)},
        {"bg.lighter",      juce::Colour(0xFF2E2E44)},
        {"bg.lightest",     juce::Colour(0xFF3A3A52)},
        {"text.primary",    juce::Colour(0xFFF1F5F9)},
        {"text.secondary",  juce::Colour(0xFF64748B)},
        {"text.muted",      juce::Colour(0xFF475569)},
        {"text.dim",        juce::Colour(0xFF334155)},
        {"text.disabled",   juce::Colour(0xFF1E293B)},
        {"primary",         juce::Colour(0xFF3B82F6)},
        {"primary.hover",   juce::Colour(0xFF60A5FA)},
        {"primary.dark",    juce::Colour(0xFF2563EB)},
        {"secondary",       juce::Colour(0xFF8B5CF6)},
        {"accent",          juce::Colour(0xFF06B6D4)},
        {"success",         juce::Colour(0xFF10B981)},
        {"warning",         juce::Colour(0xFFF59E0B)},
        {"error",           juce::Colour(0xFFEF4444)},
        {"border",          juce::Colour(0xFF1E293B)},
        {"border.light",    juce::Colour(0xFF334155)},
        {"stems.vocals",    juce::Colour(0xFFF472B6)},
        {"stems.drums",     juce::Colour(0xFF10B981)},
        {"stems.bass",      juce::Colour(0xFFF59E0B)},
        {"stems.other",     juce::Colour(0xFF8B5CF6)},
        {"cue.1",           juce::Colour(0xFFEF4444)},
        {"cue.2",           juce::Colour(0xFFF97316)},
        {"cue.3",           juce::Colour(0xFFF59E0B)},
        {"cue.4",           juce::Colour(0xFF22C55E)},
        {"cue.5",           juce::Colour(0xFF06B6D4)},
        {"cue.6",           juce::Colour(0xFF3B82F6)},
        {"cue.7",           juce::Colour(0xFF8B5CF6)},
        {"cue.8",           juce::Colour(0xFFEC4899)},
        {"waveform.low",    juce::Colour(0xFFEF4444)},
        {"waveform.mid",    juce::Colour(0xFF22C55E)},
        {"waveform.high",   juce::Colour(0xFF3B82F6)},
        {"vu.green",        juce::Colour(0xFF22C55E)},
        {"vu.yellow",       juce::Colour(0xFFFCD34D)},
        {"vu.red",          juce::Colour(0xFFEF4444)},
        {"module.home",      juce::Colour(0xFF3B82F6)},
        {"module.library",   juce::Colour(0xFF22D3EE)},
        {"module.analysis",  juce::Colour(0xFF8B5CF6)},
        {"module.hotcues",   juce::Colour(0xFFEC4899)},
        {"module.import",    juce::Colour(0xFF10B981)},
        {"module.export",    juce::Colour(0xFFF97316)},
        {"module.normalize", juce::Colour(0xFF14B8A6)},
        {"module.prep",      juce::Colour(0xFFEAB308)},
        {"module.live",      juce::Colour(0xFFEF4444)},
        {"module.streaming", juce::Colour(0xFF60A5FA)},
        {"module.mixlab",    juce::Colour(0xFF6366F1)},
        {"module.settings",  juce::Colour(0xFF94A3B8)},
    };
    return m;
}
} // namespace

void ThemeEngine::initDarkTheme()
{
    m_colors = darkDefaults();
    pushTokens();
}

void ThemeEngine::initLightTheme()
{
    m_colors = {
        {"bg.darkest",      juce::Colour(0xFFFFFFFF)},
        {"bg.darker",       juce::Colour(0xFFF7F7F8)},
        {"bg.dark",         juce::Colour(0xFFEFEFEF)},
        {"bg.medium",       juce::Colour(0xFFE5E5E5)},
        {"bg.light",        juce::Colour(0xFFDDDDDD)},
        {"bg.lighter",      juce::Colour(0xFFCCCCCC)},
        {"bg.card",         juce::Colour(0xFFFCFCFD)},
        {"bg.sidebar",      juce::Colour(0xFFEFEFEF)},
        {"text.primary",    juce::Colour(0xFF111111)},
        {"text.secondary",  juce::Colour(0xFF333333)},
        {"text.muted",      juce::Colour(0xFF666666)},
        {"text.dim",        juce::Colour(0xFF888888)},
        {"primary",         juce::Colour(0xFF0078D4)},
        {"primary.hover",   juce::Colour(0xFF1A8AE8)},
        {"accent",          juce::Colour(0xFF7C3AED)},
        {"success",         juce::Colour(0xFF00A36A)},
        {"warning",         juce::Colour(0xFFE0A000)},
        {"error",           juce::Colour(0xFFE03A3A)},
        {"border",          juce::Colour(0xFFCCCCCC)},
        {"stems.vocals",    juce::Colour(0xFFDC2626)},
        {"stems.drums",     juce::Colour(0xFFF59E0B)},
        {"stems.bass",      juce::Colour(0xFF2563EB)},
        {"stems.other",     juce::Colour(0xFF9333EA)},
        {"cue.1",           juce::Colour(0xFFDC2626)},
        {"cue.2",           juce::Colour(0xFFEA580C)},
        {"cue.3",           juce::Colour(0xFFEAB308)},
        {"cue.4",           juce::Colour(0xFF16A34A)},
        {"cue.5",           juce::Colour(0xFF0891B2)},
        {"cue.6",           juce::Colour(0xFF2563EB)},
        {"cue.7",           juce::Colour(0xFF9333EA)},
        {"cue.8",           juce::Colour(0xFFDB2777)},
        {"waveform.low",    juce::Colour(0xFF2563EB)},
        {"waveform.mid",    juce::Colour(0xFF16A34A)},
        {"waveform.high",   juce::Colour(0xFFDC2626)},
        {"vu.green",        juce::Colour(0xFF22C55E)},
        {"vu.yellow",       juce::Colour(0xFFF59E0B)},
        {"vu.red",          juce::Colour(0xFFDC2626)},
        {"secondary",       juce::Colour(0xFF7C3AED)},
    };
    pushTokens();
}

// Arctic Nord palette (https://www.nordtheme.com). Cool blues + muted greys.
void ThemeEngine::initNordTheme()
{
    m_colors = {
        {"bg.darkest",      juce::Colour(0xFF242933)},
        {"bg.darker",       juce::Colour(0xFF2E3440)},
        {"bg.dark",         juce::Colour(0xFF3B4252)},
        {"bg.medium",       juce::Colour(0xFF434C5E)},
        {"bg.light",        juce::Colour(0xFF4C566A)},
        {"bg.lighter",      juce::Colour(0xFF5E6779)},
        {"bg.card",         juce::Colour(0xFF3B4252)},
        {"bg.sidebar",      juce::Colour(0xFF2E3440)},
        {"text.primary",    juce::Colour(0xFFECEFF4)},
        {"text.secondary",  juce::Colour(0xFFE5E9F0)},
        {"text.muted",      juce::Colour(0xFFD8DEE9)},
        {"text.dim",        juce::Colour(0xFF8FA1B3)},
        {"primary",         juce::Colour(0xFF88C0D0)},
        {"primary.hover",   juce::Colour(0xFF8FBCBB)},
        {"accent",          juce::Colour(0xFFB48EAD)},
        {"success",         juce::Colour(0xFFA3BE8C)},
        {"warning",         juce::Colour(0xFFEBCB8B)},
        {"error",           juce::Colour(0xFFBF616A)},
        {"border",          juce::Colour(0xFF434C5E)},
        {"stems.vocals",    juce::Colour(0xFFBF616A)},
        {"stems.drums",     juce::Colour(0xFFEBCB8B)},
        {"stems.bass",      juce::Colour(0xFF5E81AC)},
        {"stems.other",     juce::Colour(0xFFB48EAD)},
        {"cue.1",           juce::Colour(0xFFBF616A)},
        {"cue.2",           juce::Colour(0xFFD08770)},
        {"cue.3",           juce::Colour(0xFFEBCB8B)},
        {"cue.4",           juce::Colour(0xFFA3BE8C)},
        {"cue.5",           juce::Colour(0xFF88C0D0)},
        {"cue.6",           juce::Colour(0xFF5E81AC)},
        {"cue.7",           juce::Colour(0xFFB48EAD)},
        {"cue.8",           juce::Colour(0xFF8FBCBB)},
        {"waveform.low",    juce::Colour(0xFF5E81AC)},
        {"waveform.mid",    juce::Colour(0xFFA3BE8C)},
        {"waveform.high",   juce::Colour(0xFFBF616A)},
        {"vu.green",        juce::Colour(0xFFA3BE8C)},
        {"vu.yellow",       juce::Colour(0xFFEBCB8B)},
        {"vu.red",          juce::Colour(0xFFBF616A)},
        {"secondary",       juce::Colour(0xFFB48EAD)},
    };
    pushTokens();
}

// Dracula palette (https://draculatheme.com). High-contrast purple/pink on dark.
void ThemeEngine::initDraculaTheme()
{
    m_colors = {
        {"bg.darkest",      juce::Colour(0xFF1E1F29)},
        {"bg.darker",       juce::Colour(0xFF282A36)},
        {"bg.dark",         juce::Colour(0xFF343746)},
        {"bg.medium",       juce::Colour(0xFF3A3C4E)},
        {"bg.light",        juce::Colour(0xFF44475A)},
        {"bg.lighter",      juce::Colour(0xFF565872)},
        {"bg.card",         juce::Colour(0xFF343746)},
        {"bg.sidebar",      juce::Colour(0xFF282A36)},
        {"text.primary",    juce::Colour(0xFFF8F8F2)},
        {"text.secondary",  juce::Colour(0xFFE2E4E8)},
        {"text.muted",      juce::Colour(0xFFBFC7D5)},
        {"text.dim",        juce::Colour(0xFF6272A4)},
        {"primary",         juce::Colour(0xFFBD93F9)},
        {"primary.hover",   juce::Colour(0xFFCAA6FA)},
        {"accent",          juce::Colour(0xFFFF79C6)},
        {"success",         juce::Colour(0xFF50FA7B)},
        {"warning",         juce::Colour(0xFFF1FA8C)},
        {"error",           juce::Colour(0xFFFF5555)},
        {"border",          juce::Colour(0xFF44475A)},
        {"stems.vocals",    juce::Colour(0xFFFF5555)},
        {"stems.drums",     juce::Colour(0xFFFFB86C)},
        {"stems.bass",      juce::Colour(0xFF8BE9FD)},
        {"stems.other",     juce::Colour(0xFFBD93F9)},
        {"cue.1",           juce::Colour(0xFFFF5555)},
        {"cue.2",           juce::Colour(0xFFFFB86C)},
        {"cue.3",           juce::Colour(0xFFF1FA8C)},
        {"cue.4",           juce::Colour(0xFF50FA7B)},
        {"cue.5",           juce::Colour(0xFF8BE9FD)},
        {"cue.6",           juce::Colour(0xFF6272A4)},
        {"cue.7",           juce::Colour(0xFFBD93F9)},
        {"cue.8",           juce::Colour(0xFFFF79C6)},
        {"waveform.low",    juce::Colour(0xFF6272A4)},
        {"waveform.mid",    juce::Colour(0xFF50FA7B)},
        {"waveform.high",   juce::Colour(0xFFFF5555)},
        {"vu.green",        juce::Colour(0xFF50FA7B)},
        {"vu.yellow",       juce::Colour(0xFFF1FA8C)},
        {"vu.red",          juce::Colour(0xFFFF5555)},
        {"secondary",       juce::Colour(0xFFFF79C6)},
    };
    pushTokens();
}

// High-contrast theme for accessibility (WCAG AAA against bg.darker).
void ThemeEngine::initHighContrastTheme()
{
    m_colors = {
        {"bg.darkest",      juce::Colour(0xFF000000)},
        {"bg.darker",       juce::Colour(0xFF000000)},
        {"bg.dark",         juce::Colour(0xFF0A0A0A)},
        {"bg.medium",       juce::Colour(0xFF111111)},
        {"bg.light",        juce::Colour(0xFF1A1A1A)},
        {"bg.lighter",      juce::Colour(0xFF252525)},
        {"bg.card",         juce::Colour(0xFF000000)},
        {"bg.sidebar",      juce::Colour(0xFF000000)},
        {"text.primary",    juce::Colour(0xFFFFFFFF)},
        {"text.secondary",  juce::Colour(0xFFFFFFFF)},
        {"text.muted",      juce::Colour(0xFFE0E0E0)},
        {"text.dim",        juce::Colour(0xFFB0B0B0)},
        {"primary",         juce::Colour(0xFF00FFFF)},
        {"primary.hover",   juce::Colour(0xFF40FFFF)},
        {"accent",          juce::Colour(0xFFFFFF00)},
        {"success",         juce::Colour(0xFF00FF00)},
        {"warning",         juce::Colour(0xFFFFFF00)},
        {"error",           juce::Colour(0xFFFF0000)},
        {"border",          juce::Colour(0xFFFFFFFF)},
        {"stems.vocals",    juce::Colour(0xFFFF0000)},
        {"stems.drums",     juce::Colour(0xFFFFFF00)},
        {"stems.bass",      juce::Colour(0xFF00FFFF)},
        {"stems.other",     juce::Colour(0xFFFF00FF)},
        {"cue.1",           juce::Colour(0xFFFF0000)},
        {"cue.2",           juce::Colour(0xFFFF8000)},
        {"cue.3",           juce::Colour(0xFFFFFF00)},
        {"cue.4",           juce::Colour(0xFF00FF00)},
        {"cue.5",           juce::Colour(0xFF00FFFF)},
        {"cue.6",           juce::Colour(0xFF0080FF)},
        {"cue.7",           juce::Colour(0xFFFF00FF)},
        {"cue.8",           juce::Colour(0xFFFFFFFF)},
        {"waveform.low",    juce::Colour(0xFF00FFFF)},
        {"waveform.mid",    juce::Colour(0xFF00FF00)},
        {"waveform.high",   juce::Colour(0xFFFF0000)},
        {"vu.green",        juce::Colour(0xFF00FF00)},
        {"vu.yellow",       juce::Colour(0xFFFFFF00)},
        {"vu.red",          juce::Colour(0xFFFF0000)},
        {"secondary",       juce::Colour(0xFFFFFF00)},
    };
    pushTokens();
}

} // namespace BeatMate::UI
