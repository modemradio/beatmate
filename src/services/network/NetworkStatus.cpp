#include "NetworkStatus.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Network {

NetworkStatus::NetworkStatus() {}

bool NetworkStatus::isOnline() {
    juce::URL url("https://www.google.com/generate_204");
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(5000);
    auto stream = url.createInputStream(options);
    bool online = (stream != nullptr);

    if (online != lastStatus_) {
        lastStatus_ = online;
        if (callback_) callback_(online);
    }
    return online;
}

} // namespace BeatMate::Services::Network
