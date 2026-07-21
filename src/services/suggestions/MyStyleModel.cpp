#include "MyStyleModel.h"

#include "../library/TrackDatabase.h"
#include "../../models/Track.h"

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace BeatMate::Services::Suggestions {

namespace {

constexpr int64_t kPairWindowSeconds = 30 * 60;   // 30 min adjacency window
constexpr int     kHistoryLimit      = 5000;
constexpr int     kPairCountCap      = 30;

double cosine(const double a[4], const double b[4]) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < 4; ++i) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    double c = dot / (std::sqrt(na) * std::sqrt(nb));
    if (c < -1.0) c = -1.0;
    if (c >  1.0) c =  1.0;
    return c;
}

} // namespace

MyStyleModel::MyStyleModel(std::shared_ptr<Services::Library::TrackDatabase> db)
    : db_(std::move(db)) {
    // Profil restaure au demarrage pour que scoreCandidate() soit utile avant tout train().
    try { loadProfile(); }
    catch (const std::exception& e) {
        spdlog::warn("[MyStyle] loadProfile failed: {}", e.what());
    }
}

MyStyleModel::~MyStyleModel() {
    if (trainThread_.joinable()) trainThread_.join();
}

void MyStyleModel::train() {
    if (!db_) return;


    std::ostringstream q;
    q << "SELECT t.* FROM tracks t "
      << "INNER JOIN play_history ph ON ph.track_id = t.id "
      << "ORDER BY ph.played_at ASC LIMIT " << kHistoryLimit;

    std::vector<Models::Track> rows;
    try {
        rows = db_->getTracksByQuery(q.str(), {});
    } catch (const std::exception& e) {
        spdlog::warn("[MyStyle] history query failed: {}", e.what());
        return;
    }

    std::unordered_map<int64_t, std::unordered_map<int64_t, int>> pairs;
    std::unordered_map<std::string, int> genres;
    std::unordered_map<std::string, int> camelots;
    std::unordered_map<int, int> years;

    double sumBpm = 0.0, sumEnergy = 0.0, sumDance = 0.0, sumVal = 0.0;
    int featureN = 0;

    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];

        if (!r.genre.empty())       ++genres[r.genre];
        if (!r.camelotKey.empty())  ++camelots[r.camelotKey];
        if (r.year > 0)             ++years[r.year];

        if (r.bpm > 0.0) {
            sumBpm    += r.bpm;
            sumEnergy += static_cast<double>(r.energy);
            sumDance  += static_cast<double>(r.danceability);
            sumVal    += static_cast<double>(r.rating) / 5.0;
            ++featureN;
        }

        if (i + 1 < rows.size()) {
            const auto& next = rows[i + 1];
            int64_t tA = r.lastPlayed;
            int64_t tB = next.lastPlayed;
            bool withinWindow = true;
            if (tA > 0 && tB > 0) {
                int64_t delta = std::llabs(tB - tA);
                withinWindow = (delta <= kPairWindowSeconds);
            }
            if (withinWindow && r.id > 0 && next.id > 0 && r.id != next.id) {
                ++pairs[r.id][next.id];
            }
        }
    }

    AvgFeatures avg;
    if (featureN > 0) {
        avg.bpm          = sumBpm    / featureN;
        avg.energy       = sumEnergy / featureN;
        avg.danceability = sumDance  / featureN;
        avg.valence      = sumVal    / featureN;
    }

    std::string domGenre, domCamelot;
    int bestG = 0, bestC = 0;
    for (const auto& kv : genres)   if (kv.second > bestG) { bestG = kv.second; domGenre   = kv.first; }
    for (const auto& kv : camelots) if (kv.second > bestC) { bestC = kv.second; domCamelot = kv.first; }

    // Le feedback utilisateur (accepte/passe) persiste dans track_pairs
    mergeTrackPairsFromDb(pairs);

    {
        std::lock_guard<std::mutex> g(lock_);
        pairCounts_       = std::move(pairs);
        genreHistogram_   = std::move(genres);
        camelotHistogram_ = std::move(camelots);
        yearHistogram_    = std::move(years);
        avg_              = avg;
        totalPlays_       = static_cast<int>(rows.size());
        dominantGenre_    = std::move(domGenre);
        dominantCamelot_  = std::move(domCamelot);
    }

    spdlog::info("[MyStyle] trained: plays={}, avgBpm={:.1f}, avgEnergy={:.2f}, genre='{}', key='{}'",
                 static_cast<int>(rows.size()),
                 avg.bpm, avg.energy,
                 domGenre.empty() ? "?" : domGenre,
                 domCamelot.empty() ? "?" : domCamelot);
}

