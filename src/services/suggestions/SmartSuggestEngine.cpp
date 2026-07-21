#include "SmartSuggestEngine.h"

#include "MyStyleModel.h"
#include "../library/TrackDatabase.h"
#include "../../models/Track.h"
#include "../../core/analysis/CollectionRadar.h"
#include "../djsoftware/virtualdj/VirtualDJRemote.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace BeatMate::Services::Suggestions {

namespace {

constexpr int64_t kRecentSeconds           = 30 * 60;
constexpr int64_t kRecentlyPlayedCacheTtl  = 60;
constexpr double  kIncompatibleBpmPct      = 20.0;

int64_t nowUnix() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool parseCamelot(const std::string& s, int& number, char& letter) {
    if (s.size() < 2) return false;
    try {
        number = std::stoi(s.substr(0, s.size() - 1));
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s.back())));
        if (c != 'A' && c != 'B') return false;
        if (number < 1 || number > 12) return false;
        letter = c;
        return true;
    } catch (...) {
        return false;
    }
}

int wheelDistance(const std::string& keyA, const std::string& keyB) {
    int nA = 0, nB = 0;
    char lA = 'A', lB = 'A';
    if (!parseCamelot(keyA, nA, lA) || !parseCamelot(keyB, nB, lB)) return 0;
    int diff = nB - nA;
    while (diff >  6) diff -= 12;
    while (diff < -6) diff += 12;
    return diff;
}

