#include "ControllerProfiles.h"
#include <fstream>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

ControllerProfile ControllerProfiles::getDDJ400() {
    ControllerProfile p;
    p.name = "Pioneer DDJ-400";
    p.manufacturer = "Pioneer DJ";
    p.ccMappings[0x00] = "deck1.volume";   p.ccMappings[0x01] = "deck2.volume";
    p.ccMappings[0x02] = "deck1.eqHigh";   p.ccMappings[0x03] = "deck2.eqHigh";
    p.ccMappings[0x04] = "deck1.eqMid";    p.ccMappings[0x05] = "deck2.eqMid";
    p.ccMappings[0x06] = "deck1.eqLow";    p.ccMappings[0x07] = "deck2.eqLow";
    p.ccMappings[0x08] = "deck1.filter";   p.ccMappings[0x09] = "deck2.filter";
    p.ccMappings[0x0A] = "crossfader";     p.ccMappings[0x0B] = "deck1.tempo";
    p.ccMappings[0x0C] = "deck2.tempo";
    p.ccMappings[0x0D] = "deck1.headphoneGain";
    p.ccMappings[0x0E] = "deck2.headphoneGain";
    p.ccMappings[0x0F] = "master.volume";
    p.noteMappings[0x00] = "deck1.play";   p.noteMappings[0x01] = "deck2.play";
    p.noteMappings[0x02] = "deck1.cue";    p.noteMappings[0x03] = "deck2.cue";
    p.noteMappings[0x04] = "deck1.sync";   p.noteMappings[0x05] = "deck2.sync";
    p.noteMappings[0x06] = "deck1.shiftPlay"; p.noteMappings[0x07] = "deck2.shiftPlay";
    for (int i = 0; i < 8; ++i) {
        p.noteMappings[0x10 + i] = "deck1.hotcue" + std::to_string(i + 1);
        p.noteMappings[0x20 + i] = "deck2.hotcue" + std::to_string(i + 1);
    }
    p.noteMappings[0x30] = "deck1.padMode.hotcue";
    p.noteMappings[0x31] = "deck1.padMode.beatloop";
    p.noteMappings[0x32] = "deck1.padMode.beatjump";
    p.noteMappings[0x33] = "deck1.padMode.sampler";
    p.noteMappings[0x34] = "deck2.padMode.hotcue";
    p.noteMappings[0x35] = "deck2.padMode.beatloop";
    p.noteMappings[0x36] = "deck2.padMode.beatjump";
    p.noteMappings[0x37] = "deck2.padMode.sampler";
    p.noteMappings[0x40] = "deck1.loopIn";  p.noteMappings[0x41] = "deck1.loopOut";
    p.noteMappings[0x42] = "deck2.loopIn";  p.noteMappings[0x43] = "deck2.loopOut";
    p.noteMappings[0x44] = "deck1.reloop";  p.noteMappings[0x45] = "deck2.reloop";
    p.ccMappings[0x40] = "browser.scroll";
    p.noteMappings[0x50] = "browser.load1"; p.noteMappings[0x51] = "browser.load2";
    return p;
}

ControllerProfile ControllerProfiles::getDDJ800() {
    ControllerProfile p = getDDJ400();
    p.name = "Pioneer DDJ-800";
    p.ccMappings[0x10] = "deck1.fx1.knob";  p.ccMappings[0x11] = "deck1.fx2.knob";
    p.ccMappings[0x12] = "deck1.fx3.knob";  p.ccMappings[0x13] = "deck1.fxDry";
    p.ccMappings[0x14] = "deck2.fx1.knob";  p.ccMappings[0x15] = "deck2.fx2.knob";
    p.ccMappings[0x16] = "deck2.fx3.knob";  p.ccMappings[0x17] = "deck2.fxDry";
    p.noteMappings[0x60] = "deck1.fx1.toggle"; p.noteMappings[0x61] = "deck1.fx2.toggle";
    p.noteMappings[0x62] = "deck1.fx3.toggle"; p.noteMappings[0x63] = "deck1.fxTap";
    p.noteMappings[0x64] = "deck2.fx1.toggle"; p.noteMappings[0x65] = "deck2.fx2.toggle";
    p.noteMappings[0x66] = "deck2.fx3.toggle"; p.noteMappings[0x67] = "deck2.fxTap";
    p.ccMappings[0x20] = "deck1.jogWheel";   p.ccMappings[0x21] = "deck2.jogWheel";
    p.ccMappings[0x22] = "deck1.jogTouch";   p.ccMappings[0x23] = "deck2.jogTouch";
    p.ccMappings[0x24] = "beatfx.knob";
    p.noteMappings[0x68] = "beatfx.toggle";  p.noteMappings[0x69] = "beatfx.select";
    p.ccMappings[0x25] = "deck1.trim";       p.ccMappings[0x26] = "deck2.trim";
    p.noteMappings[0x70] = "deck1.headphoneCue"; p.noteMappings[0x71] = "deck2.headphoneCue";
    return p;
}

