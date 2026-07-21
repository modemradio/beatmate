#pragma once
// ViewPrefs - tiny helper for UI selector persistence (combos, toggles, sliders).

#include "../../app/ServiceLocator.h"
#include "../../services/config/SettingsManager.h"
#include <string>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::UI::Prefs {

inline Services::Config::SettingsManager* manager() {
    if (!BeatMate::g_serviceLocator) return nullptr;
    return BeatMate::g_serviceLocator->tryGet<Services::Config::SettingsManager>();
}

template <typename T>
inline T get(const std::string& key, const T& defaultValue) {
    if (auto* m = manager()) return m->get<T>(key, defaultValue);
    return defaultValue;
}

template <typename T>
inline void set(const std::string& key, const T& value) {
    if (auto* m = manager()) { m->set<T>(key, value); m->save(); }
}

inline void setInt   (const std::string& k, int    v) { set<int>(k, v); }
inline void setBool  (const std::string& k, bool   v) { set<bool>(k, v); }
inline void setDouble(const std::string& k, double v) { set<double>(k, v); }
inline void setString(const std::string& k, const std::string& v) { set<std::string>(k, v); }

inline int         getInt   (const std::string& k, int         d = 0)       { return get<int>(k, d); }
inline bool        getBool  (const std::string& k, bool        d = false)   { return get<bool>(k, d); }
inline double      getDouble(const std::string& k, double      d = 0.0)     { return get<double>(k, d); }
inline std::string getString(const std::string& k, const std::string& d = {}) { return get<std::string>(k, d); }

} // namespace BeatMate::UI::Prefs
