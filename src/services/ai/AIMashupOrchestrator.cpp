#include "AIMashupOrchestrator.h"

#include "../preparation/SetCompatibilityScorer.h"
#include "../preparation/CamelotMoveClassifier.h"

#include <juce_events/juce_events.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace BeatMate::Services::AI {

namespace {

constexpr double kBeatsPerBar = 4.0;

double barsToSeconds(int bars, double bpm) {
    if (bpm <= 0.0) return 0.0;
    return (bars * kBeatsPerBar * 60.0) / bpm;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int parseCamelotNumber(const std::string& key) {
    if (key.empty()) return -1;
    int n = 0;
    size_t i = 0;
    while (i < key.size() && std::isdigit(static_cast<unsigned char>(key[i]))) {
        n = n * 10 + (key[i] - '0');
        ++i;
    }
    if (i == 0) return -1;
    return n;
}

char parseCamelotLetter(const std::string& key) {
    for (auto c : key) {
        if (c == 'A' || c == 'a') return 'A';
        if (c == 'B' || c == 'b') return 'B';
    }
    return '?';
}

int camelotDistance(const std::string& a, const std::string& b) {
    int na = parseCamelotNumber(a);
    int nb = parseCamelotNumber(b);
    if (na <= 0 || nb <= 0) return 6;
    int d = std::abs(na - nb);
    if (d > 6) d = 12 - d;
    char la = parseCamelotLetter(a);
    char lb = parseCamelotLetter(b);
    if (la != lb) d += 1;
    return d;
}

double pairScore(const Models::Track& a, const Models::Track& b) {
    double s = 100.0;
    s -= std::abs(a.bpm - b.bpm) * 2.0;
    s -= camelotDistance(a.camelotKey.empty() ? a.key : a.camelotKey,
                         b.camelotKey.empty() ? b.key : b.camelotKey) * 5.0;
    s -= std::abs(a.energy - b.energy) * 1.5;
    return s;
}

bool isHarmonicCompatible(const Models::Track& a, const Models::Track& b) {
    return camelotDistance(a.camelotKey.empty() ? a.key : a.camelotKey,
                           b.camelotKey.empty() ? b.key : b.camelotKey) <= 1;
}

void postProgress(AIMashupOrchestrator::ProgressCb cb, float pct, const juce::String& phase) {
    if (!cb) return;
    juce::MessageManager::callAsync([cb, pct, phase]() { cb(pct, phase); });
}

} // namespace

AIMashupOrchestrator::AIMashupOrchestrator() = default;

AIMashupOrchestrator::~AIMashupOrchestrator() {
    cancel();
    if (m_worker) {
        m_worker->stopThread(2000);
        m_worker.reset();
    }
}

void AIMashupOrchestrator::cancel() {
    m_cancel.store(true);
}

int AIMashupOrchestrator::defaultDurationFor(MashupType t) const noexcept {
    switch (t) {
        case MashupType::Mashup:  return 120;
        case MashupType::Megamix: return 240;
        case MashupType::Medley:  return 360;
        case MashupType::Remix:   return 240;
    }
    return 240;
}

TransitionHint AIMashupOrchestrator::pickTransitionHint(double energyA, double energyB, MashupType type) const noexcept {
    const double diff = energyB - energyA;
    if (type == MashupType::Mashup)  return TransitionHint::EqualPower;
    if (type == MashupType::Remix)   return TransitionHint::Cut;
    if (type == MashupType::Medley)  return diff > 1.5 ? TransitionHint::FilterSweep : TransitionHint::EqualPower;
    if (diff > 2.0)  return TransitionHint::FilterSweep;
    if (diff < -2.0) return TransitionHint::EchoOut;
    return TransitionHint::EqualPower;
}

juce::String AIMashupOrchestrator::pickTransitionKind(double energyA, double energyB, MashupType type) const {
    return juce::String(toString(pickTransitionHint(energyA, energyB, type)));
}

void AIMashupOrchestrator::generateAsync(const MashupRequest& req,
                                          ProgressCb onProgress,
                                          DoneCb     onDone) {
    if (m_busy.exchange(true)) {
        if (onDone) {
            MashupResult r;
            r.ok = false;
            r.errorMessage = "AIMashupOrchestrator already busy";
            juce::MessageManager::callAsync([onDone, r]() { onDone(r); });
        }
        return;
    }
    m_cancel.store(false);

    if (m_worker) {
        m_worker->stopThread(1000);
        m_worker.reset();
    }

    class WorkerThread : public juce::Thread {
    public:
        WorkerThread(AIMashupOrchestrator& o,
                     MashupRequest r,
                     ProgressCb p,
                     DoneCb d)
            : juce::Thread("AIMashupOrchestrator"),
              owner(o), req(std::move(r)),
              progress(std::move(p)), done(std::move(d)) {}

        void run() override {
            MashupResult res = owner.generateInternal(req, progress);
            owner.m_busy.store(false);
            if (done) {
                juce::MessageManager::callAsync([cb = done, res]() { cb(res); });
            }
        }

        AIMashupOrchestrator& owner;
        MashupRequest         req;
        ProgressCb            progress;
        DoneCb                done;
    };

    m_worker = std::make_unique<WorkerThread>(*this, req, std::move(onProgress), std::move(onDone));
    m_worker->startThread();
}

MashupResult AIMashupOrchestrator::generateInternal(const MashupRequest& req, ProgressCb onProgress) {
    MashupResult result;

    postProgress(onProgress, 0.02f, "Analyzing request");

    if (req.sourceTracks.empty() && req.libraryPool.empty()) {
        result.ok = false;
        result.errorMessage = "No source tracks and empty library pool";
        return result;
    }

    auto pickedTracks = selectTracks(req, onProgress);
    if (m_cancel.load()) {
        result.errorMessage = "Cancelled";
        return result;
    }
    if (pickedTracks.empty()) {
        result.ok = false;
        result.errorMessage = "Track selection failed";
        return result;
    }

    postProgress(onProgress, 0.35f, "Selection done");

    switch (req.type) {
        case MashupType::Mashup:  result = buildMashup(req, pickedTracks);  break;
        case MashupType::Megamix: result = buildMegamix(req, pickedTracks); break;
        case MashupType::Medley:  result = buildMedley(req, pickedTracks);  break;
        case MashupType::Remix:   result = buildRemix(req, pickedTracks);   break;
    }

    if (m_cancel.load()) {
        MashupResult r;
        r.ok = false;
        r.errorMessage = "Cancelled";
        return r;
    }

    postProgress(onProgress, 0.95f, "Finalizing");

    if (result.ok) {
        std::vector<double> bpms;
        bpms.reserve(result.clips.size());
        for (const auto& c : result.clips) {
            if (c.track.bpm > 0.0) bpms.push_back(c.track.bpm);
        }
        if (!bpms.empty()) result.averageBpm = median(bpms);
    }

    postProgress(onProgress, 1.0f, "Done");
    return result;
}

std::vector<Models::Track> AIMashupOrchestrator::selectTracks(const MashupRequest& req,
                                                                ProgressCb onProgress) {
    if (!req.sourceTracks.empty()) {
        postProgress(onProgress, 0.20f, "Using provided tracks");
        return req.sourceTracks;
    }

    std::vector<Models::Track> pool = req.libraryPool;

    auto bpmFilter = [&](const Models::Track& t) {
        if (t.bpm <= 0.0) return false;
        if (req.type == MashupType::Megamix || req.type == MashupType::Medley) {
            return t.bpm >= 118.0 && t.bpm <= 138.0;
        }
        return t.bpm >= 90.0 && t.bpm <= 160.0;
    };

    pool.erase(std::remove_if(pool.begin(), pool.end(),
                              [&](const Models::Track& t) { return !bpmFilter(t); }),
               pool.end());

    if (pool.empty()) return {};

    int target = 12;
    switch (req.type) {
        case MashupType::Mashup:  target = 3;  break;
        case MashupType::Megamix: target = 12; break;
        case MashupType::Medley:  target = 6;  break;
        case MashupType::Remix:   target = 1;  break;
    }
    if (target > static_cast<int>(pool.size())) target = static_cast<int>(pool.size());

    postProgress(onProgress, 0.10f, "Filtering pool");

    Preparation::SetCompatibilityScorer scorer;

    std::vector<Models::Track> selected;
    selected.reserve(target);

    int seedIdx = 0;
    if (req.targetBpm > 0) {
        double best = 1e9;
        for (size_t i = 0; i < pool.size(); ++i) {
            double d = std::abs(pool[i].bpm - req.targetBpm);
            if (d < best) { best = d; seedIdx = static_cast<int>(i); }
        }
    }
    selected.push_back(pool[seedIdx]);
    pool.erase(pool.begin() + seedIdx);

    postProgress(onProgress, 0.18f, "Greedy selection");

    while (static_cast<int>(selected.size()) < target && !pool.empty()) {
        if (m_cancel.load()) return {};

        const auto& last = selected.back();
        int bestIdx = -1;
        double bestScore = -1e9;

        for (size_t i = 0; i < pool.size(); ++i) {
            const auto& cand = pool[i];
            if (req.harmonicOnly && !isHarmonicCompatible(last, cand)) continue;

            double s = pairScore(last, cand);

            auto sc = scorer.score(last, cand);
            s = (s + static_cast<double>(sc.score)) * 0.5;

            if (s > bestScore) { bestScore = s; bestIdx = static_cast<int>(i); }
        }

        if (bestIdx < 0) {
            double bestFallback = -1e9;
            for (size_t i = 0; i < pool.size(); ++i) {
                double s = pairScore(last, pool[i]);
                if (s > bestFallback) { bestFallback = s; bestIdx = static_cast<int>(i); }
            }
            if (bestIdx < 0) break;
        }

        selected.push_back(pool[bestIdx]);
        pool.erase(pool.begin() + bestIdx);

        float pct = 0.18f + 0.12f * (selected.size() / static_cast<float>(target));
        postProgress(onProgress, pct, "Selecting tracks");
    }

    return selected;
}

MashupResult AIMashupOrchestrator::buildMegamix(const MashupRequest& req,
                                                  const std::vector<Models::Track>& tracks) {
    MashupResult res;
    if (tracks.empty()) {
        res.errorMessage = "No tracks for megamix";
        return res;
    }

    double targetBpm = req.targetBpm > 0 ? req.targetBpm : 0.0;
    if (targetBpm <= 0.0) {
        std::vector<double> bpms;
        for (const auto& t : tracks) if (t.bpm > 0) bpms.push_back(t.bpm);
        targetBpm = median(bpms);
    }
    if (targetBpm <= 0.0) {
        res.errorMessage = juce::String::fromUTF8(u8"BPM inconnu : analysez les pistes avant de générer un megamix");
        return res;
    }

    const int targetDur = req.targetDurationSec > 0 ? req.targetDurationSec
                                                    : defaultDurationFor(MashupType::Megamix);

    double timeline = 0.0;
    double perClip  = static_cast<double>(targetDur) / static_cast<double>(tracks.size());
    perClip = std::clamp(perClip, 18.0, 30.0);

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];

