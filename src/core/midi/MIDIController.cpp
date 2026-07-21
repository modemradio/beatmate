#include "MIDIController.h"

namespace BeatMate::Core {

std::string MIDIController::processCC(int cc, int /*value*/) {
    auto it = profile_.ccMappings.find(cc);
    if (it != profile_.ccMappings.end()) return it->second;
    return "";
}

std::string MIDIController::processNote(int note, int /*velocity*/) {
    auto it = profile_.noteMappings.find(note);
    if (it != profile_.noteMappings.end()) return it->second;
    return "";
}

} // namespace BeatMate::Core