std::string toLowerCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimCopy(std::string s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

struct HarmonicRelation {
    int         score = -1;
    const char* name  = nullptr;
};

HarmonicRelation harmonicRelationDirectional(const std::string& keyA,
                                             const std::string& keyB,
                                             bool energyBoostIntent) {
    int numA = 0, numB = 0;
    char letA = 'A', letB = 'A';
    if (!parseCamelot(keyA, numA, letA) || !parseCamelot(keyB, numB, letB))
        return {};

    int delta = (numB - numA) % 12;
    if (delta < 0) delta += 12;
    if (delta > 6) delta -= 12;

    if (letA == letB) {
        switch (delta) {
            case  0: return { 100, "Parfaite" };
            case +1: return {  90, "Dominante +1" };
            case -1: return {  88, "Sous-dominante -1" };
            case +2: return energyBoostIntent
                       ? HarmonicRelation{ 85, "Energy Boost +2" }
                       : HarmonicRelation{ 60, "Energy Boost +2" };
            case -2: return {  45, "Energy Drop -2" };
            case +3: return {  30, "Tierce" };
            case -3: return {  30, "Tierce" };
            case +4: return {  30, "Tierce" };
            case -4: return {  20, "\xc3\x89loign\xc3\xa9""e" };
            case +5: return {  18, "\xc3\x89loign\xc3\xa9""e" };
            case -5: return {  40, "Boost Armin +1/2 ton" };
            case +6: return {   8, "Triton" };
        }
    } else {
        const bool aToB = (letA == 'A' && letB == 'B');
        switch (delta) {
            case  0: return { 95, "Relative" };
            case +1: return aToB
                       ? HarmonicRelation{ 65, "Diagonale floue" }
                       : HarmonicRelation{ 75, "Diagonale" };
            case -1: return aToB
                       ? HarmonicRelation{ 75, "Diagonale" }
                       : HarmonicRelation{ 65, "Diagonale floue" };
            case +3: if (aToB)  return { 50, "Parall\xc3\xa8le" };
                     break;
            case -3: if (!aToB) return { 50, "Parall\xc3\xa8le" };
                     break;
            default: break;
        }
        const int ad = std::abs(delta);
        if (ad == 2) return { 25, "\xc3\x89loign\xc3\xa9""e" };
        if (ad == 3) return { 20, "\xc3\x89loign\xc3\xa9""e" };
        if (ad == 4) return { 18, "\xc3\x89loign\xc3\xa9""e" };
        if (ad == 5) return { 15, "\xc3\x89loign\xc3\xa9""e" };
        if (ad == 6) return {  8, "Triton" };
    }
    return { 10, "\xc3\x89loign\xc3\xa9""e" };
}

double normalizeConfidence(float c) {
    double d = static_cast<double>(c);
    if (d <= 0.0) return 1.0;
    if (d > 1.0)  d = 1.0;
    return d;
}

std::string normalizeGenre(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool lastSpace = false;
    for (unsigned char c : s) {
        if (c == '-' || c == '_' || c == '/' || c == '&' || c == ',') c = ' ';
        c = static_cast<unsigned char>(std::tolower(c));
        if (c == ' ' || c == '\t') {
            if (!lastSpace && !out.empty()) out.push_back(' ');
            lastSpace = true;
        } else {
            out.push_back(static_cast<char>(c));
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool genreMatches(const std::string& needle, const std::string& candidate) {
    const std::string n = normalizeGenre(needle);
    const std::string c = normalizeGenre(candidate);
    if (n.empty() || c.empty()) return false;
    if (c.find(n) != std::string::npos) return true;
    if (n.find(c) != std::string::npos) return true;
    return false;
}

int genreGraphDelta(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return 0;
    const std::string la = toLowerCopy(a);
    const std::string lb = toLowerCopy(b);
    if (la == lb) return 20;

    struct Edge { const char* from; const char* to; int delta; };
    // Symmetric unless otherwise noted. Values tuned for EDM/DJ sets.
    static const Edge kEdges[] = {
        {"house",       "tech house",  25},
        {"house",       "deep house",  22},
        {"house",       "progressive", 20},
        {"tech house",  "techno",      18},
        {"deep house",  "house",       22},
        {"deep house",  "melodic house", 20},
        {"house",       "hip-hop",    -15},
        {"house",       "hip hop",    -15},
        {"techno",      "hip-hop",    -25},
        {"house",       "dnb",        -20},
        {"house",       "drum and bass", -20},
        {"hip-hop",     "rnb",         20},
        {"hip hop",     "rnb",         20},
        {"hip-hop",     "trap",        18},
        {"afro house",  "house",       22},
        {"afro house",  "tech house",  18},
        {"reggaeton",   "latin",       22},
        {"pop",         "dance",       15},
        {"pop",         "rock",         5},
        {"rock",        "techno",     -20},
        {"trance",      "progressive", 22},
        {"trance",      "techno",      12},
    };

    for (const auto& e : kEdges) {
        const std::string ef = e.from;
        const std::string et = e.to;
        if ((la == ef && lb == et) || (la == et && lb == ef)) return e.delta;
    }

    // Substring fallback — catches "Tech House" vs "tech-house" variants.
    for (const auto& e : kEdges) {
        const std::string ef = e.from;
        const std::string et = e.to;
        const bool fwd = la.find(ef) != std::string::npos && lb.find(et) != std::string::npos;
        const bool bwd = la.find(et) != std::string::npos && lb.find(ef) != std::string::npos;
        if (fwd || bwd) return e.delta;
    }

    return 0;
}

} // namespace


// Scoring helpers — presence-aware. Returning -1 signals "feature missing"
int SmartSuggestEngine::calcHarmonicScore(const std::string& keyA, const std::string& keyB) {
    return calcHarmonicScore(keyA, keyB, false);
}

int SmartSuggestEngine::calcHarmonicScore(const std::string& keyA, const std::string& keyB,
                                          bool energyBoostIntent) {
    return harmonicRelationDirectional(keyA, keyB, energyBoostIntent).score;
}

std::string SmartSuggestEngine::harmonicRelationName(const std::string& keyA, const std::string& keyB,
                                                     bool energyBoostIntent) {
    const auto rel = harmonicRelationDirectional(keyA, keyB, energyBoostIntent);
    return rel.name != nullptr ? std::string(rel.name) : std::string();
}

int SmartSuggestEngine::calcBPMScore(double bpmA, double bpmB) {
    if (bpmA <= 0.0 || bpmB <= 0.0) return -1;

    const double r1 = bpmB / bpmA;
    const double r2 = (bpmB * 2.0) / bpmA;
    const double r3 = bpmB / (bpmA * 2.0);

    double bestR = r1;
    bool isHalfDouble = false;
    if (std::abs(r2 - 1.0) < std::abs(bestR - 1.0)) { bestR = r2; isHalfDouble = true; }
    if (std::abs(r3 - 1.0) < std::abs(bestR - 1.0)) { bestR = r3; isHalfDouble = true; }

    double pct = std::abs(bestR - 1.0) * 100.0;
    pct *= (bestR > 1.0) ? 1.0 : 0.765;

    // Tolerance bands calibrated on ISMIR 2020 real-world DJ-mix analysis
    int s;
    if      (pct <= 0.3)  s = 100;
    else if (pct <= 1.0)  s =  97;
    else if (pct <= 2.0)  s =  92;
    else if (pct <= 3.5)  s =  82;
    else if (pct <= 5.0)  s =  70;
    else if (pct <= 8.0)  s =  50;
    else if (pct <= 12.0) s =  30;
    else if (pct <= 20.0) s =  15;
    else                  s =   5;

    if (isHalfDouble) s = static_cast<int>(std::round(s * 0.90));
    return s;
}

int SmartSuggestEngine::calcEnergyScore(double energyA, double energyB, EnergyDirection dir) {
    if (energyA <= 0.0 || energyB <= 0.0) return -1;
    double diff = energyB - energyA;
    double adiff = std::abs(diff);

    int s = 20;
    switch (dir) {
        case EnergyDirection::Maintain:
            if      (adiff <= 0.5) s = 100;
            else if (adiff <= 1.0) s = 80;
            else if (adiff <= 2.0) s = 55;
            else if (adiff <= 3.0) s = 30;
            else                   s = 10;
            break;

        case EnergyDirection::Increase:
            if      (diff >= 0.5 && diff <= 2.5) s = 100;
            else if (diff > 2.5 && diff <= 4.0)  s = 70;
            else if (diff > 0.0 && diff < 0.5)   s = 75;
            else if (adiff < 0.3)                s = 60;
            else                                 s = 20;
            break;

        case EnergyDirection::Decrease:
            if      (diff <= -0.5 && diff >= -2.5) s = 100;
            else if (diff < -2.5)                  s = 70;
            else                                   s = 20;
            break;

        case EnergyDirection::Auto:
        default:
            if      (adiff <= 1.5) s = 85;
            else if (adiff <= 2.5) s = 65;
            else if (adiff <= 3.5) s = 40;
            else                   s = 20;
            break;
    }

    if (adiff > 2.0 && s > 15) s = 15;
    return s;
}

double SmartSuggestEngine::energyAtEdge(const std::string& energySegmentsJson, bool outro) {
    if (energySegmentsJson.empty()) return -1.0;

    struct Seg { double a = 0.0; double b = 0.0; double e = 0.0; };
    std::vector<Seg> segs;
    try {
        auto j = nlohmann::json::parse(energySegmentsJson);
        if (!j.is_array() || j.empty()) return -1.0;
        for (const auto& el : j) {
            Seg sg;
            if (el.is_array() && el.size() >= 3) {
                sg.a = el[0].get<double>();
                sg.b = el[1].get<double>();
                sg.e = el[2].get<double>();
            } else if (el.is_object()) {
                sg.a = el.value("startTime", el.value("startSec", -1.0));
                sg.b = el.value("endTime",   el.value("endSec",   -1.0));
                sg.e = el.value("energy", -1.0);
            } else {
                continue;
            }
            if (sg.b > sg.a && sg.e > 0.0) segs.push_back(sg);
        }
    } catch (...) {
        return -1.0;
    }
    if (segs.empty()) return -1.0;

    double endMax = 0.0;
    for (const auto& sg : segs) endMax = std::max(endMax, sg.b);

    constexpr double kEdgeWindowSec = 30.0;
    const double win0 = outro ? std::max(0.0, endMax - kEdgeWindowSec) : 0.0;
    const double win1 = outro ? endMax : kEdgeWindowSec;

    double eSum = 0.0, wSum = 0.0;
    for (const auto& sg : segs) {
        const double lo = std::max(sg.a, win0);
        const double hi = std::min(sg.b, win1);
        if (hi > lo) { eSum += sg.e * (hi - lo); wSum += (hi - lo); }
    }
    if (wSum <= 0.0) return -1.0;
    return eSum / wSum;
}

int SmartSuggestEngine::calcStructureScore(double /*introStartA*/, double /*introEndA*/,
                                           double outroStartA, double outroEndA,
                                           double introStartB, double introEndB,
                                           double /*outroStartB*/, double /*outroEndB*/) {
    int score = 50;
    if (outroStartA >= 0.0 && outroEndA > outroStartA) {
        double len = outroEndA - outroStartA;
        if (len >= 8.0 && len <= 64.0)      score += 20;
        else if (len >= 4.0)                score += 10;
        else                                score -= 10;
    }
    if (introStartB >= 0.0 && introEndB > introStartB) {
        double len = introEndB - introStartB;
        if (len >= 8.0 && len <= 64.0)      score += 20;
        else if (len >= 4.0)                score += 10;
        else                                score -= 10;
    }
    if (score < 0)   score = 0;
    if (score > 100) score = 100;
    return score;
}

int SmartSuggestEngine::calcEraScore(int candYear, int preferredCenter, int spread) {
    if (candYear <= 0 || preferredCenter <= 0) return -1;
    if (spread <= 0) spread = 5;
    int d = std::abs(candYear - preferredCenter);
    if (d == 0)           return 100;
    if (d <= spread / 2)  return 90;
    if (d <= spread)      return 75;
    if (d <= spread * 2)  return 55;
    if (d <= spread * 3)  return 35;
    return 15;
}

int SmartSuggestEngine::calcGenreGraphScore(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return -1;
    return 50 + genreGraphDelta(a, b);
}

Suggestion::Level SmartSuggestEngine::levelFor(int score) {
    if (score >= 80) return Suggestion::Level::Green;
    if (score >= 55) return Suggestion::Level::Yellow;
    return Suggestion::Level::Red;
}


SmartSuggestEngine::SmartSuggestEngine(std::shared_ptr<Library::TrackDatabase> db)
    : db_(std::move(db)) {
    m_style = std::make_unique<MyStyleModel>(db_);
    m_style->train();
    lastStyleTrainUnix_ = nowUnix();
}

SmartSuggestEngine::~SmartSuggestEngine() = default;

void SmartSuggestEngine::setCollectionRadar(std::shared_ptr<Core::Analysis::CollectionRadar> radar) {
    std::lock_guard<std::mutex> g(lock_);
    radar_ = std::move(radar);
}

void SmartSuggestEngine::setClapLookup(ClapLookup lookup) {
    std::lock_guard<std::mutex> g(lock_);
    clapLookup_ = std::move(lookup);
}

int SmartSuggestEngine::clapSimilarityToScore(float cosine) {
    constexpr double lo = 0.20;
    constexpr double hi = 0.80;
    const double t = std::clamp(((double) cosine - lo) / (hi - lo), 0.0, 1.0);
    return static_cast<int>(std::round(t * 100.0));
}

void SmartSuggestEngine::setVirtualDJRemote(std::shared_ptr<VirtualDJ::VirtualDJRemote> remote) {
    std::lock_guard<std::mutex> g(lock_);
    vdj_ = std::move(remote);
}

void SmartSuggestEngine::setSessionVenue(const std::string& venue) {
    std::lock_guard<std::mutex> g(lock_);
    sessionVenue_ = venue;
}

void SmartSuggestEngine::setBlacklist(std::unordered_set<int64_t> ids) {
    std::lock_guard<std::mutex> g(lock_);
    blacklist_ = std::move(ids);
}

void SmartSuggestEngine::blacklistAdd(int64_t id) {
    std::lock_guard<std::mutex> g(lock_);
    blacklist_.insert(id);
}

void SmartSuggestEngine::blacklistRemove(int64_t id) {
    std::lock_guard<std::mutex> g(lock_);
    blacklist_.erase(id);
}

void SmartSuggestEngine::setCurrentTrack(int64_t id) {
    std::lock_guard<std::mutex> g(lock_);
    currentTrackId_ = id;
}

int64_t SmartSuggestEngine::getCurrentTrack() const {
    std::lock_guard<std::mutex> g(lock_);
    return currentTrackId_;
}

void SmartSuggestEngine::setEnergyDirection(EnergyDirection d) {
    std::lock_guard<std::mutex> g(lock_);
    energyDirection_ = d;
}

void SmartSuggestEngine::setBPMRange(double minBpm, double maxBpm) {
    std::lock_guard<std::mutex> g(lock_);
    minBpm_ = minBpm;
    maxBpm_ = maxBpm;
}

void SmartSuggestEngine::setHarmonicOnly(bool on) {
    std::lock_guard<std::mutex> g(lock_);
    harmonicOnly_ = on;
}

void SmartSuggestEngine::setEnergyBoost(bool on) {
    std::lock_guard<std::mutex> g(lock_);
    energyBoost_ = on;
    if (on) energyDirection_ = EnergyDirection::Increase;
}

bool SmartSuggestEngine::getEnergyBoost() const {
    std::lock_guard<std::mutex> g(lock_);
    return energyBoost_;
}

void SmartSuggestEngine::setBPMTolerancePercent(double pct) {
    std::lock_guard<std::mutex> g(lock_);
    bpmTolerancePct_ = (pct < 0.0) ? 0.0 : pct;
}

void SmartSuggestEngine::setHarmonicMinScore(int minScore) {
    std::lock_guard<std::mutex> g(lock_);
    if (minScore == 90) minScore = 88;
    else if (minScore == 65) minScore = 60;
    harmonicMinScore_ = (minScore < 0) ? 0 : (minScore > 100 ? 100 : minScore);
}

void SmartSuggestEngine::setMashupMode(bool on) {
    std::lock_guard<std::mutex> g(lock_);
    mashupMode_ = on;
}

void SmartSuggestEngine::setSessionPhase(SessionPhase p) {
    std::lock_guard<std::mutex> g(lock_);
    sessionPhase_ = p;
}

SessionPhase SmartSuggestEngine::getSessionPhase() const {
    std::lock_guard<std::mutex> g(lock_);
    return sessionPhase_;
}

void SmartSuggestEngine::noteJustPlayed(const Models::Track& t) {
    std::lock_guard<std::mutex> g(lock_);
    if (t.energy > 0.0) {
        recentEnergies_.push_back(t.energy);
        if (recentEnergies_.size() > 6) recentEnergies_.erase(recentEnergies_.begin());
    }
    {
        std::string lg = t.genre;
        for (auto& c : lg) c = (char) std::tolower((unsigned char) c);
        recentGenres_.push_back(lg);
        if (recentGenres_.size() > 6) recentGenres_.erase(recentGenres_.begin());
    }
    std::string c = t.comment;
    for (auto& ch : c) ch = (char) std::tolower((unsigned char) ch);
    const bool isInstrumental = c.find("instrumental") != std::string::npos
                             || c.find("instru") != std::string::npos;
    if (isInstrumental) recentVocalsInARow_ = 0;
    else                recentVocalsInARow_ += 1;
}

void SmartSuggestEngine::clearRecentEnergy() {
    std::lock_guard<std::mutex> g(lock_);
    recentEnergies_.clear();
    recentGenres_.clear();
    recentVocalsInARow_ = 0;
}

SessionPhase SmartSuggestEngine::resolvePhase(SessionPhase p) {
    if (p != SessionPhase::Auto) return p;
    // Wall-clock heuristic, local time.
    auto now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    lt = *std::localtime(&now);
#endif
    int h = lt.tm_hour;
    // Warmup: 18:00–22:00 (and any daytime pre-18:00)
    if (h >= 3 && h < 6)   return SessionPhase::Cooldown;
    if (h >= 22 || h < 3)  return SessionPhase::Peak;
    return SessionPhase::Warmup;
}

int SmartSuggestEngine::scorePhaseForTrack(SessionPhase phase,
                                           double bpm, double energy)
{
    auto bell = [](double x, double mu, double sigma) {
        if (sigma <= 0.0) return 0.0;
        const double k = (x - mu) / sigma;
        return std::max(0.0, 100.0 * std::exp(-0.5 * k * k));
    };

    SessionPhase ph = resolvePhase(phase);
    double bpmFit = 50.0, energyFit = 50.0;
    switch (ph) {
        case SessionPhase::Warmup:
            bpmFit    = bell(bpm, 105.0, 15.0);  // 85–125 BPM ideal
            energyFit = bell(energy, 4.0, 2.0);  // medium energy
            break;
        case SessionPhase::Peak:
            bpmFit    = bell(bpm, 128.0, 12.0);  // 116–140 BPM
            energyFit = bell(energy, 8.0, 2.0);  // high energy
            break;
        case SessionPhase::Cooldown:
            bpmFit    = bell(bpm, 100.0, 18.0);  // 82–118 BPM
            energyFit = bell(energy, 3.0, 2.0);  // low energy
            break;
        case SessionPhase::Auto:
            return 50;
    }
    return (int) std::round(0.55 * bpmFit + 0.45 * energyFit);
}

int SmartSuggestEngine::scoreEnergyContinuity(double candEnergy,
                                               const std::vector<double>& recent) const
{
    if (candEnergy <= 0.0 || recent.size() < 2) return 70;

    const double last = recent.back();
    double sumSlope = 0.0;
    for (size_t i = 1; i < recent.size(); ++i) {
        sumSlope += (recent[i] - recent[i - 1]);
    }
    const double avgSlope = sumSlope / (double)(recent.size() - 1);
    const double projected = last + avgSlope;
    const double deviation = std::abs(candEnergy - projected);

    double s = 100.0 - (deviation / 4.0) * 100.0;
    if (s < 0.0)   s = 0.0;
    if (s > 100.0) s = 100.0;

    if (std::abs(candEnergy - last) < 1.5) s = std::max(s, 80.0);
    return (int) std::round(s);
}

int SmartSuggestEngine::scoreVocalBalance(const Models::Track& cand,
                                           int vocalsInARow) const
{
    std::string c = cand.comment;
    for (auto& ch : c) ch = (char) std::tolower((unsigned char) ch);
    const bool candIsInstrumental = c.find("instrumental") != std::string::npos
                                 || c.find("instru") != std::string::npos;

    if (vocalsInARow >= 3) return candIsInstrumental ? 100 : 40;
    if (vocalsInARow >= 2) return candIsInstrumental ? 85  : 60;
    return 70;
}

void SmartSuggestEngine::setGenreFilter(const std::string& genre) {
    std::lock_guard<std::mutex> g(lock_);
    genreFilter_ = genre;
}

void SmartSuggestEngine::setKeyOverride(const std::string& camelot) {
    std::lock_guard<std::mutex> g(lock_);
    keyOverride_ = camelot;
}

void SmartSuggestEngine::setStrictFilter(bool on) {
    std::lock_guard<std::mutex> g(lock_);
    strictFilter_ = on;
}

bool SmartSuggestEngine::getStrictFilter() const {
    std::lock_guard<std::mutex> g(lock_);
    return strictFilter_;
}

void SmartSuggestEngine::setWeights(std::array<double, 10> w) {
    std::lock_guard<std::mutex> g(lock_);
    weights_ = w;
}

void SmartSuggestEngine::setWeights(std::array<double, 6> w) {
    std::lock_guard<std::mutex> g(lock_);
    weights_[0] = w[0]; weights_[1] = w[1]; weights_[2] = w[2];
    weights_[3] = w[3]; weights_[4] = w[4]; weights_[5] = w[5];
    weights_[6] = 0.08; weights_[7] = 0.05; weights_[8] = 0.02; weights_[9] = 0.03;
}

void SmartSuggestEngine::invalidateCache() {
    std::lock_guard<std::mutex> g(lock_);
    recentlyPlayed_.clear();
    recentlyPlayedRefreshedAt_ = 0;
    spdlog::info("[SmartSuggest] cache invalidated");
}

void SmartSuggestEngine::refreshStyleModel() {
    std::lock_guard<std::mutex> g(lock_);
    lastStyleTrainUnix_ = 0;
    spdlog::info("[SmartSuggest] style model marked for retrain");
}

void SmartSuggestEngine::reportAccepted(int64_t suggestedId) {
    spdlog::info("[SmartSuggest] accepted: track={}", suggestedId);
    int64_t cur = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        cur = currentTrackId_;
    }
    if (m_style && cur > 0 && suggestedId > 0) {
        m_style->addPair(cur, suggestedId, +1);
    }
    if (m_style) {
        m_style->train();
        std::lock_guard<std::mutex> g(lock_);
        lastStyleTrainUnix_ = nowUnix();
    }
}

void SmartSuggestEngine::reportRejected(int64_t suggestedId) {
    spdlog::info("[SmartSuggest] rejected: track={}", suggestedId);
}

void SmartSuggestEngine::reportSkipped(int64_t suggestedId) {
    spdlog::info("[SmartSuggest] skipped: track={}", suggestedId);
    int64_t cur = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        cur = currentTrackId_;
    }
    if (m_style && cur > 0 && suggestedId > 0) {
        // Decrement by 1 — MAX(0, ...) clamp in addPair keeps it non-negative.
        m_style->addPair(cur, suggestedId, -1);
    }
}


SmartSuggestEngine::Settings SmartSuggestEngine::snapshot() const {
    ClapLookup clapLookup;
    std::lock_guard<std::mutex> g(lock_);
    Settings s;
    clapLookup     = clapLookup_;
    s.currentId    = currentTrackId_;
    s.dir          = energyDirection_;
    s.minBpm       = minBpm_;
    s.maxBpm       = maxBpm_;
    s.harmonicOnly = harmonicOnly_;
    s.mashupMode   = mashupMode_;
    s.strictFilter = strictFilter_;
    s.energyBoost  = energyBoost_;
    s.bpmTolerancePct  = bpmTolerancePct_;
    s.harmonicMinScore = harmonicMinScore_;
    s.w            = weights_;
    s.genreFilter  = genreFilter_;
    s.keyOverride  = keyOverride_;
    s.sessionVenue = sessionVenue_;
    s.blacklist    = blacklist_;
    s.boost        = boostSet_;
    s.phase             = sessionPhase_;
    s.recentEnergies    = recentEnergies_;
    s.recentGenres      = recentGenres_;
    s.vocalTracksInARow = recentVocalsInARow_;
    s.manualAssoc       = manualAssoc_;
    s.clapOffset        = personalClapOffset_.load();
    s.playlistsByTrack  = playlistsByTrack_;
    if (clapLookup) s.clapEmbeds = clapLookup();
    return s;
}

void SmartSuggestEngine::boostIds(const std::vector<int64_t>& ids) {
    std::lock_guard<std::mutex> g(lock_);
    boostSet_.clear();
    for (auto id : ids) if (id > 0) boostSet_.insert(id);
}

void SmartSuggestEngine::boostClear() {
    std::lock_guard<std::mutex> g(lock_);
    boostSet_.clear();
}

void SmartSuggestEngine::refreshRecentlyPlayedIfStale() {
    int64_t now = nowUnix();
    {
        std::lock_guard<std::mutex> g(lock_);
        if (now - recentlyPlayedRefreshedAt_ < kRecentlyPlayedCacheTtl
            && !recentlyPlayed_.empty()) {
            return;
        }
    }

    std::unordered_set<int64_t> fresh;
    const int64_t cutoff = now - kRecentSeconds;
    std::ostringstream q;
    q << "SELECT t.* FROM tracks t INNER JOIN play_history ph "
      << "ON ph.track_id = t.id WHERE ph.played_at > " << cutoff;
    try {
        auto rows = db_->getTracksByQuery(q.str(), {});
        for (const auto& t : rows) fresh.insert(t.id);
    } catch (const std::exception& e) {
        spdlog::warn("[SmartSuggest] recentlyPlayed refresh failed: {}", e.what());
    }

    std::lock_guard<std::mutex> g(lock_);
    recentlyPlayed_            = std::move(fresh);
    recentlyPlayedRefreshedAt_ = now;
}

void SmartSuggestEngine::ensureStyleTrained() {
    if (!m_style) return;
    int64_t last;
    {
        std::lock_guard<std::mutex> g(lock_);
        last = lastStyleTrainUnix_;
    }
    if (nowUnix() - last > 5 * 60) {
        m_style->train();
        refreshPersonalClapCalibration();
        std::lock_guard<std::mutex> g(lock_);
        lastStyleTrainUnix_ = nowUnix();
    }
}

void SmartSuggestEngine::refreshPlaylistIndexIfStale() {
    if (!db_) return;
    int64_t last;
    {
        std::lock_guard<std::mutex> g(lock_);
        last = playlistIndexRefreshedAt_;
    }
    if (nowUnix() - last <= 5 * 60) return;

    auto idx = std::make_shared<std::unordered_map<int64_t, std::vector<int64_t>>>();
    db_->executeRead(
        "SELECT track_id, playlist_id FROM playlist_tracks", {},
        [&idx](sqlite3_stmt* stmt) {
            (*idx)[sqlite3_column_int64(stmt, 0)].push_back(sqlite3_column_int64(stmt, 1));
        });
    {
        std::lock_guard<std::mutex> g(lock_);
        playlistsByTrack_ = std::move(idx);
        playlistIndexRefreshedAt_ = nowUnix();
    }
}

float SmartSuggestEngine::computePersonalClapOffset(std::vector<float> cosines) {
    if (cosines.size() < 10) return 0.0f;
    std::sort(cosines.begin(), cosines.end());
    const float median = cosines[cosines.size() / 2];
    return std::clamp(kClapBaselineCos - median, -0.10f, 0.10f);
}

void SmartSuggestEngine::refreshPersonalClapCalibration() {
    if (!db_) return;
    ClapLookup lookup;
    {
        std::lock_guard<std::mutex> g(lock_);
        lookup = clapLookup_;
    }
    if (!lookup) return;
    auto embeds = lookup();
    if (!embeds || embeds->empty()) return;

    std::vector<std::pair<int64_t, int64_t>> pairs;
    db_->executeRead(
        "SELECT track_a_id, track_b_id FROM track_pairs WHERE play_count > 0", {},
        [&pairs](sqlite3_stmt* stmt) {
            pairs.emplace_back(sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1));
        });

    std::vector<float> cosines;
    for (const auto& [a, b] : pairs) {
        auto ia = embeds->find(a);
        auto ib = embeds->find(b);
        if (ia == embeds->end() || ib == embeds->end()) continue;
        const auto& va = ia->second;
        const auto& vb = ib->second;
        if (va.size() != vb.size() || va.empty()) continue;
        double dot = 0.0;
        for (size_t i = 0; i < va.size(); ++i) dot += (double) va[i] * (double) vb[i];
        cosines.push_back((float) std::clamp(dot, -1.0, 1.0));
    }

    const float offset = computePersonalClapOffset(cosines);
    personalClapOffset_.store(offset);
    if (!cosines.empty())
        spdlog::info("[SmartSuggest] calibration perso: {} paires acceptees avec vecteurs, decalage sonorite {:+.3f}",
                     cosines.size(), offset);
}