        MashupClipPlan clip;
        clip.track = t;

        double srcStart = t.introEnd > 0.0 ? t.introEnd : 30.0;
        if (srcStart >= t.duration - 5.0) srcStart = std::max(0.0, t.duration * 0.25);
        double srcEnd = std::min(t.duration > 0 ? t.duration : (srcStart + 90.0),
                                 srcStart + 90.0);

        clip.sourceStartSec   = srcStart;
        clip.sourceEndSec     = srcEnd;
        clip.timelineStartSec = timeline;
        clip.bpmAdjustRatio   = (t.bpm > 0.0) ? targetBpm / t.bpm : 1.0;
        clip.reason           = juce::String::formatted("Bar %d in mix, BPM %.1f -> %.1f",
                                                         static_cast<int>(i) + 1,
                                                         t.bpm, targetBpm);

        res.clips.push_back(std::move(clip));

        if (i + 1 < tracks.size()) {
            MashupTransitionPlan tr;
            tr.gapIndex = static_cast<int>(i);
            tr.durationBars = (std::abs(tracks[i].bpm - tracks[i + 1].bpm) > 5.0) ? 16 : 8;
            tr.hint = pickTransitionHint(tracks[i].energy, tracks[i + 1].energy, MashupType::Megamix);
            tr.kind = juce::String(toString(tr.hint));
            res.transitions.push_back(tr);

            const double overlapSec = barsToSeconds(tr.durationBars, targetBpm);
            timeline += perClip - overlapSec;
        } else {
            timeline += perClip;
        }
    }

    res.ok = true;
    res.averageBpm = targetBpm;
    if (req.skill == SkillMode::Expert) {
        res.reasoning = juce::String::formatted("Megamix %d tracks @ %.1f BPM, harmonic chain",
                                                 static_cast<int>(tracks.size()), targetBpm);
    } else {
        res.reasoning = "Auto megamix generated";
    }
    return res;
}