int MyStyleModel::calcStyleScore(int64_t currentTrackId, int64_t candidateTrackId) {
    if (!db_) return 50;

    std::unordered_map<int64_t, int> pairsForCurrent;
    AvgFeatures avg;
    std::string domGenre;
    int total = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        auto it = pairCounts_.find(currentTrackId);
        if (it != pairCounts_.end()) pairsForCurrent = it->second;
        avg         = avg_;
        domGenre    = dominantGenre_;
        total       = totalPlays_;
    }

    // -1 = signal inconnu, ecarte du melange par l'appelant.
    if (total <= 0) return -1;

    auto candOpt = db_->getTrack(candidateTrackId);
    if (!candOpt) return 50;
    const auto& cand = *candOpt;

    double score = 50.0;

    auto pit = pairsForCurrent.find(candidateTrackId);
    if (pit != pairsForCurrent.end()) {
        int pairBonus = 8 * pit->second;
        if (pairBonus > kPairCountCap) pairBonus = kPairCountCap;
        score += pairBonus;
    }

    if (avg.bpm > 0.0 && cand.bpm > 0.0) {
        const double avgVec[4]  = { avg.bpm / 200.0,
                                    avg.energy / 10.0,
                                    avg.danceability,
                                    avg.valence };
        const double candVec[4] = { cand.bpm / 200.0,
                                    static_cast<double>(cand.energy) / 10.0,
                                    static_cast<double>(cand.danceability),
                                    static_cast<double>(cand.rating) / 5.0 };
        double cos = cosine(avgVec, candVec);
        if (cos < 0.0) cos = 0.0;
        score += 20.0 * cos;
    }

    if (!domGenre.empty() && !cand.genre.empty() && cand.genre == domGenre) {
        score += 10.0;
    }

    if (score < 0.0)   score = 0.0;
    if (score > 100.0) score = 100.0;
    return static_cast<int>(std::round(score));
}

int MyStyleModel::totalPlays() const {
    std::lock_guard<std::mutex> g(lock_);
    return totalPlays_;
}

std::string MyStyleModel::dominantGenre() const {
    std::lock_guard<std::mutex> g(lock_);
    return dominantGenre_;
}

double MyStyleModel::avgBPM() const {
    std::lock_guard<std::mutex> g(lock_);
    return avg_.bpm;
}

double MyStyleModel::avgEnergy() const {
    std::lock_guard<std::mutex> g(lock_);
    return avg_.energy;
}

std::string MyStyleModel::dominantCamelot() const {
    std::lock_guard<std::mutex> g(lock_);
    return dominantCamelot_;
}

std::vector<std::pair<std::string, int>> MyStyleModel::topGenres(int n) const {
    std::lock_guard<std::mutex> g(lock_);
    std::vector<std::pair<std::string, int>> v(genreHistogram_.begin(),
                                               genreHistogram_.end());
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (n > 0 && static_cast<int>(v.size()) > n) v.resize(static_cast<size_t>(n));
    return v;
}

std::vector<std::pair<int, int>> MyStyleModel::topYears(int n) const {
    std::lock_guard<std::mutex> g(lock_);
    std::vector<std::pair<int, int>> v(yearHistogram_.begin(), yearHistogram_.end());
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (n > 0 && static_cast<int>(v.size()) > n) v.resize(static_cast<size_t>(n));
    return v;
}