void SmartSuggestEngine::scoreCandidate(Suggestion& s,
                                        const Models::Track& cur,
                                        const Models::Track& cand,
                                        const Settings& set,
                                        int maxPlayCount,
                                        int preferredYearCenter,
                                        int preferredYearSpread,
                                        std::int64_t now) const {
    int harmonicRaw = calcHarmonicScore(cur.camelotKey, cand.camelotKey, set.energyBoost);
    if (harmonicRaw >= 0) {
        const double confKey = std::min(normalizeConfidence(cur.keyConfidence),
                                        normalizeConfidence(cand.keyConfidence));
        harmonicRaw = static_cast<int>(std::round(50.0 + confKey * (harmonicRaw - 50.0)));
    }

    int bpmRaw = calcBPMScore(cur.bpm, cand.bpm);
    if (bpmRaw >= 0) {
        const double confBpm = std::min(normalizeConfidence(cur.bpmConfidence),
                                        normalizeConfidence(cand.bpmConfidence));
        bpmRaw = static_cast<int>(std::round(50.0 + confBpm * (bpmRaw - 50.0)));
    }

    int  energyRaw = -1;
    bool edgeEnergyUsed = false;
    {
        double outroCur = -1.0, introCand = -1.0;
        if (!cur.energySegments.empty() && !cand.energySegments.empty()) {
            outroCur  = energyAtEdge(cur.energySegments, true);
            introCand = energyAtEdge(cand.energySegments, false);
        }
        if (outroCur > 0.0 && introCand > 0.0) {
            energyRaw = calcEnergyScore(outroCur, introCand, set.dir);
            edgeEnergyUsed = (energyRaw >= 0);
            if (edgeEnergyUsed && outroCur >= 8.0 && introCand <= 3.0 && energyRaw > 25)
                energyRaw = 25;
        } else {
            energyRaw = calcEnergyScore(cur.energy, cand.energy, set.dir);
        }
    }
    s.edgeEnergyUsed = edgeEnergyUsed;

    const int styleRaw    = (m_style && cur.id > 0 && cand.id > 0)
                            ? m_style->calcStyleScore(cur.id, cand.id) : -1;
    const int structureRaw = (cur.outroStart >= 0.0 && cand.introStart >= 0.0)
        ? calcStructureScore(cur.introStart, cur.introEnd,
                             cur.outroStart, cur.outroEnd,
                             cand.introStart, cand.introEnd,
                             cand.outroStart, cand.outroEnd)
        : -1;

    s.harmonicScore  = std::max(0, harmonicRaw);
    s.bpmScore       = std::max(0, bpmRaw);
    s.energyScoreV   = std::max(0, energyRaw);
    s.styleScore     = std::max(0, styleRaw);
    s.structureScore = std::max(0, structureRaw);

    int trendingRaw = -1;
    if (maxPlayCount > 0 && cand.playCount > 0) {
        double norm = static_cast<double>(cand.playCount) / maxPlayCount;
        trendingRaw = static_cast<int>(std::round(norm * 70.0));
    }
    {
        const std::string& cm = cand.comment;
        auto p = cm.find("chart:rank=");
        if (p != std::string::npos) {
            try {
                int rank = std::stoi(cm.substr(p + 11));
                if (rank >= 1 && rank <= 100) {
                    int chartBonus = static_cast<int>(std::round((101 - rank) * 0.3));
                    if (trendingRaw < 0) trendingRaw = 0;
                    trendingRaw = std::min(100, trendingRaw + chartBonus);
                }
            } catch (...) {}
        }
    }
    s.trendingScore = std::max(0, trendingRaw);

    s.timbreSim   = 0.0f;
    int timbreRaw = -1;
    if (set.clapEmbeds) {
        auto ia = set.clapEmbeds->find(cur.id);
        auto ib = set.clapEmbeds->find(cand.id);
        if (ia != set.clapEmbeds->end() && ib != set.clapEmbeds->end()
            && ia->second.size() == ib->second.size() && !ia->second.empty()) {
            double dot = 0.0;
            const auto& va = ia->second;
            const auto& vb = ib->second;
            for (size_t i = 0; i < va.size(); ++i)
                dot += (double) va[i] * (double) vb[i];
            const float cosSim = (float) std::clamp(dot, -1.0, 1.0);
            s.timbreSim = std::max(0.0f, cosSim);
            timbreRaw   = clapSimilarityToScore(cosSim + set.clapOffset);
        }
    }
    if (timbreRaw < 0) {
        std::shared_ptr<Core::Analysis::CollectionRadar> radar;
        {
            std::lock_guard<std::mutex> g(lock_);
            radar = radar_;
        }
        if (radar) {
            float sim = radar->timbreSimilarity(cur.id, cand.id);
            if (sim >= 0.0f) {
                s.timbreSim = sim;
                timbreRaw   = static_cast<int>(std::round(sim * 100.0f));
            }
        }
    }
    s.timbreScore = std::max(0, timbreRaw);

    const int eraRaw = calcEraScore(cand.year, preferredYearCenter, preferredYearSpread);
    s.eraScore = std::max(0, eraRaw);

    int venueRaw = -1;
    if (!set.sessionVenue.empty() && !cand.venue.empty()) {
        venueRaw = (toLowerCopy(cand.venue) == toLowerCopy(set.sessionVenue)) ? 100 : 35;
    }
    s.venueScore = std::max(0, venueRaw);

    const int genreGraphRaw = calcGenreGraphScore(cur.genre, cand.genre);
    s.genreGraphScore = std::max(0, genreGraphRaw);

    int lufsRaw = -1;
    s.lufsKnown = false;
    s.lufsDelta = 0.0;
    if (cur.lufs != 0.0f && cand.lufs != 0.0f) {
        const double dLufs = std::abs(static_cast<double>(cand.lufs) -
                                      static_cast<double>(cur.lufs));
        lufsRaw = static_cast<int>(std::round(
            100.0 - std::min(100.0, dLufs / 6.0 * 100.0)));
        s.lufsKnown = true;
        s.lufsDelta = static_cast<double>(cand.lufs) - static_cast<double>(cur.lufs);
    }
    s.lufsScore = std::max(0, lufsRaw);

    s.phaseScore        = scorePhaseForTrack(set.phase, cand.bpm, cand.energy);
    s.continuityScore   = scoreEnergyContinuity(cand.energy, set.recentEnergies);
    s.vocalBalanceScore = scoreVocalBalance(cand, set.vocalTracksInARow);

    {
        const int h  = s.harmonicScore;
        const int b  = s.bpmScore;
        const bool harmonicKnown = (harmonicRaw >= 0);
        const bool bpmKnown      = (bpmRaw      >= 0);
        auto loweredComment = [](const Models::Track& t) {
            std::string c = t.comment;
            for (auto& ch : c) ch = (char) std::tolower((unsigned char) ch);
            return c;
        };
        const auto cA = loweredComment(cur);
        const auto cB = loweredComment(cand);
        auto isInst = [](const std::string& c) {
            return c.find("instrumental") != std::string::npos
                || c.find("instru")       != std::string::npos
                || c.find("no vocal")     != std::string::npos;
        };
        const bool aInst = isInst(cA);
        const bool bInst = isInst(cB);
        int vocInstScore = 50;
        if (aInst != bInst)      vocInstScore = 100; // perfect: one vocal, one instru
        else if (aInst && bInst) vocInstScore = 35;  // two instrus = stacked layers
        else                     vocInstScore = 45;  // two vocals = clash

        if (harmonicKnown && bpmKnown) {
            s.mashupScore = (int) std::round(0.40 * h + 0.35 * b + 0.25 * vocInstScore);
        } else if (!harmonicKnown && bpmKnown) {
            const int g = s.genreGraphScore;
            const int e = s.energyScoreV;
            s.mashupScore = (int) std::round(0.50 * b + 0.30 * e + 0.20 * g * 0.6
                                             + 0.10 * vocInstScore);
        } else if (harmonicKnown && !bpmKnown) {
            s.mashupScore = (int) std::round(0.60 * h + 0.25 * vocInstScore + 0.15 * 50);
        } else {
            const int g = s.genreGraphScore;
            s.mashupScore = (int) std::round(0.55 * vocInstScore + 0.45 * g);
        }
        if (s.mashupScore < 0)   s.mashupScore = 0;
        if (s.mashupScore > 100) s.mashupScore = 100;
    }

    s.camelotDelta = wheelDistance(cur.camelotKey, cand.camelotKey);
    s.bpmDelta     = cand.bpm - cur.bpm;
    s.daysSincePlayed = -1;
    if (cand.lastPlayed > 0) {
        int64_t diffSec = now - cand.lastPlayed;
        if (diffSec < 0) diffSec = 0;
        s.daysSincePlayed = static_cast<int>(diffSec / 86400);
    }

    const bool harmonicKnown = (harmonicRaw >= 0);
    const bool bpmKnown      = (bpmRaw      >= 0);
    const bool energyKnown   = (energyRaw   >= 0);
    const bool styleKnown    = (styleRaw    >= 0);
    const bool timbreKnown   = (timbreRaw   >= 0);
    const bool genreKnown    = (genreGraphRaw >= 0);
    const bool eraKnown      = (eraRaw      >= 0);
    const bool trendingKnown = (trendingRaw >= 0);
    const bool venueKnown    = (venueRaw    >= 0);
    const bool structureKnown= (structureRaw>= 0);
    const bool lufsSignal    = (lufsRaw     >= 0);

    double wH = harmonicKnown  ? set.w[0] : 0.0;
    double wB = bpmKnown       ? set.w[1] : 0.0;
    double wE = energyKnown    ? set.w[2] : 0.0;
    double wS = styleKnown     ? set.w[3] : 0.0;
    double wP = trendingKnown  ? set.w[4] : 0.0;
    double wX = structureKnown ? set.w[5] : 0.0;
    double wT = timbreKnown    ? set.w[6] : 0.0;
    double wY = eraKnown       ? set.w[7] : 0.0;
    double wV = venueKnown     ? set.w[8] : 0.0;
    double wG = genreKnown     ? set.w[9] : 0.0;
    const double wL = lufsSignal ? 0.05 : 0.0;

    double missingHard = 0.0;
    if (!harmonicKnown) missingHard += set.w[0];
    if (!bpmKnown)      missingHard += set.w[1];
    if (missingHard > 0.0) {
        const double fallbackBase = wS + wT + wG;
        if (fallbackBase > 0.0) {
            wS += missingHard * (wS / fallbackBase);
            wT += missingHard * (wT / fallbackBase);
            wG += missingHard * (wG / fallbackBase);
        }
    }

    double weightedSum = 0.0;
    double wTotal      = 0.0;
    auto accum = [&](double w, int raw) {
        if (w > 0.0 && raw >= 0) { weightedSum += w * raw; wTotal += w; }
    };
    accum(wH, harmonicRaw);
    accum(wB, bpmRaw);
    accum(wE, energyRaw);
    accum(wS, styleRaw);
    accum(wP, trendingRaw);
    accum(wX, structureRaw);
    accum(wT, timbreRaw);
    accum(wY, eraRaw);
    accum(wV, venueRaw);
    accum(wG, genreGraphRaw);
    accum(wL, lufsRaw);

    // If no signals applied at all, emit a low score (rather than 50).
    double blended = (wTotal > 1e-6) ? (weightedSum / wTotal) : 25.0;

    const double phaseBonus      = (s.phaseScore        - 50) * 0.10;
    const double continuityBonus = (s.continuityScore   - 50) * 0.06;
    const double vocalBonus      = (s.vocalBalanceScore - 50) * 0.04;

    double tieBreak = 0.0;
    if (cand.rating > 0)      tieBreak += (cand.rating - 3) * 0.8;
    if (cand.playCount > 0)   tieBreak += std::min(1.6,
                                  std::log10((double) cand.playCount + 1.0) * 0.8);
    if (cand.lastPlayed == 0) tieBreak += 0.8;
    // Stable per-track jitter so the list is reproducible between refreshes.
    uint64_t h = (uint64_t) cand.id;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    tieBreak += ((int) (h % 21) - 10) * 0.04;

    double rediscoverBonus = 0.0;
    if (cand.playCount <= 0) {
        rediscoverBonus = 3.0;
    } else {
        double rare = 1.0;
        if (maxPlayCount > 0)
            rare = 1.0 - std::min(1.0, std::log10((double) cand.playCount + 1.0)
                                       / std::log10((double) maxPlayCount + 1.0));
        double stale = 0.0;
        if (cand.lastPlayed > 0 && now > cand.lastPlayed)
            stale = std::min(1.0, (double) (now - cand.lastPlayed) / (86400.0 * 180.0));
        rediscoverBonus = 3.5 * (0.5 * rare + 0.5 * stale);
    }

    double comboBonus = 0.0;
    if (harmonicKnown && bpmKnown && s.harmonicScore >= 70 && s.bpmScore >= 70) {
        const double hN = std::min(1.0, (s.harmonicScore - 70) / 30.0);
        const double bN = std::min(1.0, (s.bpmScore - 70) / 30.0);
        comboBonus = 10.0 * hN * bN;
    }

    double genreBonus = 0.0;
    if (!cur.genre.empty() && !cand.genre.empty()) {
        if (toLowerCopy(trimCopy(cur.genre)) == toLowerCopy(trimCopy(cand.genre))) {
            genreBonus = 6.0;
            if (timbreKnown && s.timbreScore >= 60)
                genreBonus += 4.0;
        } else if (genreKnown && genreGraphRaw >= 70) {
            genreBonus = 3.0;
        }
    }

    double playlistBonus = 0.0;
    s.sharedPlaylist = false;
    if (set.playlistsByTrack && cur.id > 0 && cand.id > 0) {
        auto ia = set.playlistsByTrack->find(cur.id);
        auto ib = set.playlistsByTrack->find(cand.id);
        if (ia != set.playlistsByTrack->end() && ib != set.playlistsByTrack->end()) {
            for (int64_t p : ia->second) {
                if (std::find(ib->second.begin(), ib->second.end(), p) != ib->second.end()) {
                    s.sharedPlaylist = true;
                    playlistBonus = 4.0;
                    break;
                }
            }
        }
    }

    double finalScore = blended + phaseBonus + continuityBonus + vocalBonus
                      + tieBreak + rediscoverBonus + comboBonus + genreBonus
                      + playlistBonus;

    s.manualPair = false;
    if (!set.manualAssoc.empty() && cur.id > 0 && cand.id > 0) {
        auto it = set.manualAssoc.find(cur.id);
        if (it != set.manualAssoc.end() && it->second.count(cand.id) > 0) {
            s.manualPair = true;
            finalScore += 22.0;
        }
    }

    int total = static_cast<int>(std::round(finalScore));
    if (total < 0)   total = 0;
    if (total > 100) total = 100;

    const bool sameRecording =
        !cand.title.empty() && !cur.title.empty()
        && toLowerCopy(trimCopy(cand.title))  == toLowerCopy(trimCopy(cur.title))
        && toLowerCopy(trimCopy(cand.artist)) == toLowerCopy(trimCopy(cur.artist));
    if (!sameRecording && total > 98) total = 98;

    s.totalScore = total;
    s.level      = levelFor(total);
}