MashupResult AIMashupOrchestrator::buildMashup(const MashupRequest& req,
                                                 const std::vector<Models::Track>& tracks) {
    MashupResult res;
    if (tracks.empty()) {
        res.errorMessage = "No tracks for mashup";
        return res;
    }

    const int targetDur = req.targetDurationSec > 0 ? req.targetDurationSec
                                                    : defaultDurationFor(MashupType::Mashup);

    double targetBpm = req.targetBpm > 0 ? req.targetBpm : tracks.front().bpm;
    if (targetBpm <= 0.0) {
        res.errorMessage = juce::String::fromUTF8(u8"BPM inconnu : analysez les pistes avant de générer un mashup");
        return res;
    }

    auto pickRange = [&](const Models::Track& t) -> std::pair<double, double> {
        double s = t.introEnd > 0.0 ? t.introEnd : (t.duration > 0 ? t.duration * 0.25 : 30.0);
        double e = std::min(s + static_cast<double>(targetDur),
                            t.duration > 0 ? t.duration : s + targetDur);
        return { s, e };
    };

    for (size_t i = 0; i < tracks.size() && i < 3; ++i) {
        const auto& t = tracks[i];
        MashupClipPlan clip;
        clip.track = t;
        auto rng = pickRange(t);
        clip.sourceStartSec   = rng.first;
        clip.sourceEndSec     = rng.second;
        clip.timelineStartSec = 0.0;
        clip.bpmAdjustRatio   = (t.bpm > 0.0) ? targetBpm / t.bpm : 1.0;

        if (req.useStems) {
            if (i == 0) clip.stemsToUse = { "drums", "bass", "other" };
            else if (i == 1) clip.stemsToUse = { "vocals" };
            else clip.stemsToUse = { "other" };
        }
        clip.reason = (i == 0) ? "Instrumental bed"
                                : (i == 1 ? "Vocal layer" : "Texture layer");
        res.clips.push_back(std::move(clip));
    }

    res.ok = true;
    res.averageBpm = targetBpm;
    res.reasoning  = req.skill == SkillMode::Expert
        ? juce::String::formatted("Mashup of %d layers, harmonic match",
                                   static_cast<int>(res.clips.size()))
        : "Auto mashup generated";
    return res;
}