ControllerProfile ControllerProfiles::getDDJ1000() {
    ControllerProfile p = getDDJ800();
    p.name = "Pioneer DDJ-1000";
    p.ccMappings[0x30] = "deck3.volume";     p.ccMappings[0x31] = "deck4.volume";
    p.ccMappings[0x32] = "deck3.eqHigh";     p.ccMappings[0x33] = "deck4.eqHigh";
    p.ccMappings[0x34] = "deck3.eqMid";      p.ccMappings[0x35] = "deck4.eqMid";
    p.ccMappings[0x36] = "deck3.eqLow";      p.ccMappings[0x37] = "deck4.eqLow";
    p.ccMappings[0x38] = "deck3.filter";     p.ccMappings[0x39] = "deck4.filter";
    p.ccMappings[0x3A] = "deck3.tempo";      p.ccMappings[0x3B] = "deck4.tempo";
    p.ccMappings[0x3C] = "deck3.trim";       p.ccMappings[0x3D] = "deck4.trim";
    p.ccMappings[0x3E] = "deck3.jogWheel";   p.ccMappings[0x3F] = "deck4.jogWheel";
    p.noteMappings[0x80] = "deck3.play";     p.noteMappings[0x81] = "deck4.play";
    p.noteMappings[0x82] = "deck3.cue";      p.noteMappings[0x83] = "deck4.cue";
    p.noteMappings[0x84] = "deck3.sync";     p.noteMappings[0x85] = "deck4.sync";
    for (int i = 0; i < 8; ++i) {
        p.noteMappings[0x88 + i] = "deck3.hotcue" + std::to_string(i + 1);
        p.noteMappings[0x90 + i] = "deck4.hotcue" + std::to_string(i + 1);
    }
    p.noteMappings[0x98] = "deck3.loopIn";   p.noteMappings[0x99] = "deck3.loopOut";
    p.noteMappings[0x9A] = "deck4.loopIn";   p.noteMappings[0x9B] = "deck4.loopOut";
    p.noteMappings[0xA0] = "deckSelect.1";   p.noteMappings[0xA1] = "deckSelect.2";
    p.noteMappings[0xA2] = "deckSelect.3";   p.noteMappings[0xA3] = "deckSelect.4";
    p.noteMappings[0xA4] = "deck1.slip";     p.noteMappings[0xA5] = "deck2.slip";
    p.noteMappings[0xA6] = "deck3.slip";     p.noteMappings[0xA7] = "deck4.slip";
    p.ccMappings[0x50] = "deck1.colorFx";    p.ccMappings[0x51] = "deck2.colorFx";
    p.ccMappings[0x52] = "deck3.colorFx";    p.ccMappings[0x53] = "deck4.colorFx";
    return p;
}