std::string SmartSuggestEngine::buildReason(const Suggestion& s,
                                            const Models::Track& cur,
                                            bool energyBoost) {
    std::ostringstream r;
    auto sign = [](double v) { return v >= 0.0 ? std::string("+") : std::string(""); };

    const bool keysKnown = !cur.camelotKey.empty() && !s.camelotKey.empty();

    if (keysKnown) {
        r << "Cl\xc3\xa9 " << cur.camelotKey << "\xe2\x86\x92" << s.camelotKey;
        const auto rel = harmonicRelationDirectional(cur.camelotKey, s.camelotKey,
                                                     energyBoost);
        if (rel.name != nullptr) r << " (" << rel.name << ")";
    } else {
        r << "Cl\xc3\xa9 " << sign(static_cast<double>(s.camelotDelta)) << s.camelotDelta;
    }

    {
        std::ostringstream b;
        b << ", BPM " << sign(s.bpmDelta);
        b.precision(1);
        b << std::fixed << s.bpmDelta;
        r << b.str();
    }

    if (cur.energy > 0.0 && s.energyScore > 0.0) {
        const double ed = s.energyScore - cur.energy;
        std::ostringstream e;
        e << ", \xc3\x89nergie " << sign(ed);
        e.precision((std::abs(ed) < 0.05) ? 0 : 1);
        e << std::fixed << ed;
        r << e.str();
    } else if (s.energyScoreV >= 85) {
        r << ", \xc3\x89nergie OK";
    }

    if (energyBoost && cur.energy > 0.0 && s.energyScore > cur.energy)
        r << ", Boost \xc3\xa9nergie";

    if (s.edgeEnergyUsed && s.energyScoreV >= 55)
        r << ", outro\xe2\x86\x92intro OK";

    if (s.lufsKnown && std::abs(s.lufsDelta) >= 1.0) {
        std::ostringstream l;
        l.precision(1);
        l << std::fixed << ", \xce\x94LUFS " << sign(s.lufsDelta) << s.lufsDelta << " dB";
        r << l.str();
    }

    if (s.timbreSim > 0.0f && s.timbreScore > 0)
        r << ", Sonorit\xc3\xa9 IA " << s.timbreScore << " %";

    if (!cur.genre.empty() && !s.genre.empty()
        && toLowerCopy(trimCopy(cur.genre)) == toLowerCopy(trimCopy(s.genre)))
        r << ", M\xc3\xaame genre (" << trimCopy(s.genre) << ")";

    if (s.sharedPlaylist)
        r << ", M\xc3\xaame playlist";

    {
        std::ostringstream t;
        t.precision(2);
        t << std::fixed << ", Style " << (static_cast<double>(s.styleScore) / 100.0);
        r << t.str();
    }

    if (s.trendingScore >= 60) r << ", Tendance";

    if (s.daysSincePlayed < 0)            r << ", Jamais jou\xc3\xa9""e";
    else if (s.daysSincePlayed == 0)      r << ", Jou\xc3\xa9""e aujourd'hui";
    else                                  r << ", \xc3\x89""cout\xc3\xa9""e il y a " << s.daysSincePlayed << "j";

    return r.str();
}

