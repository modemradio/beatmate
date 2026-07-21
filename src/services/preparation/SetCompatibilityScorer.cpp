#include "SetCompatibilityScorer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Preparation {

int SetCompatibilityScorer::camelotNumber(const std::string& key) {
    std::string num;
    for (char c : key) if (c >= '0' && c <= '9') num += c;
    return num.empty() ? -1 : std::stoi(num);
}

char SetCompatibilityScorer::camelotLetter(const std::string& key) {
    for (char c : key) if (c == 'A' || c == 'B' || c == 'a' || c == 'b')
        return static_cast<char>(std::toupper(c));
    return '?';
}

int SetCompatibilityScorer::scoreKey(const std::string& key1, const std::string& key2) {
    if (key1.empty() || key2.empty()) return 15;

    int n1 = camelotNumber(key1), n2 = camelotNumber(key2);
    char l1 = camelotLetter(key1), l2 = camelotLetter(key2);
    if (n1 < 0 || n2 < 0) return 10;

    if (n1 == n2 && l1 == l2) return 30;

    int diff = std::abs(n1 - n2);
    if (diff == 11) diff = 1;
    if (diff == 1 && l1 == l2) return 25;

    if (n1 == n2 && l1 != l2) return 22;

    if (diff == 2 && l1 == l2) return 18;

    if ((diff == 7 || diff == 5) && l1 == l2) return 15;

    if (diff == 1 && l1 != l2) return 12;

    return std::max(0, 8 - diff);
}

int SetCompatibilityScorer::scoreBpm(float bpm1, float bpm2) {
    if (bpm1 <= 0 || bpm2 <= 0) return 12;

    float diff = std::abs(bpm1 - bpm2);
    float diffHalf = std::abs(bpm1 - bpm2 * 2.0f);
    float diffDouble = std::abs(bpm1 * 2.0f - bpm2);
    diff = std::min({diff, diffHalf, diffDouble});

    if (diff <= 1.0f) return 25;
    if (diff <= 3.0f) return 22;
    if (diff <= 5.0f) return 18;
    if (diff <= 8.0f) return 12;
    if (diff <= 12.0f) return 6;
    return 0;
}

int SetCompatibilityScorer::scoreEnergy(float e1, float e2) {
    if (e1 <= 0 || e2 <= 0) return 10;

    float diff = e2 - e1;
    float absDiff = std::abs(diff);

    if (absDiff <= 1.0f) return 20;
    if (diff > 0 && diff <= 2.0f) return 18;
    if (diff > 0 && diff <= 3.0f) return 15;
    if (diff < 0 && absDiff <= 2.0f) return 16;
    if (diff < 0 && absDiff <= 4.0f) return 10;
    if (absDiff > 5.0f) return 2;
    return 8;
}

int SetCompatibilityScorer::scoreGenre(const std::string& g1, const std::string& g2) {
    if (g1.empty() || g2.empty()) return 8;

    std::string gl1 = g1, gl2 = g2;
    std::transform(gl1.begin(), gl1.end(), gl1.begin(), ::tolower);
    std::transform(gl2.begin(), gl2.end(), gl2.begin(), ::tolower);

    if (gl1 == gl2) return 15;

    static const std::map<std::string, int> families = {
        {"house", 1}, {"tech house", 1}, {"deep house", 1}, {"progressive house", 1},
        {"techno", 2}, {"minimal", 2}, {"melodic techno", 2},
        {"drum and bass", 3}, {"jungle", 3}, {"dnb", 3},
        {"hip hop", 4}, {"rap", 4}, {"r&b", 4}, {"rnb", 4},
        {"pop", 5}, {"dance", 5}, {"edm", 5},
        {"trance", 6}, {"psy trance", 6}, {"progressive trance", 6},
        {"dubstep", 7}, {"bass", 7}, {"trap", 7},
        {"disco", 8}, {"funk", 8}, {"soul", 8}
    };

    auto it1 = families.find(gl1), it2 = families.find(gl2);
    if (it1 != families.end() && it2 != families.end() && it1->second == it2->second)
        return 12;

    return 5;
}