MashupResult AIMashupOrchestrator::buildMedley(const MashupRequest& req,
                                                 const std::vector<Models::Track>& tracks) {
    MashupResult res;
    if (tracks.empty()) {
        res.errorMessage = "No tracks for medley";
        return res;
    }

    double targetBpm = req.targetBpm > 0 ? req.targetBpm : 0.0;
    if (targetBpm <= 0.0) {
        std::vector<double> bpms;
        for (const auto& t : tracks) if (t.bpm > 0) bpms.push_back(t.bpm);
        targetBpm = median(bpms);
    }
    if (targetBpm <= 0.0) {
        res.errorMessage = juce::String::fromUTF8(u8"BPM inconnu : analysez les pistes avant de générer un medley");
        return res;
    }

    const int targetDur = req.targetDurationSec > 0 ? req.targetDurationSec
                                                    : defaultDurationFor(MashupType::Medley);

    double perClip = std::clamp(static_cast<double>(targetDur) / tracks.size(), 30.0, 60.0);
    double timeline = 0.0;

    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        MashupClipPlan clip;
        clip.track = t;

        double srcStart = t.outroStart > 0.0 ? std::max(0.0, t.outroStart - perClip)
                                              : (t.introEnd > 0.0 ? t.introEnd : 30.0);
        if (srcStart >= t.duration - 5.0 && t.duration > 0.0) srcStart = t.duration * 0.3;
        double srcEnd = std::min(srcStart + perClip,
                                 t.duration > 0 ? t.duration : srcStart + perClip);

        clip.sourceStartSec   = srcStart;
        clip.sourceEndSec     = srcEnd;
        clip.timelineStartSec = timeline;
        clip.bpmAdjustRatio   = (t.bpm > 0.0) ? targetBpm / t.bpm : 1.0;
        clip.reason           = juce::String::formatted("Section %d", static_cast<int>(i) + 1);
        res.clips.push_back(std::move(clip));

        if (i + 1 < tracks.size()) {
            MashupTransitionPlan tr;
            tr.gapIndex     = static_cast<int>(i);
            tr.durationBars = 12;
            if (std::abs(tracks[i].bpm - tracks[i + 1].bpm) > 4.0) tr.durationBars = 16;
            tr.hint = pickTransitionHint(tracks[i].energy, tracks[i + 1].energy, MashupType::Medley);
            tr.kind = juce::String(toString(tr.hint));
            res.transitions.push_back(tr);

            const double overlapSec = barsToSeconds(tr.durationBars, targetBpm);
            timeline += perClip - overlapSec;
        } else {
            timeline += perClip;
        }
    }

    res.ok = true;
    res.averageBpm = targetBpm;
    res.reasoning  = req.skill == SkillMode::Expert
        ? juce::String::formatted("Medley %d tracks, %d bars overlap",
                                   static_cast<int>(tracks.size()), 12)
        : "Auto medley generated";
    return res;
}