std::vector<Suggestion>
SmartSuggestEngine::diversify(std::vector<Suggestion> ranked, int maxResults) const {
    if (maxResults <= 0 || ranked.empty()) return ranked;

    // Target: >=3 distinct artists, >=2 distinct genres in the final top-N.
    std::vector<Suggestion> out;
    out.reserve(static_cast<size_t>(maxResults));

    std::unordered_map<std::string, int> artistCount;

    const int artistCap = 2;

    for (const auto& s : ranked) {
        if (static_cast<int>(out.size()) >= maxResults) break;
        const std::string a = toLowerCopy(s.artist);
        if (!a.empty() && artistCount[a] >= artistCap) continue;
        out.push_back(s);
        if (!a.empty()) ++artistCount[a];
    }

    if (static_cast<int>(out.size()) < maxResults) {
        std::unordered_set<int64_t> chosen;
        for (const auto& s : out) chosen.insert(s.trackId);
        for (const auto& s : ranked) {
            if (static_cast<int>(out.size()) >= maxResults) break;
            if (chosen.count(s.trackId)) continue;
            out.push_back(s);
        }
    }

    auto countDistinct = [&](auto pred) {
        std::unordered_set<std::string> set;
        for (const auto& s : out) {
            std::string v = pred(s);
            if (!v.empty()) set.insert(toLowerCopy(v));
        }
        return static_cast<int>(set.size());
    };

    int distinctArtists = countDistinct([](const Suggestion& s){ return s.artist; });
    int distinctGenres  = countDistinct([](const Suggestion& s){ return s.genre;  });

    if ((distinctArtists < 3 || distinctGenres < 2) && ranked.size() > out.size()) {
        std::unordered_set<int64_t> inOut;
        for (const auto& s : out) inOut.insert(s.trackId);

        for (auto it = ranked.rbegin(); it != ranked.rend(); ++it) {
            if (inOut.count(it->trackId)) continue;
            bool addsArtist = distinctArtists < 3
                && std::none_of(out.begin(), out.end(),
                                [&](const Suggestion& s){ return toLowerCopy(s.artist) == toLowerCopy(it->artist); });
            bool addsGenre  = distinctGenres < 2
                && std::none_of(out.begin(), out.end(),
                                [&](const Suggestion& s){ return toLowerCopy(s.genre) == toLowerCopy(it->genre); });
            if (!addsArtist && !addsGenre) continue;

            auto worst = std::min_element(out.begin(), out.end(),
                [](const Suggestion& a, const Suggestion& b){ return a.totalScore < b.totalScore; });
            if (worst == out.end()) break;
            inOut.erase(worst->trackId);
            *worst = *it;
            inOut.insert(it->trackId);

            distinctArtists = countDistinct([](const Suggestion& s){ return s.artist; });
            distinctGenres  = countDistinct([](const Suggestion& s){ return s.genre;  });
            if (distinctArtists >= 3 && distinctGenres >= 2) break;
        }
    }

    return out;
}

std::vector<Suggestion>
SmartSuggestEngine::pickWildCard(const std::vector<Suggestion>& poolAll,
                                 const std::unordered_set<int64_t>& chosen) const {
    std::vector<Suggestion> result;
    // Wild card: low trending + good harmonic (>=70) + unusual genre vs. chosen.
    std::unordered_set<std::string> chosenGenres;
    for (const auto& s : poolAll) {
        if (chosen.count(s.trackId)) {
            if (!s.genre.empty()) chosenGenres.insert(toLowerCopy(s.genre));
        }
    }

    const Suggestion* best = nullptr;
    double bestScore = -1.0;
    for (const auto& s : poolAll) {
        if (chosen.count(s.trackId)) continue;
        const bool hasKey = !s.camelotKey.empty();
        if (hasKey && s.harmonicScore < 70) continue;

        double unusualGenreBonus =
            (!s.genre.empty() && chosenGenres.count(toLowerCopy(s.genre)) == 0) ? 25.0 : 0.0;
        double rarityBonus = 25.0 - std::min(25.0, static_cast<double>(s.trendingScore) / 4.0);
        double harmonicPart = hasKey
            ? static_cast<double>(s.harmonicScore) * 0.5
            : static_cast<double>(s.bpmScore) * 0.5;
        double styleNotBoring = (s.styleScore < 60) ? 10.0 : 0.0;
        double sc = harmonicPart + rarityBonus + unusualGenreBonus + styleNotBoring;
        if (sc > bestScore) {
            bestScore = sc;
            best = &s;
        }
    }
    if (best) {
        Suggestion w = *best;
        w.isWildCard = true;
        w.reason = std::string("Wild card - ") + w.reason;
        result.push_back(std::move(w));
    }
    return result;
}