void MyStyleModel::mergeTrackPairsFromDb(
    std::unordered_map<int64_t, std::unordered_map<int64_t, int>>& pairs) const {
    if (!db_ || !db_->isOpen_) return;

    std::lock_guard<std::recursive_mutex> dbLock(db_->mutex_);
    const char* sql =
        "SELECT track_a_id, track_b_id, play_count FROM track_pairs "
        "WHERE play_count > 0";
    sqlite3_stmt* stmt = nullptr;
    // Table absente (aucun feedback encore enregistre) : prepare echoue, on sort.
    if (sqlite3_prepare_v2(db_->db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    int merged = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int64_t a = sqlite3_column_int64(stmt, 0);
        const int64_t b = sqlite3_column_int64(stmt, 1);
        const int     w = sqlite3_column_int(stmt, 2);
        if (a > 0 && b > 0 && a != b && w > 0) {
            pairs[a][b] += w;
            ++merged;
        }
    }
    sqlite3_finalize(stmt);
    if (merged > 0)
        spdlog::info("[MyStyle] track_pairs feedback merged: {} paires", merged);
}

bool MyStyleModel::addPair(int64_t trackA, int64_t trackB, int delta) {
    if (!db_ || trackA <= 0 || trackB <= 0 || trackA == trackB) return false;

    db_->executeSQL(
        "CREATE TABLE IF NOT EXISTS track_pairs ("
        "  track_a_id INTEGER NOT NULL,"
        "  track_b_id INTEGER NOT NULL,"
        "  play_count INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (track_a_id, track_b_id))");

    std::ostringstream sql;
    sql << "INSERT INTO track_pairs (track_a_id, track_b_id, play_count) VALUES ("
        << trackA << "," << trackB << "," << delta << ") "
        << "ON CONFLICT(track_a_id, track_b_id) DO UPDATE SET "
        << "play_count = MAX(0, play_count + (" << delta << "))";
    const bool ok = db_->executeSQL(sql.str());

    // Mise a jour en memoire pour eviter un retrain complet.
    if (ok) {
        std::lock_guard<std::mutex> g(lock_);
        auto& row = pairCounts_[trackA];
        int cur = row[trackB] + delta;
        if (cur < 0) cur = 0;
        row[trackB] = cur;
    }
    return ok;
}

namespace {

inline int bpmBin(double bpm) {
    if (bpm <= 0.0) return -1;
    return static_cast<int>(std::floor(bpm / 4.0));
}

inline int energyBin(float e) {
    if (e <= 0.0f) return -1;
    int b = static_cast<int>(std::round(static_cast<double>(e)));
    if (b < 0)  b = 0;
    if (b > 10) b = 10;
    return b;
}

inline int camelotNumber(const std::string& k) {
    if (k.empty()) return -1;
    int n = std::atoi(k.c_str());
    if (n < 1 || n > 12) return -1;
    return n;
}

inline char camelotLetter(const std::string& k) {
    if (k.empty()) return '\0';
    char c = k.back();
    if (c == 'a' || c == 'A') return 'A';
    if (c == 'b' || c == 'B') return 'B';
    return '\0';
}

// Camelot-wheel distance (harmonic) between two keys in 0..6.
inline int camelotDistance(const std::string& a, const std::string& b) {
    int na = camelotNumber(a), nb = camelotNumber(b);
    char la = camelotLetter(a), lb = camelotLetter(b);
    if (na < 0 || nb < 0 || la == '\0' || lb == '\0') return 6;
    int dn = std::abs(na - nb);
    if (dn > 6) dn = 12 - dn;
    if (la != lb) dn += 1;
    if (dn > 6) dn = 6;
    return dn;
}

} // namespace

std::string MyStyleModel::profilePath() {
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory).getChildFile("BeatMate");
    dir.createDirectory();
    return dir.getChildFile("mystyle_profile.json").getFullPathName().toStdString();
}

void MyStyleModel::trainFromHistoryAsync() {
    if (trainThread_.joinable()) trainThread_.join();
    trainThread_ = std::thread([this]() {
        try { this->trainFromHistory(); }
        catch (const std::exception& e) {
            spdlog::warn("[MyStyle] async train threw: {}", e.what());
        }
    });
}

