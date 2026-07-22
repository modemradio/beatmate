#include "BeatGridGenerator.h"
#include "AIBeatgridService.h"
#include "DownbeatDetectionService.h"
#include "BeatEngine.h"
#include "../audio/AudioTrack.h"
#include "../audio/AudioFileReader.h"
#include "OnsetDetector.h"
#include "../../models/Track.h"
#include "../../services/djsoftware/rekordbox/RekordboxAnlzParser.h"
#include "../../services/djsoftware/rekordbox/RekordboxCipher.h"
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

BeatGridGenerator::BeatGridGenerator() = default;
BeatGridGenerator::~BeatGridGenerator() = default;

double BeatGridGenerator::findFirstBeat(const AudioTrack& track, double bpm) {
    OnsetDetector onset;
    auto onsets = onset.detect(track);
    if (onsets.empty()) return 0.0;

    double beatInterval = 60.0 / bpm;

    double bestOffset = 0.0;
    double bestScore = -1e10;

    for (double candidateOffset : onsets) {
        if (candidateOffset > 2.0) break; // Only check first 2 seconds

        double score = 0.0;
        for (auto& o : onsets) {
            double beatsFromOffset = (o - candidateOffset) / beatInterval;
            double dist = std::fabs(beatsFromOffset - std::round(beatsFromOffset));
            if (dist < 0.1) score += 1.0;
        }

        if (score > bestScore) {
            bestScore = score;
            bestOffset = candidateOffset;
        }
    }

    return bestOffset;
}

BeatGrid BeatGridGenerator::generate(const AudioTrack& track, double bpm, double firstBeat) {
    BeatGrid grid;
    grid.bpm = bpm;

    if (firstBeat < 0) {
        grid.firstBeatOffset = findFirstBeat(track, bpm);
    } else {
        grid.firstBeatOffset = firstBeat;
    }

    const double beatInterval = 60.0 / bpm;
    const double duration = track.getDuration();

    std::vector<double> rawBeats;
    {
        double pos = grid.firstBeatOffset;
        while (pos < duration) {
            rawBeats.push_back(pos);
            pos += beatInterval;
        }
    }

    OnsetDetector onset;
    auto onsets = onset.detect(track);
    if (onsets.empty() || rawBeats.empty()) {
        // Fallback: keep mathematical beats.
        grid.beatPositions = std::move(rawBeats);
    } else {
        std::sort(onsets.begin(), onsets.end());
        const double tol = 0.030; // 30 ms
        grid.beatPositions.reserve(rawBeats.size());
        size_t o = 0;
        int snapped = 0;
        for (double b : rawBeats) {
            while (o + 1 < onsets.size() && onsets[o] < b - tol) ++o;
            double best = b;
            double bestDist = tol;
            for (size_t k = o; k < onsets.size() && k <= o + 1; ++k) {
                double d = std::fabs(onsets[k] - b);
                if (d <= bestDist) {
                    bestDist = d;
                    best = onsets[k];
                }
            }
            if (bestDist < tol) ++snapped;
            grid.beatPositions.push_back(best);
        }
        spdlog::info("BeatGridGenerator: snapped {}/{} beats to onsets (tol={}ms)",
                     snapped, grid.beatPositions.size(), int(tol * 1000));
    }

    for (size_t i = 0; i < grid.beatPositions.size(); i += grid.beatsPerBar) {
        grid.barPositions.push_back(grid.beatPositions[i]);
    }

    spdlog::info("BeatGridGenerator: {:.1f} BPM, {} beats, offset={:.3f}s",
                 bpm, grid.beatPositions.size(), grid.firstBeatOffset);
    return grid;
}

BeatGrid BeatGridGenerator::generateFromBeats(const std::vector<double>& beats, double bpm) {
    BeatGrid grid;
    grid.bpm = bpm;
    grid.beatPositions = beats;

    if (!beats.empty()) {
        grid.firstBeatOffset = beats[0];
        for (size_t i = 0; i < beats.size(); i += grid.beatsPerBar) {
            grid.barPositions.push_back(beats[i]);
        }
    }

    return grid;
}

BeatGrid BeatGridGenerator::generateFromAnlz(const std::string& anlzDatPath) {
    BeatGrid grid;
    auto beats = Services::Rekordbox::RekordboxAnlzParser::readBeats(anlzDatPath);
    if (beats.empty()) {
        spdlog::debug("BeatGridGenerator: no beats parsed from {}", anlzDatPath);
        return grid;
    }
    grid.firstBeatOffset = beats.front().timeSec;
    // Average BPM across beats (Pioneer grids can have slight per-beat drift).
    double sumBpm = 0.0;
    int nBpm = 0;
    for (auto& b : beats) {
        grid.beatPositions.push_back(b.timeSec);
        if (b.beatNumber == 1) grid.barPositions.push_back(b.timeSec);
        if (b.bpm > 0.0) { sumBpm += b.bpm; ++nBpm; }
    }
    grid.bpm = (nBpm > 0) ? (sumBpm / nBpm) : 0.0;
    spdlog::info("BeatGridGenerator: imported {} beats from ANLZ ({:.2f} BPM)",
                 grid.beatPositions.size(), grid.bpm);
    return grid;
}