MashupResult AIMashupOrchestrator::buildRemix(const MashupRequest& req,
                                                const std::vector<Models::Track>& tracks) {
    MashupResult res;
    if (tracks.empty()) {
        res.errorMessage = "No track for remix";
        return res;
    }

    const auto& t = tracks.front();
    const double dur = t.duration > 0.0 ? t.duration : 240.0;

    double bpm = req.targetBpm > 0 ? (double) req.targetBpm : t.bpm;
    if (bpm <= 0.0) {
        res.errorMessage = juce::String::fromUTF8(u8"BPM inconnu : analysez la piste avant de générer un remix");
        return res;
    }

    auto add = [&](double srcS, double srcE, double tlStart,
                   std::vector<juce::String> stems, juce::String reason) {
        MashupClipPlan c;
        c.track = t;
        c.sourceStartSec   = srcS;
        c.sourceEndSec     = srcE;
        c.timelineStartSec = tlStart;
        c.bpmAdjustRatio   = (t.bpm > 0.0) ? bpm / t.bpm : 1.0;
        c.stemsToUse       = std::move(stems);
        c.reason           = std::move(reason);
        res.clips.push_back(std::move(c));
    };

    const double introEnd  = t.introEnd  > 0 ? t.introEnd  : std::min(32.0, dur * 0.15);
    const double outroStart = t.outroStart > 0 ? t.outroStart : std::max(dur - 32.0, dur * 0.85);
    const double drop1Start = std::min(introEnd + 16.0, dur * 0.35);
    const double drop1End   = std::min(drop1Start + 32.0, dur * 0.55);
    const double bridgeStart = std::min(drop1End + 8.0, dur * 0.65);
    const double bridgeEnd   = std::min(bridgeStart + 24.0, outroStart);

    double tl = 0.0;
    add(0.0,         introEnd,        tl,                                { "vocals" },              "Vocal-only intro");
    tl += introEnd;
    add(drop1Start,  drop1End,        tl,                                { "drums", "bass" },       "Drum/bass drop");
    tl += (drop1End - drop1Start);
    add(drop1Start,  drop1End,        tl,                                { "drums", "bass", "other" }, "Drop reprise");
    tl += (drop1End - drop1Start);
    add(bridgeStart, bridgeEnd,       tl,                                { "other" },                "Instrumental bridge");
    tl += (bridgeEnd - bridgeStart);
    add(outroStart,  dur,             tl,                                {},                         "Outro");

    for (size_t i = 0; i + 1 < res.clips.size(); ++i) {
        MashupTransitionPlan tr;
        tr.gapIndex     = static_cast<int>(i);
        tr.durationBars = 4;
        tr.hint         = TransitionHint::Cut;
        tr.kind         = juce::String(toString(tr.hint));
        res.transitions.push_back(tr);
    }

    res.ok = true;
    res.averageBpm = bpm;
    res.reasoning  = req.skill == SkillMode::Expert
        ? juce::String::formatted("Remix from stems: intro/drop x2/bridge/outro @ %.1f BPM", bpm)
        : "Auto remix generated";
    return res;
}

} // namespace BeatMate::Services::AI