void MyStyleModel::trainFromHistory() {
    if (!db_) return;

    std::ostringstream q;
    q << "SELECT t.* FROM tracks t "
      << "INNER JOIN play_history ph ON ph.track_id = t.id "
      << "ORDER BY ph.played_at ASC LIMIT 5000";

    std::vector<Models::Track> rows;
    try { rows = db_->getTracksByQuery(q.str(), {}); }
    catch (const std::exception& e) {
        spdlog::warn("[MyStyle] trainFromHistory query failed: {}", e.what());
        return;
    }

    // played_at recupere en SQL brut : le mapping Track ne le conserve pas.
    std::vector<int64_t> playedAt;
    playedAt.reserve(rows.size());
    if (db_->isOpen_) {
        std::lock_guard<std::recursive_mutex> dbLock(db_->mutex_);
        const char* sql =
            "SELECT ph.played_at FROM play_history ph "
            "INNER JOIN tracks t ON t.id = ph.track_id "
            "ORDER BY ph.played_at ASC LIMIT 5000";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_->db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                playedAt.push_back(sqlite3_column_int64(stmt, 0));
            }
            sqlite3_finalize(stmt);
        }
    }

    std::unordered_map<int64_t, std::unordered_map<int64_t, int>> pairs;
    std::unordered_map<std::string, int> genres;
    std::unordered_map<std::string, int> camelots;
    std::unordered_map<int, int> years;
    std::unordered_map<int, int> bpmBins;
    std::unordered_map<int, int> energyBins;
    std::unordered_map<int, int> hours;
    std::unordered_map<int, int> bpmDeltas;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> gBigram;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> kBigram;

    double sumBpm = 0.0, sumEnergy = 0.0, sumDance = 0.0, sumVal = 0.0;
    int featureN = 0;

    // Session splitting: a new session starts when delta > 3h between plays.
    constexpr int64_t kSessionGapSec = 3 * 60 * 60;
    constexpr int64_t kPairGapSec    = 30 * 60;
    std::vector<std::pair<int64_t,int64_t>> sessionSpans; // start..end in unix
    int64_t sessionStart = 0, sessionEnd = 0;

    auto atTs = [&](size_t i) -> int64_t {
        if (i < playedAt.size()) return playedAt[i];
        return rows[i].lastPlayed;
    };

    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];

        if (!r.genre.empty())      ++genres[r.genre];
        if (!r.camelotKey.empty()) ++camelots[r.camelotKey];
        if (r.year > 0)            ++years[r.year];

        int bb = bpmBin(r.bpm);
        if (bb >= 0) ++bpmBins[bb];
        int eb = energyBin(r.energy);
        if (eb >= 0) ++energyBins[eb];

        if (r.bpm > 0.0) {
            sumBpm    += r.bpm;
            sumEnergy += static_cast<double>(r.energy);
            sumDance  += static_cast<double>(r.danceability);
            sumVal    += static_cast<double>(r.rating) / 5.0;
            ++featureN;
        }

        int64_t ts = atTs(i);
        if (ts > 0) {
            std::time_t tt = static_cast<std::time_t>(ts);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &tt);
#else
            localtime_r(&tt, &lt);
#endif
            ++hours[lt.tm_hour];

            if (sessionStart == 0) sessionStart = ts;
            if (sessionEnd != 0 && ts - sessionEnd > kSessionGapSec) {
                sessionSpans.push_back({ sessionStart, sessionEnd });
                sessionStart = ts;
            }
            sessionEnd = ts;
        }

        if (i + 1 < rows.size()) {
            const auto& next = rows[i + 1];
            int64_t tA = atTs(i);
            int64_t tB = atTs(i + 1);
            bool withinWindow = true;
            if (tA > 0 && tB > 0) {
                withinWindow = (std::llabs(tB - tA) <= kPairGapSec);
            }
            if (withinWindow && r.id > 0 && next.id > 0 && r.id != next.id) {
                ++pairs[r.id][next.id];

                if (!r.genre.empty() && !next.genre.empty())
                    ++gBigram[r.genre][next.genre];
                if (!r.camelotKey.empty() && !next.camelotKey.empty())
                    ++kBigram[r.camelotKey][next.camelotKey];

                if (r.bpm > 0.0 && next.bpm > 0.0) {
                    int d = static_cast<int>(std::round(next.bpm - r.bpm));
                    if (d < -10) d = -10;
                    if (d >  10) d =  10;
                    ++bpmDeltas[d];
                }
            }
        }
    }
    if (sessionStart != 0) sessionSpans.push_back({ sessionStart, sessionEnd });

    AvgFeatures avg;
    if (featureN > 0) {
        avg.bpm          = sumBpm    / featureN;
        avg.energy       = sumEnergy / featureN;
        avg.danceability = sumDance  / featureN;
        avg.valence      = sumVal    / featureN;
    }

    std::string domGenre, domCamelot;
    int bestG = 0, bestC = 0;
    for (const auto& kv : genres)   if (kv.second > bestG) { bestG = kv.second; domGenre   = kv.first; }
    for (const auto& kv : camelots) if (kv.second > bestC) { bestC = kv.second; domCamelot = kv.first; }

    int prefBpm = -1, bestBpm = 0;
    for (const auto& kv : bpmBins)  if (kv.second > bestBpm) { bestBpm = kv.second; prefBpm = kv.first; }

    double avgSession = 0.0;
    if (!sessionSpans.empty()) {
        double sum = 0.0;
        for (const auto& s : sessionSpans) sum += static_cast<double>(s.second - s.first);
        avgSession = sum / static_cast<double>(sessionSpans.size());
    }

    // Comme dans train() : le feedback track_pairs survit au retrain complet.
    mergeTrackPairsFromDb(pairs);

    {
        std::lock_guard<std::mutex> g(lock_);
        pairCounts_        = std::move(pairs);
        genreHistogram_    = std::move(genres);
        camelotHistogram_  = std::move(camelots);
        yearHistogram_     = std::move(years);
        bpmBinHistogram_   = std::move(bpmBins);
        energyBinHistogram_= std::move(energyBins);
        hourHistogram_     = std::move(hours);
        bpmDeltaHistogram_ = std::move(bpmDeltas);
        genreBigram_       = std::move(gBigram);
        keyBigram_         = std::move(kBigram);
        avg_               = avg;
        totalPlays_        = static_cast<int>(rows.size());
        dominantGenre_     = std::move(domGenre);
        dominantCamelot_   = std::move(domCamelot);
        preferredBpmBin_   = prefBpm;
        avgSessionSec_     = avgSession;
        playsSinceRetrain_ = 0;

        try { saveProfileLocked(); }
        catch (const std::exception& e) {
            spdlog::warn("[MyStyle] saveProfile failed: {}", e.what());
        }
    }

    spdlog::info("[MyStyle] trainFromHistory: plays={}, avgBpm={:.1f}, prefBpmBin={}, "
                 "dominantGenre='{}', dominantKey='{}', sessions={}, avgSessionSec={:.0f}",
                 totalPlays_, avg.bpm, preferredBpmBin_,
                 dominantGenre_.empty() ? "?" : dominantGenre_,
                 dominantCamelot_.empty() ? "?" : dominantCamelot_,
                 static_cast<int>(sessionSpans.size()), avgSessionSec_);
}

