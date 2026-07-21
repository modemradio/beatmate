#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDataProvider;

// Recherche de doublons par groupes (et non par paires), avec quatre criteres
// cumulables du plus sur au plus permissif.
class DuplicateScanner {
public:
    enum class Criterion {
        FileIdentical = 0,   // taille + empreinte des blocs = fichier identique
        AudioIdentical,      // duree + BPM + cle : meme enregistrement, encodage different
        Metadata,            // artiste + titre normalises
        Filename             // nom de fichier normalise
    };

    enum class KeepRule {
        BestQuality = 0,     // debit, puis duree, puis metadonnees
        MostComplete,        // metadonnees renseignees, puis debit
        Oldest,              // premier ajoute
        Newest,              // dernier ajoute
        ShortestPath         // chemin le plus court (racine plutot que sous-dossier)
    };

    struct Entry {
        Models::Track track;
        bool keep = false;
        bool checked = false;
        std::string reason;   // pourquoi ce fichier est propose a la suppression
    };

    struct Group {
        std::vector<Entry> entries;
        Criterion criterion = Criterion::Metadata;
        float confidence = 0.0f;
        std::string label;       // artiste - titre, ou nom de fichier
        int64_t wastedBytes = 0; // espace recuperable si on ne garde qu'un fichier
    };

    struct Options {
        bool useFileIdentical = true;
        bool useAudioIdentical = true;
        bool useMetadata = true;
        bool useFilename = false;
        double durationToleranceSec = 2.0;
        float titleThreshold = 0.88f;
        bool ignoreRemixTags = true;   // (Original Mix), [Remastered], feat. ...
        KeepRule keepRule = KeepRule::BestQuality;
    };

    using Progress = std::function<void(int done, int total, const std::string& phase)>;

    // Appele des qu'un groupe est constitue, depuis le thread de scan : permet de
    // remplir la liste au fil du balayage au lieu d'attendre la fin.
    using OnGroup = std::function<void(const Group&)>;

    explicit DuplicateScanner(TrackDataProvider* provider) : provider_(provider) {}

    std::vector<Group> scan(const Options& opts, const std::atomic<bool>& cancel,
                            Progress progress = nullptr, OnGroup onGroup = nullptr);

    static std::string criterionLabel(Criterion c);
    static std::string normalizeTitle(const std::string& s, bool stripRemixTags);
    static std::string fileSignature(const std::string& path, int64_t size);

    void applyKeepRule(Group& g, KeepRule rule) const;

private:
    TrackDataProvider* provider_ = nullptr;
};

} // namespace BeatMate::Services::Library
