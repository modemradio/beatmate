#pragma once

#include "LocalizationService.h"
#include "../../app/ServiceLocator.h"

#include <juce_core/juce_core.h>

namespace BeatMate { extern ServiceLocator* g_serviceLocator; }

namespace BeatMate::Services::Config {

inline std::string tr(const std::string& key) {
    auto* sl = ::BeatMate::g_serviceLocator;
    if (sl) {
        if (auto* svc = sl->tryGet<LocalizationService>()) {
            return svc->tr(key);
        }
    }
    return key;
}

inline juce::String trJ(const std::string& key) {
    return juce::String::fromUTF8(tr(key).c_str());
}

} // namespace BeatMate::Services::Config

#define BM_T(key)  ::BeatMate::Services::Config::tr(key)
#define BM_TJ(key) ::BeatMate::Services::Config::trJ(key)