namespace {

static constexpr const char* kRekordboxMasterKey =
    "402fd482c38817c35ffa8ffb8c7d93143b749e7d315df7a81732a1ff43608497";

std::string rekordboxMasterDbPath() {
#ifdef _WIN32
    auto appData = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                       .getParentDirectory().getChildFile("Roaming");
#else
    auto appData = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library");
#endif
    auto rb = appData.getChildFile("Pioneer").getChildFile("rekordbox");
    auto db = rb.getChildFile("master.db");
    if (db.existsAsFile()) return db.getFullPathName().toStdString();
    auto db6 = appData.getChildFile("Pioneer").getChildFile("rekordbox6").getChildFile("master.db");
    if (db6.existsAsFile()) return db6.getFullPathName().toStdString();
    return {};
}

// Copie master.db (+WAL/SHM) dans %TEMP% pour éviter la course avec Rekordbox
std::string snapshotMasterDb(const std::string& src) {
    juce::File s(src);
    if (!s.existsAsFile()) return src;
    juce::File dst = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("beatmate_grid_snapshot.db");
    dst.deleteFile();
    s.copyFileTo(dst);
    juce::File walS(src + "-wal");
    if (walS.existsAsFile()) {
        juce::File walD(dst.getFullPathName() + "-wal");
        walD.deleteFile();
        walS.copyFileTo(walD);
    }
    juce::File shmS(src + "-shm");
    if (shmS.existsAsFile()) {
        juce::File shmD(dst.getFullPathName() + "-shm");
        shmD.deleteFile();
        shmS.copyFileTo(shmD);
    }
    return dst.getFullPathName().toStdString();
}

// Convertit l'AnalysisDataPath relatif de Rekordbox en chemin absolu
std::string resolveAnlzAbsolute(const std::string& analysisDataPathRel) {
    if (analysisDataPathRel.empty()) return {};
    std::string dbPath = rekordboxMasterDbPath();
    if (dbPath.empty()) return {};
    juce::File root = juce::File(dbPath).getSiblingFile("share");
    std::string rel = analysisDataPathRel;
    for (auto& c : rel) if (c == '/') c = juce::File::getSeparatorChar();
    if (!rel.empty() && (rel.front() == '\\' || rel.front() == '/')) rel.erase(0, 1);
    juce::File dir = root.getChildFile(rel);
    if (!dir.isDirectory()) {
        spdlog::debug("[BeatGridGen] ANLZ dir not found under {}: {}",
                      root.getFullPathName().toStdString(), rel);
        return {};
    }
    juce::File dat = dir.getChildFile("ANLZ0000.DAT");
    if (dat.existsAsFile()) return dat.getFullPathName().toStdString();
    // Certains exports varient en casse ou .EXT mais PQTZ vit dans le .DAT
    return {};
}

// Trouve l'ANLZ*.DAT d'un track via djmdContent (FolderPath + FileNameL)
std::string findAnlzForTrackPath(const std::string& trackAbsPath) {
    if (trackAbsPath.empty()) return {};
    std::string dbPath = rekordboxMasterDbPath();
    if (dbPath.empty()) {
        spdlog::debug("[BeatGridGen] Rekordbox master.db not found");
        return {};
    }
    std::string snap = snapshotMasterDb(dbPath);
    sqlite3* db = nullptr;
    int rc = Services::Rekordbox::RekordboxCipher::openEncrypted(snap, kRekordboxMasterKey, &db);
    if (rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        spdlog::warn("[BeatGridGen] cannot open master.db (rc={})", rc);
        return {};
    }
    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);

    juce::File trackFile(trackAbsPath);
    std::string fileName = trackFile.getFileName().toStdString();

    // Match exact FileNameL — plus rapide qu'un LIKE plein chemin sur grosse base
    sqlite3_stmt* st = nullptr;
    std::string out;
    const char* sql =
        "SELECT FolderPath, AnalysisDataPath FROM djmdContent "
        "WHERE FileNameL = ? LIMIT 32;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, fileName.c_str(), -1, SQLITE_TRANSIENT);
        juce::String expectedParent = trackFile.getParentDirectory()
            .getFullPathName().replaceCharacter('\\', '/').toLowerCase();
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* fp = sqlite3_column_text(st, 0);
            const unsigned char* ap = sqlite3_column_text(st, 1);
            if (!fp || !ap) continue;
            juce::String folder(reinterpret_cast<const char*>(fp));
            folder = folder.replaceCharacter('\\', '/').toLowerCase();
            if (folder.endsWith("/")) folder = folder.dropLastCharacters(1);
            if (expectedParent.endsWithIgnoreCase(folder) ||
                folder.endsWithIgnoreCase(expectedParent)) {
                out = reinterpret_cast<const char*>(ap);
                break;
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);

    if (out.empty()) {
        spdlog::debug("[BeatGridGen] no djmdContent row for {}", trackAbsPath);
        return {};
    }
    return resolveAnlzAbsolute(out);
}