ControllerProfile ControllerProfiles::getKontrolS2() {
    ControllerProfile p;
    p.name = "Traktor Kontrol S2";
    p.manufacturer = "Native Instruments";
    p.ccMappings[0x00] = "deck1.volume";     p.ccMappings[0x01] = "deck2.volume";
    p.ccMappings[0x02] = "crossfader";       p.ccMappings[0x03] = "master.volume";
    p.ccMappings[0x04] = "deck1.eqHigh";     p.ccMappings[0x05] = "deck2.eqHigh";
    p.ccMappings[0x06] = "deck1.eqMid";      p.ccMappings[0x07] = "deck2.eqMid";
    p.ccMappings[0x08] = "deck1.eqLow";      p.ccMappings[0x09] = "deck2.eqLow";
    p.ccMappings[0x0A] = "deck1.filter";     p.ccMappings[0x0B] = "deck2.filter";
    p.ccMappings[0x0C] = "deck1.trim";       p.ccMappings[0x0D] = "deck2.trim";
    p.ccMappings[0x0E] = "deck1.tempo";      p.ccMappings[0x0F] = "deck2.tempo";
    p.ccMappings[0x10] = "deck1.fx1.knob";   p.ccMappings[0x11] = "deck1.fx2.knob";
    p.ccMappings[0x12] = "deck2.fx1.knob";   p.ccMappings[0x13] = "deck2.fx2.knob";
    p.ccMappings[0x14] = "deck1.jogWheel";   p.ccMappings[0x15] = "deck2.jogWheel";
    p.noteMappings[0x00] = "deck1.play";     p.noteMappings[0x01] = "deck2.play";
    p.noteMappings[0x02] = "deck1.cue";      p.noteMappings[0x03] = "deck2.cue";
    p.noteMappings[0x04] = "deck1.sync";     p.noteMappings[0x05] = "deck2.sync";
    for (int i = 0; i < 8; ++i) {
        p.noteMappings[0x10 + i] = "deck1.hotcue" + std::to_string(i + 1);
        p.noteMappings[0x20 + i] = "deck2.hotcue" + std::to_string(i + 1);
    }
    p.noteMappings[0x30] = "deck1.loopIn";   p.noteMappings[0x31] = "deck1.loopOut";
    p.noteMappings[0x32] = "deck2.loopIn";   p.noteMappings[0x33] = "deck2.loopOut";
    p.noteMappings[0x34] = "deck1.loopSize"; p.noteMappings[0x35] = "deck2.loopSize";
    p.noteMappings[0x40] = "deck1.fx1.toggle"; p.noteMappings[0x41] = "deck1.fx2.toggle";
    p.noteMappings[0x42] = "deck2.fx1.toggle"; p.noteMappings[0x43] = "deck2.fx2.toggle";
    p.ccMappings[0x20] = "browser.scroll";
    p.noteMappings[0x50] = "browser.load1";  p.noteMappings[0x51] = "browser.load2";
    p.noteMappings[0x52] = "deck1.headphoneCue"; p.noteMappings[0x53] = "deck2.headphoneCue";
    p.ccMappings[0x21] = "headphone.mix";    p.ccMappings[0x22] = "headphone.volume";
    return p;
}

ControllerProfile ControllerProfiles::getKontrolS4() {
    ControllerProfile p = getKontrolS2();
    p.name = "Traktor Kontrol S4";
    p.ccMappings[0x30] = "deck3.volume";     p.ccMappings[0x31] = "deck4.volume";
    p.ccMappings[0x32] = "deck3.eqHigh";     p.ccMappings[0x33] = "deck4.eqHigh";
    p.ccMappings[0x34] = "deck3.eqMid";      p.ccMappings[0x35] = "deck4.eqMid";
    p.ccMappings[0x36] = "deck3.eqLow";      p.ccMappings[0x37] = "deck4.eqLow";
    p.ccMappings[0x38] = "deck3.filter";     p.ccMappings[0x39] = "deck4.filter";
    p.ccMappings[0x3A] = "deck3.trim";       p.ccMappings[0x3B] = "deck4.trim";
    p.ccMappings[0x3C] = "deck3.tempo";      p.ccMappings[0x3D] = "deck4.tempo";
    p.ccMappings[0x3E] = "deck3.jogWheel";   p.ccMappings[0x3F] = "deck4.jogWheel";
    p.noteMappings[0x60] = "deck3.play";     p.noteMappings[0x61] = "deck4.play";
    p.noteMappings[0x62] = "deck3.cue";      p.noteMappings[0x63] = "deck4.cue";
    p.noteMappings[0x64] = "deck3.sync";     p.noteMappings[0x65] = "deck4.sync";
    for (int i = 0; i < 8; ++i) {
        p.noteMappings[0x68 + i] = "deck3.hotcue" + std::to_string(i + 1);
        p.noteMappings[0x78 + i] = "deck4.hotcue" + std::to_string(i + 1);
    }
    p.ccMappings[0x40] = "deck3.fx1.knob";   p.ccMappings[0x41] = "deck3.fx2.knob";
    p.ccMappings[0x42] = "deck4.fx1.knob";   p.ccMappings[0x43] = "deck4.fx2.knob";
    p.noteMappings[0x80] = "deck3.fx1.toggle"; p.noteMappings[0x81] = "deck3.fx2.toggle";
    p.noteMappings[0x82] = "deck4.fx1.toggle"; p.noteMappings[0x83] = "deck4.fx2.toggle";
    p.ccMappings[0x50] = "deck1.mixerFx";    p.ccMappings[0x51] = "deck2.mixerFx";
    p.ccMappings[0x52] = "deck3.mixerFx";    p.ccMappings[0x53] = "deck4.mixerFx";
    p.noteMappings[0x90] = "deck1.stemSelect"; p.noteMappings[0x91] = "deck2.stemSelect";
    p.noteMappings[0x92] = "deck3.stemSelect"; p.noteMappings[0x93] = "deck4.stemSelect";
    p.ccMappings[0x54] = "deck3.jogTouch";   p.ccMappings[0x55] = "deck4.jogTouch";
    return p;
}

