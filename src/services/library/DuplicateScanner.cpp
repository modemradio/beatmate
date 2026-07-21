#include "DuplicateScanner.h"
#include "TrackDataProvider.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_map>

namespace BeatMate::Services::Library {

namespace {

const char* kNoiseTokens[] = {
    "original mix", "originalmix", "radio edit", "radioedit", "extended mix",
    "extended version", "club mix", "remastered", "remaster", "explicit",
    "clean version", "album version", "single version", "hq", "hd", "official",
    "official video", "official audio", "lyrics", "lyric video", "free download",
    "bonus track", "128kbps", "192kbps", "320kbps", "www", "mp3"
};

std::string lowerAscii(const std::string& in)
{
    auto s = juce::String::fromUTF8(in.c_str()).toLowerCase();
    juce::String out;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        // Repli des accents pour comparer "remixe" et "remixé".
        if      (c == 0xe0 || c == 0xe1 || c == 0xe2 || c == 0xe4) out << 'a';
        else if (c == 0xe8 || c == 0xe9 || c == 0xea || c == 0xeb) out << 'e';
        else if (c == 0xee || c == 0xef) out << 'i';
        else if (c == 0xf4 || c == 0xf6) out << 'o';
        else if (c == 0xf9 || c == 0xfb || c == 0xfc) out << 'u';
        else if (c == 0xe7) out << 'c';
        else out << c;
    }
    return out.toStdString();
}

int levenshtein(const std::string& a, const std::string& b)
{
    const size_t n = a.size(), m = b.size();
    if (n == 0) return (int) m;
    if (m == 0) return (int) n;
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = (int) j;
    for (size_t i = 1; i <= n; ++i)
    {
        cur[0] = (int) i;
        for (size_t j = 1; j <= m; ++j)
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1,
                                prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        prev.swap(cur);
    }
    return prev[m];
}

float similarity(const std::string& a, const std::string& b)
{
    if (a.empty() && b.empty()) return 1.0f;
    const size_t longest = std::max(a.size(), b.size());
    if (longest == 0) return 1.0f;
    return 1.0f - (float) levenshtein(a, b) / (float) longest;
}

int metadataScore(const Models::Track& t)
{
    int n = 0;
    if (! t.title.empty())      ++n;
    if (! t.artist.empty())     ++n;
    if (! t.album.empty())      ++n;
    if (! t.genre.empty())      ++n;
    if (t.year > 0)             ++n;
    if (t.bpm > 0.0)            ++n;
    if (! t.key.empty())        ++n;
    if (! t.camelotKey.empty()) ++n;
    if (! t.comment.empty())    ++n;
    if (! t.label.empty())      ++n;
    if (t.energy > 0)           ++n;
    if (t.analyzed)             ++n;
    return n;
}

} // namespace

std::string DuplicateScanner::criterionLabel(Criterion c)
{
    switch (c)
    {
        case Criterion::FileIdentical:  return "Fichier identique";
        case Criterion::AudioIdentical: return "Audio identique";
        case Criterion::Metadata:       return "Artiste + titre";
        case Criterion::Filename:       return "Nom de fichier";
    }
    return "";
}