std::vector<Suggestion> SmartSuggestEngine::getSuggestions(int maxResults) {
    if (!db_) return {};

    Settings set = snapshot();
    if (set.currentId <= 0) return {};

    ensureStyleTrained();
    refreshRecentlyPlayedIfStale();
    refreshPlaylistIndexIfStale();
    if (!set.playlistsByTrack) {
        std::lock_guard<std::mutex> g(lock_);
        set.playlistsByTrack = playlistsByTrack_;
    }

    auto currentOpt = db_->getTrack(set.currentId);
    if (!currentOpt) return {};
    const auto& cur = *currentOpt;

    int preferredYearCenter = 0;
    int preferredYearSpread = 5;
    if (m_style) {
        auto years = m_style->topYears(5);
        if (!years.empty()) {
            long long sum = 0; int n = 0;
            for (const auto& p : years) { sum += p.first; ++n; }
            if (n > 0) preferredYearCenter = static_cast<int>(sum / n);

            int minY = years.front().first, maxY = years.front().first;
            for (const auto& p : years) {
                if (p.first < minY) minY = p.first;
                if (p.first > maxY) maxY = p.first;
            }
            preferredYearSpread = std::max(3, (maxY - minY) / 2);
        }
    }

    auto all = db_->getAllTracks();
    spdlog::info("[SmartSuggest] getSuggestions: {} candidate tracks (cur={}, genreFilter='{}')",
                 all.size(), set.currentId, set.genreFilter);

    int maxPlayCount = 0;
    for (const auto& t : all)
        if (t.playCount > maxPlayCount) maxPlayCount = t.playCount;

    std::unordered_set<int64_t> recentCache;
    {
        std::lock_guard<std::mutex> g(lock_);
        recentCache = recentlyPlayed_;
    }

    const std::string genreNeedle = toLowerCopy(set.genreFilter);
    const int64_t now = nowUnix();

    std::vector<Suggestion> scored;
    scored.reserve(all.size());
    int rejectedNotAnalyzed = 0, rejectedBpmIncompat = 0,
        rejectedGenre = 0, rejectedRecent = 0, rejectedKeyOverride = 0;

    const double curBpm = cur.bpm;
    const std::string curTitleKey = toLowerCopy(trimCopy(cur.title));
    for (const auto& cand : all) {
        if (cand.id == cur.id) continue;
        if (!curTitleKey.empty() && toLowerCopy(trimCopy(cand.title)) == curTitleKey) continue;
        // Directive utilisateur : tout candidat entre dans le pool de scoring
        if (recentCache.count(cand.id)) { ++rejectedRecent; continue; }
        if (set.blacklist.count(cand.id)) continue;

        // Rejet BPM seulement au-dela de 35 % (tempo immixable)
        if (curBpm > 0.0 && cand.bpm > 0.0) {
            double r1 = cand.bpm / curBpm;
            double r2 = (cand.bpm * 2.0) / curBpm;
            double r3 = cand.bpm / (curBpm * 2.0);
            double best = std::min({ std::abs(r1 - 1.0),
                                      std::abs(r2 - 1.0),
                                      std::abs(r3 - 1.0) });
            if (best * 100.0 > 35.0) { ++rejectedBpmIncompat; continue; }
        }

        // Plage BPM saisie = plage stricte (pas de demi/double tempo)
        const bool bpmFilterActive =
            (set.minBpm > 0.0 && set.minBpm < 250.0) ||
            (set.maxBpm > 0.0 && set.maxBpm < 250.0);
        if (bpmFilterActive) {
            const double lo = (set.minBpm > 0.0) ? set.minBpm : 0.0;
            const double hi = (set.maxBpm > 0.0 && set.maxBpm < 250.0) ? set.maxBpm : 250.0;
            if (cand.bpm < lo || cand.bpm > hi) {
                ++rejectedBpmIncompat; continue;
            }
        }

        // Tolerance BPM relative facon Related-Tracks Rekordbox
        if (set.bpmTolerancePct > 0.0 && curBpm > 0.0 && cand.bpm > 0.0) {
            const double r1 = cand.bpm / curBpm;
            const double r2 = (cand.bpm * 2.0) / curBpm;
            const double r3 = cand.bpm / (curBpm * 2.0);
            const double best = std::min({ std::abs(r1 - 1.0),
                                           std::abs(r2 - 1.0),
                                           std::abs(r3 - 1.0) });
            if (best * 100.0 > set.bpmTolerancePct) {
                ++rejectedBpmIncompat; continue;
            }
        }

        // Exigence Camelot facon Related-Tracks Rekordbox
        if (set.harmonicMinScore > 0
            && !cur.camelotKey.empty() && !cand.camelotKey.empty()) {
            const int hs = calcHarmonicScore(cur.camelotKey, cand.camelotKey,
                                             set.energyBoost);
            if (hs >= 0 && hs < set.harmonicMinScore) {
                ++rejectedKeyOverride; continue;
            }
        }

        int genrePenalty = 0;
        if (!genreNeedle.empty()) {
            if (cand.genre.empty()) {
                if (set.strictFilter) { ++rejectedGenre; continue; }
                genrePenalty = 15;
            } else if (!genreMatches(set.genreFilter, cand.genre)) {
                ++rejectedGenre; continue;
            }
        }

        int keyOverridePenalty = 0;
        if (!set.keyOverride.empty()) {
            if (cand.camelotKey.empty()) {
                if (set.strictFilter) { ++rejectedKeyOverride; continue; }
                keyOverridePenalty = 10;
            } else {
                int hsOverride = calcHarmonicScore(set.keyOverride, cand.camelotKey);
                const int cutoff = set.strictFilter ? 80 : 60;
                if (hsOverride < cutoff) { ++rejectedKeyOverride; continue; }
            }
        }

        Suggestion s;
        s.trackId     = cand.id;
        s.filePath    = cand.filePath;
        s.title       = cand.title;
        s.artist      = cand.artist;
        s.genre       = cand.genre;
        s.camelotKey  = cand.camelotKey;
        s.bpm         = cand.bpm;
        s.energyScore = cand.energy;
        s.year        = cand.year;

        scoreCandidate(s, cur, cand, set, maxPlayCount,
                       preferredYearCenter, preferredYearSpread, now);

        // Mode harmonique : penalite au lieu d'un rejet (evite l'impasse UX)
        if (set.harmonicOnly
            && !cur.camelotKey.empty()
            && !cand.camelotKey.empty()
            && s.harmonicScore < 60) {
            int penalty = 60 - s.harmonicScore;
            s.totalScore = std::max(0, s.totalScore - penalty / 3);
        }

        if (genrePenalty > 0 || keyOverridePenalty > 0) {
            s.totalScore = std::max(0, s.totalScore - genrePenalty - keyOverridePenalty);
        }

        // Energy Boost : bonus si le candidat monte l'energie
        if (set.energyBoost
            && cur.energy > 0.0 && cand.energy > 0.0
            && cand.energy > cur.energy) {
            const double climb = cand.energy - cur.energy;
            int bonus = (int) std::round(std::min(climb, 3.0) * 3.0);
            if (s.harmonicScore >= 65) bonus += 4;
            s.totalScore = std::min(100, s.totalScore + std::min(bonus, 12));
        }

        s.reason = buildReason(s, cur, set.energyBoost);
        {
            TransitionPlan tp = buildTransitionPlan(cur, cand);
            s.transitionLabel  = tp.label;
            s.transitionDetail = tp.detail;
            s.mixBars          = tp.bars;
        }
        if (s.manualPair) {
            s.reason = std::string("\xf0\x9f\x94\x97 Association manuelle - ") + s.reason;
        }
        if (!set.boost.empty() && set.boost.count(s.trackId)) {
            s.totalScore = std::min(100, s.totalScore + 20);
        }
        scored.push_back(std::move(s));
    }

    spdlog::info("[SmartSuggest] stage1: scored={} strict={} rej(noBpm={}, recent={}, bpmIncompat={}, "
                 "genre={}, keyOverride={})",
                 scored.size(), set.strictFilter ? "yes" : "no",
                 rejectedNotAnalyzed, rejectedRecent,
                 rejectedBpmIncompat, rejectedGenre, rejectedKeyOverride);

    if (set.mashupMode) {
        std::sort(scored.begin(), scored.end(),
                  [](const Suggestion& a, const Suggestion& b) {
                      if (a.mashupScore != b.mashupScore)
                          return a.mashupScore > b.mashupScore;
                      return a.totalScore > b.totalScore;
                  });
    } else {
        std::sort(scored.begin(), scored.end(),
                  [](const Suggestion& a, const Suggestion& b) {
                      return a.totalScore > b.totalScore;
                  });
    }

    {
        std::unordered_set<std::string> seenTitles;
        std::vector<Suggestion> unique;
        unique.reserve(scored.size());
        for (auto& s : scored) {
            const std::string key = toLowerCopy(trimCopy(s.title));
            if (!key.empty() && !seenTitles.insert(key).second) continue;
            unique.push_back(std::move(s));
        }
        scored = std::move(unique);
    }

    const int wildSlot = (maxResults >= 3) ? 1 : 0;
    int coreN = maxResults - wildSlot;
    if (coreN < 1) coreN = maxResults;

    std::vector<Suggestion> finalList = diversify(scored, coreN);

    if (wildSlot > 0) {
        std::unordered_set<int64_t> chosen;
        for (const auto& s : finalList) chosen.insert(s.trackId);
        auto wild = pickWildCard(scored, chosen);
        for (auto& w : wild) finalList.push_back(std::move(w));
    }

    if (static_cast<int>(finalList.size()) > maxResults)
        finalList.resize(static_cast<size_t>(maxResults));

    return finalList;
}

std::vector<Suggestion>
SmartSuggestEngine::getSuggestionsForDeck(int deckNum, int maxResults) {
    std::shared_ptr<VirtualDJ::VirtualDJRemote> remote;
    {
        std::lock_guard<std::mutex> g(lock_);
        remote = vdj_;
    }

    if (!remote || !remote->isConnected()) {
        // No remote — mimic default behaviour, tag deck.
        auto list = getSuggestions(maxResults);
        for (auto& s : list) s.deck = deckNum;
        return list;
    }

    auto decks = remote->getDecks();
    VirtualDJ::DeckInfo thisDeck;
    VirtualDJ::DeckInfo otherDeck;
    for (const auto& d : decks) {
        if (d.deckNumber == deckNum)       thisDeck  = d;
        else if (otherDeck.deckNumber == 0) otherDeck = d;
    }

    int64_t myTrackId     = 0;
    int64_t otherTrackId  = 0;
    if (db_) {
        if (!thisDeck.filePath.empty()) {
            auto t = db_->getTrackByPath(thisDeck.filePath);
            if (t) myTrackId = t->id;
        }
        if (!otherDeck.filePath.empty()) {
            auto t = db_->getTrackByPath(otherDeck.filePath);
            if (t) otherTrackId = t->id;
        }
    }

    // Override temporaire de currentTrackId_ pour ce scoring, restaure ensuite
    std::lock_guard<std::mutex> deckGuard(deckSerialMutex_);

    int64_t savedCurrent = 0;
    std::unordered_set<int64_t> savedBlacklist;
    {
        std::lock_guard<std::mutex> g(lock_);
        savedCurrent = currentTrackId_;
        savedBlacklist = blacklist_;
        if (myTrackId > 0) currentTrackId_ = myTrackId;
        if (otherTrackId > 0) blacklist_.insert(otherTrackId);
    }

    auto list = getSuggestions(maxResults);

    {
        std::lock_guard<std::mutex> g(lock_);
        currentTrackId_ = savedCurrent;
        blacklist_      = std::move(savedBlacklist);
    }

    for (auto& s : list) s.deck = deckNum;
    return list;
}