ControllerProfile ControllerProfiles::getMC7000() {
    ControllerProfile p;
    p.name = "Denon MC7000";
    p.manufacturer = "Denon DJ";
    p.ccMappings[0x00] = "deck1.volume";     p.ccMappings[0x01] = "deck2.volume";
    p.ccMappings[0x02] = "deck3.volume";     p.ccMappings[0x03] = "deck4.volume";
    p.ccMappings[0x04] = "crossfader";       p.ccMappings[0x05] = "master.volume";
    p.ccMappings[0x06] = "booth.volume";
    for (int ch = 0; ch < 4; ++ch) {
        std::string deck = "deck" + std::to_string(ch + 1);
        p.ccMappings[0x10 + ch * 4] = deck + ".eqHigh";
        p.ccMappings[0x11 + ch * 4] = deck + ".eqMid";
        p.ccMappings[0x12 + ch * 4] = deck + ".eqLow";
        p.ccMappings[0x13 + ch * 4] = deck + ".filter";
    }
    p.ccMappings[0x30] = "deck1.trim";       p.ccMappings[0x31] = "deck2.trim";
    p.ccMappings[0x32] = "deck3.trim";       p.ccMappings[0x33] = "deck4.trim";
    p.ccMappings[0x34] = "deck1.tempo";      p.ccMappings[0x35] = "deck2.tempo";
    p.ccMappings[0x36] = "deck3.tempo";      p.ccMappings[0x37] = "deck4.tempo";
    p.ccMappings[0x38] = "deck1.jogWheel";   p.ccMappings[0x39] = "deck2.jogWheel";
    p.ccMappings[0x3A] = "deck3.jogWheel";   p.ccMappings[0x3B] = "deck4.jogWheel";
    p.ccMappings[0x40] = "fx1.knob1";        p.ccMappings[0x41] = "fx1.knob2";
    p.ccMappings[0x42] = "fx1.knob3";        p.ccMappings[0x43] = "fx1.dryWet";
    p.ccMappings[0x44] = "fx2.knob1";        p.ccMappings[0x45] = "fx2.knob2";
    p.ccMappings[0x46] = "fx2.knob3";        p.ccMappings[0x47] = "fx2.dryWet";
    for (int ch = 0; ch < 4; ++ch) {
        std::string deck = "deck" + std::to_string(ch + 1);
        p.noteMappings[ch * 4]     = deck + ".play";
        p.noteMappings[ch * 4 + 1] = deck + ".cue";
        p.noteMappings[ch * 4 + 2] = deck + ".sync";
        p.noteMappings[ch * 4 + 3] = deck + ".shiftPlay";
    }
    for (int ch = 0; ch < 4; ++ch) {
        std::string deck = "deck" + std::to_string(ch + 1);
        for (int i = 0; i < 8; ++i) {
            p.noteMappings[0x20 + ch * 8 + i] = deck + ".hotcue" + std::to_string(i + 1);
        }
    }
    for (int ch = 0; ch < 4; ++ch) {
        std::string deck = "deck" + std::to_string(ch + 1);
        p.noteMappings[0x60 + ch * 2]     = deck + ".loopIn";
        p.noteMappings[0x60 + ch * 2 + 1] = deck + ".loopOut";
    }
    p.noteMappings[0x70] = "fx1.toggle1"; p.noteMappings[0x71] = "fx1.toggle2";
    p.noteMappings[0x72] = "fx1.toggle3"; p.noteMappings[0x73] = "fx1.tap";
    p.noteMappings[0x74] = "fx2.toggle1"; p.noteMappings[0x75] = "fx2.toggle2";
    p.noteMappings[0x76] = "fx2.toggle3"; p.noteMappings[0x77] = "fx2.tap";
    p.ccMappings[0x50] = "headphone.mix";    p.ccMappings[0x51] = "headphone.volume";
    for (int ch = 0; ch < 4; ++ch) {
        p.noteMappings[0x78 + ch] = "deck" + std::to_string(ch + 1) + ".headphoneCue";
    }
    p.ccMappings[0x52] = "browser.scroll";
    p.noteMappings[0x7C] = "browser.load1"; p.noteMappings[0x7D] = "browser.load2";
    p.noteMappings[0x7E] = "browser.load3"; p.noteMappings[0x7F] = "browser.load4";
    for (int ch = 0; ch < 4; ++ch) {
        p.noteMappings[0xA0 + ch] = "deck" + std::to_string(ch + 1) + ".slip";
    }
    return p;
}

