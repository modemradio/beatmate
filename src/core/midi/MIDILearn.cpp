#include "MIDILearn.h"
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

void MIDILearn::startLearn(const std::string& targetParameter, MIDILearnCallback callback) {
    targetParam_ = targetParameter;
    callback_ = std::move(callback);
    learning_.store(true);
    spdlog::info("MIDILearn: waiting for input for '{}'", targetParameter);
}

void MIDILearn::cancelLearn() {
    learning_.store(false);
    spdlog::info("MIDILearn: cancelled");
}

void MIDILearn::onCC(int channel, int cc, int /*value*/) {
    if (!learning_.load()) return;
    learning_.store(false);
    MIDILearnResult result{channel, cc, 0, true};
    spdlog::info("MIDILearn: mapped CC {} ch {} to '{}'", cc, channel, targetParam_);
    if (callback_) callback_(result);
}

void MIDILearn::onNote(int channel, int note, int /*velocity*/) {
    if (!learning_.load()) return;
    learning_.store(false);
    MIDILearnResult result{channel, 0, note, false};
    spdlog::info("MIDILearn: mapped Note {} ch {} to '{}'", note, channel, targetParam_);
    if (callback_) callback_(result);
}

} // namespace BeatMate::Core