std::string SmartSuggestEngine::explainSuggestion(int64_t candidateTrackId) {
    if (!db_ || candidateTrackId <= 0) return {};
    Settings set = snapshot();
    if (set.currentId <= 0) return "Aucune piste courante.";

    auto curOpt  = db_->getTrack(set.currentId);
    auto candOpt = db_->getTrack(candidateTrackId);
    if (!curOpt || !candOpt) return "Piste introuvable.";

    ensureStyleTrained();

    int preferredYearCenter = 0;
    int preferredYearSpread = 5;
    if (m_style) {
        auto years = m_style->topYears(5);
        if (!years.empty()) {
            long long sum = 0; int n = 0;
            for (const auto& p : years) { sum += p.first; ++n; }
            if (n > 0) preferredYearCenter = static_cast<int>(sum / n);
        }
    }

    int maxPlayCount = 0;
    auto all = db_->getAllTracks();
    for (const auto& t : all) if (t.playCount > maxPlayCount) maxPlayCount = t.playCount;

    Suggestion s;
    s.trackId    = candOpt->id;
    s.artist     = candOpt->artist;
    s.genre      = candOpt->genre;
    s.camelotKey = candOpt->camelotKey;
    s.bpm        = candOpt->bpm;
    s.year       = candOpt->year;
    scoreCandidate(s, *curOpt, *candOpt, set, maxPlayCount,
                   preferredYearCenter, preferredYearSpread, nowUnix());

    std::ostringstream r;
    r << "=== Explication suggestion ===\n";
    r << "Candidat : " << (candOpt->artist.empty() ? "?" : candOpt->artist)
      << " - " << (candOpt->title.empty() ? "?" : candOpt->title) << "\n";
    r << "Score total : " << s.totalScore << "/100\n";
    r << "  Harmonique  : " << s.harmonicScore    << "/100"
      << " (" << curOpt->camelotKey << " -> " << candOpt->camelotKey
      << ", delta=" << s.camelotDelta << ")\n";
    r << "  BPM         : " << s.bpmScore         << "/100"
      << " (" << curOpt->bpm << " -> " << candOpt->bpm << ")\n";
    r << "  Energie     : " << s.energyScoreV     << "/100";
    if (s.edgeEnergyUsed) r << " (outro->intro via segments)";
    r << "\n";
    r << "  LUFS        : " << s.lufsScore        << "/100";
    if (s.lufsKnown) r << " (delta=" << s.lufsDelta << " dB)";
    r << "\n";
    r << "  MyStyle     : " << s.styleScore       << "/100\n";
    r << "  Trending    : " << s.trendingScore    << "/100\n";
    r << "  Structure   : " << s.structureScore   << "/100\n";
    r << "  Timbre      : " << s.timbreScore      << "/100"
      << " (cosine=" << s.timbreSim << ")\n";
    r << "  Epoque      : " << s.eraScore         << "/100"
      << " (annee=" << candOpt->year << ", cible=" << preferredYearCenter << ")\n";
    r << "  Contexte    : " << s.venueScore       << "/100"
      << " (venue=" << (candOpt->venue.empty() ? "?" : candOpt->venue)
      << ", session=" << (set.sessionVenue.empty() ? "?" : set.sessionVenue) << ")\n";
    r << "  Genre graph : " << s.genreGraphScore  << "/100"
      << " (" << (curOpt->genre.empty() ? "?" : curOpt->genre)
      << " -> " << (candOpt->genre.empty() ? "?" : candOpt->genre) << ")\n";

    TransitionPlan tp = buildTransitionPlan(*curOpt, *candOpt);
    r << "  Transition  : " << tp.label;
    if (tp.bars > 0) r << " (" << tp.bars << " mesures)";
    r << "\n";
    if (!tp.detail.empty()) r << "                " << tp.detail << "\n";

    PhraseInfo ph = computePhrases(*curOpt);
    if (ph.valid) {
        r << "  Phrases     : " << ph.barsTotal << " mesures, frontieres tous les "
          << ph.phraseLengthBars << " temps forts\n";
        if (ph.mixOutPointSec >= 0.0)
            r << "                Point de mix sortie ~ "
              << (int) std::round(ph.mixOutPointSec) << " s\n";
    }
    return r.str();
}

TransitionPlan SmartSuggestEngine::buildTransitionPlan(const Models::Track& from,
                                                       const Models::Track& to) {
    TransitionPlan tp;

    const int harm = calcHarmonicScore(from.camelotKey, to.camelotKey);
    const double eFrom = from.energy;
    const double eTo   = to.energy;
    const double eDelta = (eFrom > 0.0 && eTo > 0.0) ? (eTo - eFrom) : 0.0;
    const bool energyKnown = (eFrom > 0.0 && eTo > 0.0);

    double bpmPct = -1.0;
    if (from.bpm > 0.0 && to.bpm > 0.0) {
        const double r1 = to.bpm / from.bpm;
        const double r2 = (to.bpm * 2.0) / from.bpm;
        const double r3 = to.bpm / (from.bpm * 2.0);
        bpmPct = std::min({ std::abs(r1 - 1.0), std::abs(r2 - 1.0),
                            std::abs(r3 - 1.0) }) * 100.0;
    }

    const bool harmGood = (harm >= 80);
    const bool harmOk   = (harm >= 55);
    const bool bpmTight = (bpmPct >= 0.0 && bpmPct <= 2.0);
    const bool bpmClose = (bpmPct >= 0.0 && bpmPct <= 5.0);
    const bool bpmFar   = (bpmPct > 8.0);

    auto isInst = [](const Models::Track& t) {
        std::string c = t.comment;
        for (auto& ch : c) ch = (char) std::tolower((unsigned char) ch);
        return c.find("instrumental") != std::string::npos
            || c.find("no vocal") != std::string::npos;
    };

    if (bpmFar || (!bpmClose && bpmPct >= 0.0)) {
        tp.type = TransitionType::EchoOut;
        tp.bars = 4;
        tp.label = "Echo / coupe";
        tp.detail = "\xc3\x89""cart de tempo trop large pour un fondu beatmatch\xc3\xa9 - "
                    "couper sur un temps fort ou sortir en \xc3\xa9""cho.";
    } else if (energyKnown && eDelta <= -1.5 && bpmClose) {
        tp.type = TransitionType::LongBlend;
        tp.bars = 32;
        tp.label = "Fondu long";
        tp.detail = "Baisse d'\xc3\xa9nergie : laisser respirer sur un long fondu (\xe2\x89\x88""32 mesures).";
    } else if (harmGood && bpmTight && (!energyKnown || std::abs(eDelta) <= 1.0)) {
        tp.type = TransitionType::BassSwap;
        tp.bars = 16;
        tp.label = "Bass swap";
        tp.detail = "Cl\xc3\xa9 et tempo align\xc3\xa9s : \xc3\xa9""changer les basses (couper les graves "
                    "de la sortante, monter ceux de l'entrante) sur 16 mesures.";
    } else if (harmGood && bpmClose && energyKnown && eDelta >= 1.0) {
        tp.type = TransitionType::FilterFade;
        tp.bars = 16;
        tp.label = "Filtre montant";
        tp.detail = "\xc3\x89nergie montante : ouvrir un filtre passe-haut sur la sortante "
                    "et la retirer au drop de l'entrante.";
    } else if (harmOk && bpmClose) {
        tp.type = TransitionType::Crossfade;
        tp.bars = 16;
        tp.label = "Crossfade harmonique";
        tp.detail = "Compatibilit\xc3\xa9 correcte : fondu crois\xc3\xa9 \xc3\xa9quilibr\xc3\xa9 (16 mesures), "
                    "EQ pour \xc3\xa9viter l'empilement des basses.";
    } else if (bpmTight) {
        tp.type = TransitionType::QuickBlend;
        tp.bars = 8;
        tp.label = "Fondu court";
        tp.detail = "Tempo verrouill\xc3\xa9 mais cl\xc3\xa9 \xc3\xa9loign\xc3\xa9""e : fondu court (8 mesures) "
                    "sur les passages instrumentaux.";
    } else {
        tp.type = TransitionType::Cut;
        tp.bars = 2;
        tp.label = "Coupe nette";
        tp.detail = "Peu de compatibilit\xc3\xa9 : pr\xc3\xa9voir une coupe sur un temps fort.";
    }

    if (tp.type == TransitionType::BassSwap && isInst(to)) {
        tp.detail += " Entrante instrumentale : id\xc3\xa9""al pour superposer un acapella.";
    }

    return tp;
}

TransitionPlan SmartSuggestEngine::suggestTransition(int64_t fromTrackId,
                                                     int64_t toTrackId) const {
    TransitionPlan tp;
    if (!db_ || fromTrackId <= 0 || toTrackId <= 0) return tp;
    auto a = db_->getTrack(fromTrackId);
    auto b = db_->getTrack(toTrackId);
    if (!a || !b) return tp;
    return buildTransitionPlan(*a, *b);
}