ControllerProfile ControllerProfiles::getGeneric() {
    ControllerProfile p;
    p.name = "Generic MIDI";
    p.manufacturer = "Generic";
    for (int i = 0; i < 16; ++i) {
        p.ccMappings[i] = "cc" + std::to_string(i);
    }
    for (int i = 0; i < 16; ++i) {
        p.noteMappings[i] = "note" + std::to_string(i);
    }
    return p;
}

std::vector<std::string> ControllerProfiles::getAvailableProfiles() {
    return {"Pioneer DDJ-400", "Pioneer DDJ-800", "Pioneer DDJ-1000",
            "Traktor Kontrol S2", "Traktor Kontrol S4",
            "Denon MC7000", "Generic MIDI"};
}

ControllerProfile ControllerProfiles::getProfile(const std::string& name) {
    if (name == "Pioneer DDJ-400") return getDDJ400();
    if (name == "Pioneer DDJ-800") return getDDJ800();
    if (name == "Pioneer DDJ-1000") return getDDJ1000();
    if (name == "Traktor Kontrol S2") return getKontrolS2();
    if (name == "Traktor Kontrol S4") return getKontrolS4();
    if (name == "Denon MC7000") return getMC7000();
    return getGeneric();
}

bool ControllerProfiles::exportProfile(const ControllerProfile& profile, const std::string& path) {
    std::ofstream file(path);
    if (!file) return false;
    file << "name=" << profile.name << "\n";
    file << "manufacturer=" << profile.manufacturer << "\n";
    for (auto& [cc, action] : profile.ccMappings) file << "cc:" << cc << "=" << action << "\n";
    for (auto& [note, action] : profile.noteMappings) file << "note:" << note << "=" << action << "\n";
    return true;
}

ControllerProfile ControllerProfiles::importProfile(const std::string& path) {
    ControllerProfile p;
    std::ifstream file(path);
    if (!file) return p;
    std::string line;
    while (std::getline(file, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "name") p.name = val;
        else if (key == "manufacturer") p.manufacturer = val;
        else if (key.substr(0, 3) == "cc:") p.ccMappings[std::stoi(key.substr(3))] = val;
        else if (key.substr(0, 5) == "note:") p.noteMappings[std::stoi(key.substr(5))] = val;
    }
    return p;
}

} // namespace BeatMate::Core