SetCompatibilityScorer::CompatibilityResult
SetCompatibilityScorer::score(const Models::Track& current, const Models::Track& next) {
    CompatibilityResult r;
    std::string ck = current.camelotKey.empty() ? current.key : current.camelotKey;
    std::string nk = next.camelotKey.empty() ? next.key : next.camelotKey;

    r.keyScore = scoreKey(ck, nk);
    r.bpmScore = scoreBpm(current.bpm, next.bpm);
    r.energyScore = scoreEnergy(current.energy, next.energy);
    r.genreScore = scoreGenre(current.genre, next.genre);
    r.varietyScore = (current.artist != next.artist) ? 10 : 3;
    r.score = r.keyScore + r.bpmScore + r.energyScore + r.genreScore + r.varietyScore;

    if (r.keyScore >= 25) r.keyInfo = "Harmonique parfait";
    else if (r.keyScore >= 18) r.keyInfo = "Compatible";
    else if (r.keyScore >= 12) r.keyInfo = "Acceptable";
    else r.keyInfo = "Clash harmonique";

    float bpmDiff = std::abs(current.bpm - next.bpm);
    r.bpmInfo = "BPM " + std::to_string(static_cast<int>(current.bpm))
              + " -> " + std::to_string(static_cast<int>(next.bpm))
              + " (diff " + std::to_string(static_cast<int>(bpmDiff)) + ")";

    if (r.score >= 85) r.advice = "Transition parfaite - long blend possible";
    else if (r.score >= 70) r.advice = "Bonne transition - blend standard";
    else if (r.score >= 50) r.advice = "Transition courte recommandee";
    else if (r.score >= 30) r.advice = "Transition rapide ou effet";
    else r.advice = "Transition difficile - utiliser un breakdown";

    return r;
}

std::vector<SetCompatibilityScorer::Suggestion>
SetCompatibilityScorer::suggestNext(const Models::Track& current,
                                      const std::vector<Models::Track>& pool,
                                      int maxResults) {
    std::vector<Suggestion> suggestions;
    for (auto& t : pool) {
        if (t.id == current.id) continue;
        auto r = score(current, t);
        suggestions.push_back({t.id, r.score, r.keyInfo + " | " + r.bpmInfo});
    }
    std::sort(suggestions.begin(), suggestions.end(),
              [](const Suggestion& a, const Suggestion& b) { return a.score > b.score; });
    if (static_cast<int>(suggestions.size()) > maxResults)
        suggestions.resize(static_cast<size_t>(maxResults));
    return suggestions;
}

std::vector<int64_t> SetCompatibilityScorer::autoOrder(const std::vector<Models::Track>& tracks) {
    if (tracks.empty()) return {};
    if (tracks.size() == 1) return {tracks[0].id};

    std::vector<int64_t> order;
    std::set<int64_t> used;

    int startIdx = 0;
    float minEnergy = 999;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].energy < minEnergy) {
            minEnergy = tracks[i].energy;
            startIdx = static_cast<int>(i);
        }
    }

    order.push_back(tracks[startIdx].id);
    used.insert(tracks[startIdx].id);

    while (order.size() < tracks.size()) {
        const Models::Track* current = nullptr;
        for (auto& t : tracks)
            if (t.id == order.back()) { current = &t; break; }
        if (!current) break;

        int bestScore = -1;
        int64_t bestId = -1;
        for (auto& t : tracks) {
            if (used.count(t.id)) continue;
            auto r = score(*current, t);
            if (r.score > bestScore) {
                bestScore = r.score;
                bestId = t.id;
            }
        }
        if (bestId < 0) break;
        order.push_back(bestId);
        used.insert(bestId);
    }

    spdlog::info("[SetScorer] Auto-ordered {} tracks", order.size());
    return order;
}

}