std::shared_ptr<AudioTrack> decodeForAnalysis(const std::string& filePath) {
    if (filePath.empty()) return nullptr;
    AudioFileReader reader;
    return reader.readFile(filePath);
}

} // namespace

BeatGridResult BeatGridGenerator::generateForTrack(BeatGridMode mode,
                                                    const Models::Track& track,
                                                    const std::string& anlzPathHint) {
    BeatGridResult r;
    r.modeUsed = mode;

    auto fallbackFixed = [&](const char* reason) {
        r.ok = false;
        r.error = reason ? reason : "";
        r.modeUsed = BeatGridMode::Fixed;
        if (track.bpm <= 0) {
            r.error = "no BPM available";
            spdlog::warn("[BeatGridGen] mode {} unavailable and track has no BPM — no grid generated",
                         static_cast<int>(mode));
            return r;
        }
        BeatGrid g;
        const double bpm = track.bpm;
        g.bpm = bpm;
        const double interval = 60.0 / bpm;
        const double duration = (track.duration > 0) ? track.duration : 0.0;
        g.firstBeatOffset = 0.0;
        for (double t = 0.0; t < duration; t += interval) g.beatPositions.push_back(t);
        for (size_t i = 0; i < g.beatPositions.size(); i += g.beatsPerBar)
            g.barPositions.push_back(g.beatPositions[i]);
        r.grid = std::move(g);
        spdlog::warn("[BeatGridGen] mode {} unavailable ({}), falling back to Fixed",
                     static_cast<int>(mode), r.error);
        return r;
    };

    switch (mode) {
        case BeatGridMode::Manual: {
            // Une grille Manual démarre de la grille math Fixed, l'utilisateur l'édite ensuite
            if (track.bpm <= 0.0 || track.duration <= 0.0)
                return fallbackFixed("missing BPM or duration");
            BeatGrid g;
            g.bpm = track.bpm;
            const double interval = 60.0 / track.bpm;
            for (double t = 0.0; t < track.duration; t += interval) g.beatPositions.push_back(t);
            for (size_t i = 0; i < g.beatPositions.size(); i += g.beatsPerBar)
                g.barPositions.push_back(g.beatPositions[i]);
            r.grid = std::move(g);
            r.ok = true;
            spdlog::info("[BeatGridGen] Manual: seeded {} beats @ {:.1f} BPM",
                         r.grid.beatPositions.size(), r.grid.bpm);
            return r;
        }

        case BeatGridMode::Fixed: {
            if (track.bpm <= 0.0 || track.duration <= 0.0)
                return fallbackFixed("missing BPM or duration");
            BeatGrid g;
            g.bpm = track.bpm;
            const double interval = 60.0 / track.bpm;
            for (double t = 0.0; t < track.duration; t += interval) g.beatPositions.push_back(t);
            for (size_t i = 0; i < g.beatPositions.size(); i += g.beatsPerBar)
                g.barPositions.push_back(g.beatPositions[i]);
            r.grid = std::move(g);
            r.ok = true;
            return r;
        }

        case BeatGridMode::AI:
        case BeatGridMode::AIFlex: {
            auto audio = decodeForAnalysis(track.filePath);
            if (!audio || !audio->isLoaded())
                return fallbackFixed("audio decode failed");

            BeatEngine engine;
            BeatEngineOptions opts;
            if (mode == BeatGridMode::AIFlex) {
                opts.minBPM = 50.0;
                opts.maxBPM = 220.0;
                opts.allowVariableTempo = true;
            } else {
                opts.minBPM = 70.0;
                opts.maxBPM = 180.0;
                opts.allowVariableTempo = false;
            }
            BeatGridCore core = engine.analyze(*audio, opts);
            if (!core.ok || core.beats.empty())
                return fallbackFixed("AI detected no beats");

            BeatGrid g;
            g.bpm = core.bpm;
            g.firstBeatOffset = core.firstDownbeatSec;
            g.beatPositions = core.beats;
            g.barPositions = core.bars;
            g.beatsPerBar = core.beatsPerBarNum;
            r.grid = std::move(g);
            r.confidence = static_cast<float>(core.confidence);
            r.isVariableTempo = core.variableTempo;
            r.ok = true;
            spdlog::info("[BeatGridGen] {} -> {} beats, {:.2f} BPM, conf={:.0f}%, variable={}",
                         (mode == BeatGridMode::AIFlex) ? "AIFlex" : "AI",
                         r.grid.beatPositions.size(), r.grid.bpm,
                         r.confidence * 100.0f, r.isVariableTempo ? "yes" : "no");
            return r;
        }

        case BeatGridMode::Rekordbox: {
            std::string anlzPath = anlzPathHint;
            if (anlzPath.empty()) {
                anlzPath = findAnlzForTrackPath(track.filePath);
            }
            if (anlzPath.empty())
                return fallbackFixed("Rekordbox ANLZ not found for this track");
            auto grid = generateFromAnlz(anlzPath);
            if (grid.beatPositions.empty())
                return fallbackFixed("ANLZ PQTZ section empty");
            r.grid = std::move(grid);
            r.ok = true;
            return r;
        }
    }

    return fallbackFixed("unknown mode");
}

} // namespace BeatMate::Core
