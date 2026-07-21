#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../models/Track.h"

namespace BeatMate::Services::AI {

enum class MashupType {
    Mashup,
    Megamix,
    Medley,
    Remix
};

enum class SkillMode { Novice, Expert };

// Type-safe hint for transition selection. POD enum, trivially copyable
enum class TransitionHint {
    Cut,
    Fade,
    EqualPower,
    EchoOut,
    FilterSweep,
    Backspin,
    Custom
};

// Libelle canonique d'un TransitionHint
inline std::string toString(TransitionHint h) noexcept {
    switch (h) {
        case TransitionHint::Cut:         return "Cut";
        case TransitionHint::Fade:        return "Fade";
        case TransitionHint::EqualPower:  return "EqualPower";
        case TransitionHint::EchoOut:     return "EchoOut";
        case TransitionHint::FilterSweep: return "FilterSweep";
        case TransitionHint::Backspin:    return "Backspin";
        case TransitionHint::Custom:      return "Custom";
    }
    return "EqualPower";
}

// Parse un TransitionHint ; inconnu = Crossfade
inline TransitionHint hintFromString(const std::string& s) noexcept {
    if (s == "Cut")         return TransitionHint::Cut;
    if (s == "Fade")        return TransitionHint::Fade;
    if (s == "EqualPower")  return TransitionHint::EqualPower;
    if (s == "EchoOut")     return TransitionHint::EchoOut;
    if (s == "FilterSweep") return TransitionHint::FilterSweep;
    if (s == "Backspin")    return TransitionHint::Backspin;
    if (s == "Custom")      return TransitionHint::Custom;
    // XFade = alias externe de Crossfade
    return TransitionHint::EqualPower;
}

struct MashupRequest {
    MashupType type = MashupType::Megamix;
    SkillMode  skill = SkillMode::Novice;
    std::vector<Models::Track> sourceTracks;
    std::vector<Models::Track> libraryPool;
    int targetDurationSec = 0;
    int targetBpm = 0;
    juce::String targetKey;
    bool useStems = true;
    bool harmonicOnly = true;
};

struct MashupClipPlan {
    Models::Track track;
    double sourceStartSec = 0.0;
    double sourceEndSec   = 0.0;
    double timelineStartSec = 0.0;
    double bpmAdjustRatio = 1.0;
    double pitchSemitones = 0.0;
    juce::String reason;
    std::vector<juce::String> stemsToUse;
};

struct MashupTransitionPlan {
    int gapIndex = 0;
    juce::String kind;                         // back-compat string label
    TransitionHint hint = TransitionHint::EqualPower; // type-safe duplicate of `kind`
    int durationBars = 8;
};

struct MashupResult {
    bool ok = false;
    juce::String errorMessage;
    std::vector<MashupClipPlan>       clips;
    std::vector<MashupTransitionPlan> transitions;
    double averageBpm = 0.0;
    juce::String reasoning;
};

class AIMashupOrchestrator {
public:
    using ProgressCb = std::function<void(float pct, const juce::String& phase)>;
    using DoneCb     = std::function<void(MashupResult)>;

    AIMashupOrchestrator();
    ~AIMashupOrchestrator();

    void generateAsync(const MashupRequest& req,
                       ProgressCb onProgress,
                       DoneCb     onDone);
    void cancel();
    bool isBusy() const noexcept { return m_busy.load(); }

private:
    std::atomic<bool>             m_busy { false };
    std::atomic<bool>             m_cancel { false };
    std::unique_ptr<juce::Thread> m_worker;

    MashupResult generateInternal(const MashupRequest& req, ProgressCb onProgress);
    std::vector<Models::Track> selectTracks(const MashupRequest& req, ProgressCb onProgress);
    MashupResult buildMashup(const MashupRequest& req, const std::vector<Models::Track>& tracks);
    MashupResult buildMegamix(const MashupRequest& req, const std::vector<Models::Track>& tracks);
    MashupResult buildMedley(const MashupRequest& req, const std::vector<Models::Track>& tracks);
    MashupResult buildRemix(const MashupRequest& req, const std::vector<Models::Track>& tracks);

    int defaultDurationFor(MashupType t) const noexcept;

    // Type-safe transition selection. New code should call this directly.
    TransitionHint pickTransitionHint(double energyA, double energyB, MashupType type) const noexcept;

    // Back-compat string wrapper. Returns toString(pickTransitionHint(...))
    juce::String pickTransitionKind(double energyA, double energyB, MashupType type) const;
};

} // namespace BeatMate::Services::AI