void MyStyleModel::notePlay() {
    int plays = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        plays = ++playsSinceRetrain_;
    }
    if (plays >= kRetrainEvery) {
        trainFromHistoryAsync();
    }
}

bool MyStyleModel::hasEnoughHistory() const {
    std::lock_guard<std::mutex> g(lock_);
    return totalPlays_ >= kMinPlaysForPrior;
}

float MyStyleModel::scoreCandidate(const Models::Track& current,
                                   const Models::Track& candidate) const {
    std::unordered_map<std::string, int> genres;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> gBi, kBi;
    int prefBpm = -1;
    int total = 0;
    double avgEnergy = 0.0;
    int genreTotal = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (totalPlays_ < kMinPlaysForPrior) return 1.0f;
        genres     = genreHistogram_;
        gBi        = genreBigram_;
        kBi        = keyBigram_;
        prefBpm    = preferredBpmBin_;
        total      = totalPlays_;
        avgEnergy  = avg_.energy;
        for (const auto& kv : genres) genreTotal += kv.second;
    }

    double genreScore = 0.5;
    if (!candidate.genre.empty() && genreTotal > 0) {
        int c = 0;
        auto it = genres.find(candidate.genre);
        if (it != genres.end()) c = it->second;
        double p = (static_cast<double>(c) + 1.0) /
                   (static_cast<double>(genreTotal) + static_cast<double>(genres.size()) + 1.0);
        double bigramBoost = 0.0;
        if (!current.genre.empty()) {
            auto row = gBi.find(current.genre);
            if (row != gBi.end()) {
                int rowTotal = 0;
                for (const auto& kv : row->second) rowTotal += kv.second;
                if (rowTotal > 0) {
                    int cb = 0;
                    auto it2 = row->second.find(candidate.genre);
                    if (it2 != row->second.end()) cb = it2->second;
                    bigramBoost = static_cast<double>(cb) / static_cast<double>(rowTotal);
                }
            }
        }
        // Remise a l'echelle de la log-proba ~[-ln(N),0] vers [0..1].
        double scaled = 1.0 + std::log(p) / std::log(static_cast<double>(genreTotal + 2));
        if (scaled < 0.0) scaled = 0.0;
        if (scaled > 1.0) scaled = 1.0;
        genreScore = 0.6 * scaled + 0.4 * bigramBoost;
    }

    double keyScore = 0.5;
    if (!candidate.camelotKey.empty()) {
        double bg = 0.0;
        if (!current.camelotKey.empty()) {
            auto row = kBi.find(current.camelotKey);
            if (row != kBi.end()) {
                int rowTotal = 0;
                for (const auto& kv : row->second) rowTotal += kv.second;
                if (rowTotal > 0) {
                    int cb = 0;
                    auto it2 = row->second.find(candidate.camelotKey);
                    if (it2 != row->second.end()) cb = it2->second;
                    bg = static_cast<double>(cb) / static_cast<double>(rowTotal);
                }
            }
        }
        // Repli harmonique : distance sur la roue Camelot.
        int dist = camelotDistance(current.camelotKey, candidate.camelotKey);
        double harm = 1.0 - static_cast<double>(dist) / 6.0;
        keyScore = 0.5 * harm + 0.5 * std::min(1.0, bg * 4.0);
    }

    double bpmScore = 0.5;
    if (candidate.bpm > 0.0 && prefBpm >= 0) {
        int cb = bpmBin(candidate.bpm);
        int d = std::abs(cb - prefBpm);
        // Each 4-BPM bin off the peak costs 0.15.
        double s = 1.0 - 0.15 * d;
        if (s < 0.0) s = 0.0;
        bpmScore = s;
    }

    double energyScore = 0.5;
    if (candidate.energy > 0.0f && avgEnergy > 0.0) {
        double de = std::abs(static_cast<double>(candidate.energy) - avgEnergy);
        double s = 1.0 - de / 8.0;
        if (s < 0.0) s = 0.0;
        if (s > 1.0) s = 1.0;
        energyScore = s;
    }

    // Weighted combine (genre 0.35, key 0.25, bpm 0.25, energy 0.15).
    double final = 0.35 * genreScore
                 + 0.25 * keyScore
                 + 0.25 * bpmScore
                 + 0.15 * energyScore;
    if (final < 0.0) final = 0.0;
    if (final > 1.0) final = 1.0;

    // Compress so even "neutral" candidates stay reasonable (map 0..1 -> 0.5..1.1).
    double scaled = 0.5 + 0.6 * final;
    if (scaled > 1.1) scaled = 1.1;
    (void)total;
    return static_cast<float>(scaled);
}

