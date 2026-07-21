#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SharedLicense.h"
#include "LicenseDialogShared.h"

namespace BeatMate::Shared {

// Démarre le heartbeat licence et bloque si la licence n'est plus utilisable.
inline bool runStartupGate(juce::Component* parentForCentering, const juce::String& appName)
{
    auto& lic = SharedLicense::instance();
    lic.startHeartbeat();

    if (! lic.canRunApp())
    {
        LicenseDialogShared::show(parentForCentering, appName);
        return false;
    }
    return true;
}

} // namespace BeatMate::Shared