PhraseInfo SmartSuggestEngine::computePhrases(const Models::Track& t) {
    PhraseInfo pi;
    if (t.bpm <= 0.0 || t.duration <= 0.0) {
        pi.summary = "BPM ou dur\xc3\xa9""e inconnus : d\xc3\xa9tection de phrases indisponible.";
        return pi;
    }

    pi.bpm = t.bpm;
    pi.durationSec = t.duration;
    pi.secondsPerBar = (60.0 / t.bpm) * 4.0;
    if (pi.secondsPerBar <= 0.0) {
        pi.summary = "BPM invalide.";
        return pi;
    }
    pi.barsTotal = (int) std::floor(t.duration / pi.secondsPerBar);

    int phraseLen = 16;
    if (pi.barsTotal >= 96)      phraseLen = 32;
    else if (pi.barsTotal >= 32) phraseLen = 16;
    else                          phraseLen = 8;
    pi.phraseLengthBars = phraseLen;

    double offsetSec = 0.0;
    if (t.introStart >= 0.0)      offsetSec = t.introStart;

    for (int bar = 0; bar * pi.secondsPerBar + offsetSec < t.duration; bar += phraseLen) {
        double sec = offsetSec + bar * pi.secondsPerBar;
        if (sec > t.duration) break;
        pi.phraseBoundariesSec.push_back(sec);
    }

    if (t.introEnd > 0.0) {
        pi.mixInPointSec = t.introEnd;
    } else if (!pi.phraseBoundariesSec.empty() && pi.phraseBoundariesSec.size() >= 2) {
        pi.mixInPointSec = pi.phraseBoundariesSec[1];
    } else {
        pi.mixInPointSec = phraseLen * pi.secondsPerBar;
    }

    if (t.outroStart >= 0.0) {
        pi.mixOutPointSec = t.outroStart;
    } else {
        double mixWindow = phraseLen * pi.secondsPerBar;
        double target = t.duration - mixWindow;
        double best = -1.0;
        for (double b : pi.phraseBoundariesSec)
            if (b <= target && b > best) best = b;
        pi.mixOutPointSec = (best >= 0.0) ? best : std::max(0.0, target);
    }

    pi.valid = true;
    std::ostringstream o;
    o << pi.barsTotal << " mesures - phrases de " << phraseLen << " mesures ("
      << pi.phraseBoundariesSec.size() << " frontieres)";
    pi.summary = o.str();
    return pi;
}

PhraseInfo SmartSuggestEngine::detectPhrases(int64_t trackId) const {
    PhraseInfo pi;
    if (!db_ || trackId <= 0) return pi;
    auto t = db_->getTrack(trackId);
    if (!t) return pi;
    return computePhrases(*t);
}

PhraseInfo SmartSuggestEngine::detectPhrasesForCurrent() const {
    int64_t cur = getCurrentTrack();
    if (cur <= 0) return {};
    return detectPhrases(cur);
}

void SmartSuggestEngine::associateTracks(int64_t fromTrackId, int64_t toTrackId,
                                         int weight) {
    if (fromTrackId <= 0 || toTrackId <= 0 || fromTrackId == toTrackId) return;
    {
        std::lock_guard<std::mutex> g(lock_);
        manualAssoc_[fromTrackId].insert(toTrackId);
    }
    if (m_style && weight != 0) {
        m_style->addPair(fromTrackId, toTrackId, weight);
        m_style->train();
        std::lock_guard<std::mutex> g(lock_);
        lastStyleTrainUnix_ = nowUnix();
    }
    spdlog::info("[SmartSuggest] association manuelle {} -> {}", fromTrackId, toTrackId);
}

void SmartSuggestEngine::unassociateTracks(int64_t fromTrackId, int64_t toTrackId) {
    std::lock_guard<std::mutex> g(lock_);
    auto it = manualAssoc_.find(fromTrackId);
    if (it != manualAssoc_.end()) {
        it->second.erase(toTrackId);
        if (it->second.empty()) manualAssoc_.erase(it);
    }
}

bool SmartSuggestEngine::isAssociated(int64_t fromTrackId, int64_t toTrackId) const {
    std::lock_guard<std::mutex> g(lock_);
    auto it = manualAssoc_.find(fromTrackId);
    return it != manualAssoc_.end() && it->second.count(toTrackId) > 0;
}

void SmartSuggestEngine::setManualAssociations(
    const std::vector<std::pair<int64_t, int64_t>>& pairs) {
    std::lock_guard<std::mutex> g(lock_);
    manualAssoc_.clear();
    for (const auto& p : pairs)
        if (p.first > 0 && p.second > 0)
            manualAssoc_[p.first].insert(p.second);
}

std::vector<std::pair<int64_t, int64_t>>
SmartSuggestEngine::getManualAssociations() const {
    std::lock_guard<std::mutex> g(lock_);
    std::vector<std::pair<int64_t, int64_t>> out;
    for (const auto& kv : manualAssoc_)
        for (int64_t to : kv.second)
            out.emplace_back(kv.first, to);
    return out;
}

void SmartSuggestEngine::clearManualAssociations() {
    std::lock_guard<std::mutex> g(lock_);
    manualAssoc_.clear();
}

std::vector<PathStep> SmartSuggestEngine::searchTracksForPicker(
    const std::string& query, int limit) const {
    std::vector<PathStep> out;
    if (!db_) return out;
    if (limit <= 0) limit = 40;

    const std::string q = toLowerCopy(query);
    int64_t cur = getCurrentTrack();
    auto all = db_->getAllTracks();
    for (const auto& t : all) {
        if (t.id == cur) continue;
        if (!q.empty()) {
            const std::string hay = toLowerCopy(t.title + " " + t.artist + " " + t.genre);
            if (hay.find(q) == std::string::npos) continue;
        }
        PathStep ps;
        ps.trackId     = t.id;
        ps.title       = t.title;
        ps.artist      = t.artist;
        ps.camelotKey  = t.camelotKey;
        ps.bpm         = t.bpm;
        ps.energyScore = t.energy;
        out.push_back(std::move(ps));
        if ((int) out.size() >= limit) break;
    }
    return out;
}

std::vector<PathStep> SmartSuggestEngine::findPathToTarget(int64_t targetTrackId,
                                                           int maxHops,
                                                           int branching) const {
    std::vector<PathStep> result;
    if (!db_ || targetTrackId <= 0) return result;

    int64_t startId = getCurrentTrack();
    if (startId <= 0 || startId == targetTrackId) return result;

    auto startOpt  = db_->getTrack(startId);
    auto targetOpt = db_->getTrack(targetTrackId);
    if (!startOpt || !targetOpt) return result;

    if (maxHops < 1) maxHops = 1;
    if (maxHops > 6) maxHops = 6;
    if (branching < 2) branching = 2;
    if (branching > 12) branching = 12;

    std::unordered_set<int64_t> blacklist;
    {
        std::lock_guard<std::mutex> g(lock_);
        blacklist = blacklist_;
    }

    auto all = db_->getAllTracks();
    std::unordered_map<int64_t, const Models::Track*> byId;
    byId.reserve(all.size());
    for (const auto& t : all) byId[t.id] = &t;
    byId[startOpt->id]  = &*startOpt;
    byId[targetOpt->id] = &*targetOpt;

    auto edgeScore = [](const Models::Track& a, const Models::Track& b) -> int {
        const int harm = calcHarmonicScore(a.camelotKey, b.camelotKey);
        const int bpm  = calcBPMScore(a.bpm, b.bpm);
        int energy = 50;
        if (a.energy > 0.0 && b.energy > 0.0) {
            double d = std::abs(b.energy - a.energy);
            energy = (d <= 1.0) ? 100 : (d <= 2.0 ? 70 : (d <= 3.0 ? 45 : 20));
        }
        double wH = (harm >= 0) ? 0.5 : 0.0;
        double wB = (bpm  >= 0) ? 0.4 : 0.0;
        double wE = 0.1;
        double num = 0.0, den = 0.0;
        if (harm >= 0) { num += wH * harm; den += wH; }
        if (bpm  >= 0) { num += wB * bpm;  den += wB; }
        num += wE * energy; den += wE;
        return (den > 0.0) ? (int) std::round(num / den) : 25;
    };

    struct Node {
        std::vector<int64_t> path;
        double cost = 0.0;
        int64_t last = 0;
    };

    auto cmp = [](const Node& a, const Node& b) { return a.cost > b.cost; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

    Node init;
    init.path = { startOpt->id };
    init.last = startOpt->id;
    pq.push(init);

    std::unordered_map<int64_t, double> bestCostAt;
    Node bestComplete;
    bool found = false;

    int expansions = 0;
    const int kMaxExpansions = 4000;

    while (!pq.empty() && expansions < kMaxExpansions) {
        Node cur = pq.top();
        pq.pop();
        ++expansions;

        if (cur.last == targetTrackId) {
            bestComplete = cur;
            found = true;
            break;
        }
        if ((int) cur.path.size() - 1 >= maxHops) continue;

        const Models::Track* curTrack = byId.count(cur.last) ? byId[cur.last] : nullptr;
        if (!curTrack) continue;

        std::vector<std::pair<int, int64_t>> cands;
        cands.reserve(all.size());
        const Models::Track& tgt = *targetOpt;
        for (const auto& cand : all) {
            if (cand.id == cur.last) continue;
            if (blacklist.count(cand.id)) continue;
            bool already = false;
            for (int64_t p : cur.path) if (p == cand.id) { already = true; break; }
            if (already) continue;

            int es = edgeScore(*curTrack, cand);
            if (cand.id == targetTrackId) es = std::max(es, 1);
            int guide = edgeScore(cand, tgt);
            int rank = es + guide / 2;
            cands.emplace_back(rank, cand.id);
        }

        bool targetReachable = false;
        {
            int directEs = edgeScore(*curTrack, tgt);
            if (directEs > 0) targetReachable = true;
        }
        if (targetReachable) {
            int es = edgeScore(*curTrack, tgt);
            Node n = cur;
            n.path.push_back(targetTrackId);
            n.last = targetTrackId;
            n.cost = cur.cost + (100.0 - es);
            pq.push(n);
        }

        std::partial_sort(cands.begin(),
                          cands.begin() + std::min((size_t) branching, cands.size()),
                          cands.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        int taken = 0;
        for (const auto& c : cands) {
            if (c.second == targetTrackId) continue;
            if (taken >= branching) break;
            const Models::Track* nt = byId.count(c.second) ? byId[c.second] : nullptr;
            if (!nt) continue;
            int es = edgeScore(*curTrack, *nt);
            double stepCost = (100.0 - es);
            Node n = cur;
            n.path.push_back(c.second);
            n.last = c.second;
            n.cost = cur.cost + stepCost;
            auto bit = bestCostAt.find(c.second);
            if (bit != bestCostAt.end() && bit->second <= n.cost) continue;
            bestCostAt[c.second] = n.cost;
            pq.push(n);
            ++taken;
        }
    }

    if (!found) return result;

    for (size_t i = 1; i < bestComplete.path.size(); ++i) {
        const Models::Track* prev = byId.count(bestComplete.path[i - 1])
            ? byId[bestComplete.path[i - 1]] : nullptr;
        const Models::Track* node = byId.count(bestComplete.path[i])
            ? byId[bestComplete.path[i]] : nullptr;
        if (!node) continue;
        PathStep ps;
        ps.trackId     = node->id;
        ps.title       = node->title;
        ps.artist      = node->artist;
        ps.camelotKey  = node->camelotKey;
        ps.bpm         = node->bpm;
        ps.energyScore = node->energy;
        if (prev) {
            ps.transitionScore = edgeScore(*prev, *node);
            TransitionPlan tp = buildTransitionPlan(*prev, *node);
            ps.transitionLabel = tp.label;
            ps.mixBars         = tp.bars;
        }
        result.push_back(std::move(ps));
    }
    return result;
}

} // namespace BeatMate::Services::Suggestions