juce::String MyStyleModel::getProfileSummary() const {
    std::lock_guard<std::mutex> g(lock_);
    if (totalPlays_ <= 0)
        return "Mon Style: no history yet. Play a few tracks to bootstrap your profile.";

    std::vector<std::pair<std::string, int>> genres(genreHistogram_.begin(),
                                                    genreHistogram_.end());
    std::sort(genres.begin(), genres.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    int totalGenre = 0;
    for (const auto& kv : genres) totalGenre += kv.second;

    juce::String out = "Top genres: ";
    int n = std::min(3, static_cast<int>(genres.size()));
    for (int i = 0; i < n; ++i) {
        int pct = totalGenre > 0
            ? static_cast<int>(std::round(100.0 * genres[i].second / totalGenre))
            : 0;
        out << juce::String(genres[i].first) << " " << pct << "%";
        if (i + 1 < n) out << " / ";
    }
    if (genres.empty()) out << "n/a";

    double bpmPeak = (preferredBpmBin_ >= 0) ? (preferredBpmBin_ * 4.0 + 2.0) : avg_.bpm;
    out << ". BPM peak " << juce::String(bpmPeak, 0);
    out << ". Favourite key: "
        << (dominantCamelot_.empty() ? juce::String("?") : juce::String(dominantCamelot_));
    out << ". Avg session: " << juce::String(avgSessionSec_ / 60.0, 0) << " min";
    out << ". Plays: " << juce::String(totalPlays_) << ".";
    return out;
}

void MyStyleModel::saveProfileLocked() const {
    using nlohmann::json;
    json j;
    j["totalPlays"]      = totalPlays_;
    j["avgBpm"]          = avg_.bpm;
    j["avgEnergy"]       = avg_.energy;
    j["avgDance"]        = avg_.danceability;
    j["avgValence"]      = avg_.valence;
    j["dominantGenre"]   = dominantGenre_;
    j["dominantCamelot"] = dominantCamelot_;
    j["preferredBpmBin"] = preferredBpmBin_;
    j["avgSessionSec"]   = avgSessionSec_;

    auto dumpStrMap = [](const std::unordered_map<std::string, int>& m) {
        json o = json::object();
        for (const auto& kv : m) o[kv.first] = kv.second;
        return o;
    };
    auto dumpIntMap = [](const std::unordered_map<int, int>& m) {
        json o = json::object();
        for (const auto& kv : m) o[std::to_string(kv.first)] = kv.second;
        return o;
    };
    auto dumpBigram = [](const std::unordered_map<std::string,
                         std::unordered_map<std::string, int>>& m) {
        json o = json::object();
        for (const auto& row : m) {
            json inner = json::object();
            for (const auto& kv : row.second) inner[kv.first] = kv.second;
            o[row.first] = inner;
        }
        return o;
    };

    j["genres"]      = dumpStrMap(genreHistogram_);
    j["camelots"]    = dumpStrMap(camelotHistogram_);
    j["years"]       = dumpIntMap(yearHistogram_);
    j["bpmBins"]     = dumpIntMap(bpmBinHistogram_);
    j["energyBins"]  = dumpIntMap(energyBinHistogram_);
    j["hours"]       = dumpIntMap(hourHistogram_);
    j["bpmDeltas"]   = dumpIntMap(bpmDeltaHistogram_);
    j["genreBigram"] = dumpBigram(genreBigram_);
    j["keyBigram"]   = dumpBigram(keyBigram_);

    std::ofstream f(profilePath());
    if (f.is_open()) f << j.dump(2);
}

bool MyStyleModel::loadProfile() {
    using nlohmann::json;
    std::ifstream f(profilePath());
    if (!f.is_open()) return false;

    json j;
    try { f >> j; }
    catch (...) { return false; }

    auto loadStrMap = [](const json& o, std::unordered_map<std::string,int>& out) {
        if (!o.is_object()) return;
        for (auto it = o.begin(); it != o.end(); ++it)
            out[it.key()] = it.value().get<int>();
    };
    auto loadIntMap = [](const json& o, std::unordered_map<int,int>& out) {
        if (!o.is_object()) return;
        for (auto it = o.begin(); it != o.end(); ++it)
            out[std::atoi(it.key().c_str())] = it.value().get<int>();
    };
    auto loadBigram = [](const json& o,
                         std::unordered_map<std::string,
                             std::unordered_map<std::string,int>>& out) {
        if (!o.is_object()) return;
        for (auto it = o.begin(); it != o.end(); ++it) {
            std::unordered_map<std::string,int> row;
            if (it.value().is_object()) {
                for (auto it2 = it.value().begin(); it2 != it.value().end(); ++it2)
                    row[it2.key()] = it2.value().get<int>();
            }
            out[it.key()] = std::move(row);
        }
    };

    std::lock_guard<std::mutex> g(lock_);
    totalPlays_       = j.value("totalPlays", 0);
    avg_.bpm          = j.value("avgBpm", 0.0);
    avg_.energy       = j.value("avgEnergy", 0.0);
    avg_.danceability = j.value("avgDance", 0.0);
    avg_.valence      = j.value("avgValence", 0.0);
    dominantGenre_    = j.value("dominantGenre", std::string{});
    dominantCamelot_  = j.value("dominantCamelot", std::string{});
    preferredBpmBin_  = j.value("preferredBpmBin", -1);
    avgSessionSec_    = j.value("avgSessionSec", 0.0);

    if (j.contains("genres"))      loadStrMap(j["genres"],      genreHistogram_);
    if (j.contains("camelots"))    loadStrMap(j["camelots"],    camelotHistogram_);
    if (j.contains("years"))       loadIntMap(j["years"],       yearHistogram_);
    if (j.contains("bpmBins"))     loadIntMap(j["bpmBins"],     bpmBinHistogram_);
    if (j.contains("energyBins"))  loadIntMap(j["energyBins"],  energyBinHistogram_);
    if (j.contains("hours"))       loadIntMap(j["hours"],       hourHistogram_);
    if (j.contains("bpmDeltas"))   loadIntMap(j["bpmDeltas"],   bpmDeltaHistogram_);
    if (j.contains("genreBigram")) loadBigram(j["genreBigram"], genreBigram_);
    if (j.contains("keyBigram"))   loadBigram(j["keyBigram"],   keyBigram_);
    return true;
}

} // namespace BeatMate::Services::Suggestions
