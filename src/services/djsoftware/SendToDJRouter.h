#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "../../models/Track.h"
#include "DJSoftwareManager.h"

namespace BeatMate::Services::DJSoftware {

enum class DJTarget {
    VirtualDJ,
    Rekordbox,
    Serato,
    Traktor,
    EngineDJ,
    Auto
};

enum class DeckSlot {
    DeckA = 0,
    DeckB = 1,
    DeckC = 2,
    DeckD = 3
};

enum class SendMethod {
    Unknown,
    HTTP,         // VirtualDJ HTTP remote
    Clipboard,    // CF_HDROP posted, user needs manual action
    Keyboard,     // synthetic keystroke sequence (Ctrl+O, Ctrl+V, Enter)
    FileInbox,    // XML / playlist file dropped into the app's inbox folder
    Playlist,     // tracks appended to the app's native playlist
    NotSent       // no successful path was taken
};

struct SendResult {
    bool ok = false;
    DJTarget target = DJTarget::Auto;
    std::string message; // user-facing
    SendMethod method = SendMethod::Unknown;
};

// Pushes a track into the running DJ app; Auto order Rekordbox > Serato > Traktor > VirtualDJ > EngineDJ.
class SendToDJRouter {
public:
    SendToDJRouter();
    ~SendToDJRouter();

    std::vector<DJTarget> availableTargets() const;

    // Return true if the target's install/data folder is detected on disk.
    bool isInstalled(DJTarget t) const;
    // Return true if the target's process is currently running.
    bool isRunning(DJTarget t) const;

    // Diagnostic: per-target status string ("OK" / "installed but not running"
    std::map<DJTarget, std::string> pingTargets() const;

    // Send a single track. target=Auto selects the best candidate.
    SendResult sendTrack(const Models::Track& track,
                         DJTarget target = DJTarget::Auto,
                         DeckSlot deck = DeckSlot::DeckA);

    // Auto-everything helper: pick the running DJ app, pick the free deck
    SendResult sendTrackAutoDeck(const Models::Track& track);

    // Envoi d'un lot (ex. un onglet de suggestions complet)
    SendResult sendTracks(const std::vector<Models::Track>& tracks,
                          DJTarget target = DJTarget::Auto);

    static const char* targetLabel(DJTarget t);

    // Ordre de preference du mode Auto quand rien ne tourne
    void setPreferenceOrder(std::vector<DJTarget> order);

private:
    // Auto resolver: running-first, then preferences.
    DJTarget resolveAuto() const;

    // Per-target implementations (null-safe, return populated SendResult).
    SendResult sendToVirtualDJ(const Models::Track& track, DeckSlot deck);
    SendResult sendToRekordbox(const std::vector<Models::Track>& tracks);
    SendResult sendToSerato(const std::vector<Models::Track>& tracks);
    SendResult sendToTraktor(const std::vector<Models::Track>& tracks);
    SendResult sendToEngineDJ(const std::vector<Models::Track>& tracks);

    static std::string userProfilePath();
    static std::string documentsPath();
    static std::string appDataRoamingPath();
    static std::string musicPath();

    std::unique_ptr<DJSoftwareManager> manager_;
    std::vector<DJTarget> preference_;
};

} // namespace BeatMate::Services::DJSoftware