std::string DuplicateScanner::normalizeTitle(const std::string& s, bool stripRemixTags)
{
    auto low = lowerAscii(s);
    juce::String t = juce::String::fromUTF8(low.c_str());

    if (stripRemixTags)
    {
        // Retire les mentions entre parentheses ou crochets qui ne changent pas
        for (const char* noise : kNoiseTokens)
            t = t.replace(juce::String(noise), " ");
        t = t.replace("feat.", " ").replace("feat", " ").replace("ft.", " ");
    }

    juce::String out;
    for (int i = 0; i < t.length(); ++i)
    {
        const auto c = t[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out << c;
        else out << ' ';
    }
    return out.trim().removeCharacters(" ").toStdString();
}

std::string DuplicateScanner::fileSignature(const std::string& path, int64_t size)
{
    juce::File f(juce::String::fromUTF8(path.c_str()));
    if (! f.existsAsFile()) return {};

    juce::FileInputStream in(f);
    if (! in.openedOk()) return {};

    // Trois blocs de 64 Ko (debut, milieu, fin) : discriminant et rapide,
    const int block = 64 * 1024;
    const int64_t offsets[3] = { 0, juce::jmax((int64_t) 0, size / 2 - block / 2),
                                 juce::jmax((int64_t) 0, size - block) };
    // FNV-1a 64 bits sur les trois blocs : pas de dependance crypto, et la
    // taille exacte est concatenee a l'empreinte (collision quasi impossible).
    uint64_t h = 1469598103934665603ULL;
    juce::HeapBlock<char> buf(block);
    int totalRead = 0;
    for (int64_t off : offsets)
    {
        in.setPosition(off);
        const int got = in.read(buf.get(), block);
        for (int i = 0; i < got; ++i)
        {
            h ^= (uint64_t) (unsigned char) buf[i];
            h *= 1099511628211ULL;
        }
        totalRead += got;
    }
    if (totalRead == 0) return {};

    return (juce::String(size) + "-" + juce::String::toHexString((juce::int64) h)).toStdString();
}

void DuplicateScanner::applyKeepRule(Group& g, KeepRule rule) const
{
    if (g.entries.empty()) return;

    auto better = [rule](const Models::Track& a, const Models::Track& b) {
        switch (rule)
        {
            case KeepRule::BestQuality:
                if (a.bitRate != b.bitRate) return a.bitRate > b.bitRate;
                if (std::abs(a.duration - b.duration) > 0.5) return a.duration > b.duration;
                return metadataScore(a) > metadataScore(b);
            case KeepRule::MostComplete:
            {
                const int sa = metadataScore(a), sb = metadataScore(b);
                if (sa != sb) return sa > sb;
                return a.bitRate > b.bitRate;
            }
            case KeepRule::Oldest:  return a.id < b.id;
            case KeepRule::Newest:  return a.id > b.id;
            case KeepRule::ShortestPath:
                if (a.filePath.size() != b.filePath.size())
                    return a.filePath.size() < b.filePath.size();
                return a.bitRate > b.bitRate;
        }
        return false;
    };

    size_t best = 0;
    for (size_t i = 1; i < g.entries.size(); ++i)
        if (better(g.entries[i].track, g.entries[best].track)) best = i;

    const auto& kept = g.entries[best].track;
    g.wastedBytes = 0;
    for (size_t i = 0; i < g.entries.size(); ++i)
    {
        auto& e = g.entries[i];
        e.keep = (i == best);
        e.checked = ! e.keep;
        if (e.keep) { e.reason = "conserve"; continue; }

        g.wastedBytes += e.track.fileSize;
        if (e.track.bitRate > 0 && kept.bitRate > e.track.bitRate)
            e.reason = std::to_string(e.track.bitRate) + " kbps contre "
                     + std::to_string(kept.bitRate) + " kbps";
        else if (kept.duration - e.track.duration > 1.0)
            e.reason = "plus court de "
                     + std::to_string((int) (kept.duration - e.track.duration)) + " s";
        else if (metadataScore(kept) > metadataScore(e.track))
            e.reason = "moins de metadonnees";
        else
            e.reason = "copie";
    }
}

std::vector<DuplicateScanner::Group> DuplicateScanner::scan(const Options& opts,
                                                            const std::atomic<bool>& cancel,
                                                            Progress progress,
                                                            OnGroup onGroup)
{
    std::vector<Group> groups;
    if (! provider_) return groups;

    auto tracks = provider_->getAllTracks();
    const int total = (int) tracks.size();
    if (total == 0) return groups;

    std::vector<bool> taken(tracks.size(), false);

    auto emit = [&](std::vector<size_t> idx, Criterion crit, float conf) {
        if (idx.size() < 2) return;
        Group g;
        g.criterion = crit;
        g.confidence = conf;
        for (size_t i : idx)
        {
            taken[i] = true;
            Entry e;
            e.track = tracks[i];
            g.entries.push_back(std::move(e));
        }
        const auto& first = g.entries.front().track;
        g.label = (first.artist.empty() ? std::string() : first.artist + " - ")
                + (first.title.empty()
                       ? juce::File(juce::String::fromUTF8(first.filePath.c_str()))
                             .getFileName().toStdString()
                       : first.title);
        applyKeepRule(g, opts.keepRule);
        if (onGroup) onGroup(g);
        groups.push_back(std::move(g));
    };

    // 1. Fichier identique : pre-groupe par taille, puis empreinte des blocs.
    if (opts.useFileIdentical)
    {
        std::map<int64_t, std::vector<size_t>> bySize;
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].fileSize > 0) bySize[tracks[i].fileSize].push_back(i);

        int done = 0;
        for (const auto& kv : bySize)
        {
            if (cancel.load()) return groups;
            if (kv.second.size() < 2) { done += (int) kv.second.size(); continue; }

            std::map<std::string, std::vector<size_t>> bySig;
            for (size_t i : kv.second)
            {
                if (cancel.load()) return groups;
                auto sig = fileSignature(tracks[i].filePath, tracks[i].fileSize);
                if (! sig.empty()) bySig[sig].push_back(i);
                if (progress && (++done % 25) == 0)
                    progress(done, total, "Empreintes de fichiers");
            }
            for (const auto& s : bySig) emit(s.second, Criterion::FileIdentical, 1.0f);
        }
    }

    // 2. Audio identique : meme duree (a la tolerance pres), meme BPM, meme cle.
    if (opts.useAudioIdentical)
    {
        if (progress) progress(0, total, "Comparaison audio");
        std::map<std::string, std::vector<size_t>> byAudio;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            if (taken[i] || tracks[i].duration <= 0.0) continue;
            const int bucket = (int) (tracks[i].duration / juce::jmax(1.0, opts.durationToleranceSec));
            std::string k = std::to_string(bucket);
            if (tracks[i].bpm > 0.0) k += "|" + std::to_string((int) (tracks[i].bpm + 0.5));
            if (! tracks[i].camelotKey.empty()) k += "|" + tracks[i].camelotKey;
            byAudio[k].push_back(i);
        }
        for (const auto& kv : byAudio)
        {
            if (cancel.load()) return groups;
            if (kv.second.size() < 2) continue;
            // Verification fine de la duree a l'interieur du seau.
            std::vector<size_t> cluster;
            for (size_t i : kv.second)
            {
                if (taken[i]) continue;
                if (cluster.empty()
                    || std::abs(tracks[cluster.front()].duration - tracks[i].duration)
                           <= opts.durationToleranceSec)
                    cluster.push_back(i);
            }
            emit(cluster, Criterion::AudioIdentical, 0.9f);
        }
    }

    // 3. Metadonnees : artiste + titre normalises, avec rattrapage par similarite.
    if (opts.useMetadata)
    {
        if (progress) progress(0, total, "Comparaison des tags");
        std::map<std::string, std::vector<size_t>> byMeta;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            if (taken[i]) continue;
            if (tracks[i].title.empty() && tracks[i].artist.empty()) continue;
            const auto k = normalizeTitle(tracks[i].artist, opts.ignoreRemixTags) + "|"
                         + normalizeTitle(tracks[i].title, opts.ignoreRemixTags);
            if (k == "|") continue;
            byMeta[k].push_back(i);
        }
        for (const auto& kv : byMeta)
        {
            if (cancel.load()) return groups;
            std::vector<size_t> cluster;
            for (size_t i : kv.second) if (! taken[i]) cluster.push_back(i);
            emit(cluster, Criterion::Metadata, 0.85f);
        }

        // Rattrapage : titres proches mais pas identiques (fautes, suffixes).
        // Les titres et artistes normalises sont precalcules une seule fois : la
        // boucle en O(n^2) ne fait plus que comparer des chaines deja pretes au
        // lieu de renormaliser a chaque paire, ce qui etait l'essentiel du cout
        // de cette phase (une allocation juce::String par comparaison).
        std::vector<size_t> rest;
        for (size_t i = 0; i < tracks.size(); ++i)
            if (! taken[i] && ! tracks[i].title.empty()) rest.push_back(i);

        std::vector<std::string> normTitle(rest.size());
        std::vector<std::string> normArtist(rest.size());
        for (size_t r = 0; r < rest.size(); ++r)
        {
            normTitle[r]  = normalizeTitle(tracks[rest[r]].title, opts.ignoreRemixTags);
            normArtist[r] = normalizeTitle(tracks[rest[r]].artist, opts.ignoreRemixTags);
        }

        for (size_t a = 0; a < rest.size(); ++a)
        {
            if (cancel.load()) return groups;
            if (taken[rest[a]]) continue;
            std::vector<size_t> cluster{ rest[a] };
            const std::string& ka = normTitle[a];
            const std::string& aa = normArtist[a];
            for (size_t b = a + 1; b < rest.size(); ++b)
            {
                if (taken[rest[b]]) continue;
                const std::string& kb = normTitle[b];
                if (std::abs((int) ka.size() - (int) kb.size()) > 6) continue;
                const std::string& ab = normArtist[b];
                if (similarity(ka, kb) >= opts.titleThreshold
                    && (aa.empty() || ab.empty() || similarity(aa, ab) >= 0.75f))
                    cluster.push_back(rest[b]);
            }
            if (cluster.size() >= 2) emit(cluster, Criterion::Metadata, 0.7f);
        }
    }

    // 4. Nom de fichier : dernier filet, souvent bruyant.
    if (opts.useFilename)
    {
        if (progress) progress(0, total, "Comparaison des noms de fichiers");
        std::map<std::string, std::vector<size_t>> byName;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            if (taken[i]) continue;
            const auto stem = juce::File(juce::String::fromUTF8(tracks[i].filePath.c_str()))
                                  .getFileNameWithoutExtension().toStdString();
            const auto k = normalizeTitle(stem, opts.ignoreRemixTags);
            if (k.empty()) continue;
            byName[k].push_back(i);
        }
        for (const auto& kv : byName)
        {
            if (cancel.load()) return groups;
            std::vector<size_t> cluster;
            for (size_t i : kv.second) if (! taken[i]) cluster.push_back(i);
            emit(cluster, Criterion::Filename, 0.6f);
        }
    }

    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        if (a.confidence != b.confidence) return a.confidence > b.confidence;
        return a.wastedBytes > b.wastedBytes;
    });

    spdlog::info("[DuplicateScanner] {} groupes sur {} pistes", groups.size(), total);
    return groups;
}

} // namespace BeatMate::Services::Library
