#include "AggregatedTrendingFetcher.h"

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <thread>
#include <unordered_set>

namespace BeatMate::Services::Trending {

using Streaming::PlayCountEntry;

namespace {

static juce::String httpGet(const juce::String& url, int timeoutMs) {
    const juce::String headers =
        "User-Agent: BeatMate/11.0 (Windows; AggregatedTrending)\r\n"
        "Accept: application/json,text/html;q=0.9,*/*;q=0.5\r\n"
        "Accept-Language: en-US,en;q=0.8\r\n";
    juce::URL u(url);
    int statusCode = 0;
    auto stream = u.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(timeoutMs)
            .withExtraHeaders(headers)
            .withNumRedirectsToFollow(5)
            .withStatusCode(&statusCode));
    if (!stream) {
        spdlog::debug("AggregatedTrending: no stream url={}", url.toStdString());
        return {};
    }
    auto body = stream->readEntireStreamAsString();
    if (statusCode != 0 && (statusCode < 200 || statusCode >= 300)) {
        spdlog::debug("AggregatedTrending: HTTP {} url={}",
                      statusCode, url.toStdString());
        return {};
    }
    return body;
}

static std::string toLowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string normalizeForMatch(const std::string& in) {
    std::string s = toLowerAscii(in);
    s = std::regex_replace(s, std::regex(R"(\([^\)]*\))"), "");
    s = std::regex_replace(s, std::regex(R"(\[[^\]]*\])"), "");
    s = std::regex_replace(s, std::regex(R"(\bfeat\.?\b.*$)"), "");
    s = std::regex_replace(s, std::regex(R"(\bft\.?\b.*$)"), "");
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(c));
        else if (std::isspace(c)) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static int levenshtein(const std::string& a, const std::string& b) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    if (n == 0) return m;
    if (m == 0) return n;
    std::vector<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

static std::string fuzzyKey(const std::string& title, const std::string& artist) {
    return normalizeForMatch(title) + "|" + normalizeForMatch(artist);
}

static bool isDjFriendly(const std::string& genreRaw) {
    if (genreRaw.empty()) return true; // don't drop unknowns, they might be fine
    const auto g = toLowerAscii(genreRaw);
    static const char* kOk[] = {
        "electronic", "dance", "house", "techno", "trance", "drum",
        "bass", "dubstep", "edm", "hip-hop", "hip hop", "hiphop", "rap",
        "r&b", "r and b", "urban", "club", "funk", "disco",
        "pop" /* pop remixes are DJ-relevant */
    };
    for (const auto* k : kOk) if (g.find(k) != std::string::npos) return true;
    return false;
}

struct RawHit {
    std::string title;
    std::string artist;
    std::string genre;
    std::string artworkUrl;
    int position = 0;   // 1-based in-chart rank
    uint32_t source = 0;
};

static std::vector<RawHit> parseAppleMusicRSS(const std::string& cc, int timeoutMs) {
    const juce::StringArray bases = {
        "https://rss.marketingtools.apple.com/api/v2/",
        "https://rss.applemarketingtools.com/api/v2/"
    };
    const juce::StringArray feeds = {
        "/music/most-played/100/songs.json",
        "/music/top-songs/100/songs.json"
    };
    juce::String body;
    for (const auto& base : bases) {
        for (const auto& feed : feeds) {
            body = httpGet(base + juce::String(cc) + feed, timeoutMs);
            if (body.isNotEmpty()) break;
        }
        if (body.isNotEmpty()) break;
    }
    std::vector<RawHit> out;
    if (body.isEmpty()) return out;
    try {
        auto j = nlohmann::json::parse(body.toStdString());
        if (!j.contains("feed") || !j["feed"].contains("results")) return out;
        int pos = 1;
        for (const auto& it : j["feed"]["results"]) {
            RawHit h;
            h.source   = SRC_AppleMusicRSS;
            h.position = pos++;
            h.title    = it.value("name", "");
            h.artist   = it.value("artistName", "");
            if (it.contains("genres") && !it["genres"].empty())
                h.genre = it["genres"][0].value("name", "");
            h.artworkUrl = it.value("artworkUrl100", std::string{});
            out.push_back(std::move(h));
        }
        spdlog::info("AggregatedTrending: AppleMusicRSS OK {} cc={}", out.size(), cc);
    } catch (const std::exception& ex) {
        spdlog::warn("AggregatedTrending: AppleMusicRSS parse failed ({})", ex.what());
    }
    return out;
}

static std::vector<RawHit> parseItunesLegacy(const std::string& cc, int timeoutMs) {
    auto body = httpGet("https://itunes.apple.com/" + juce::String(cc)
                        + "/rss/topsongs/limit=100/json", timeoutMs);
    std::vector<RawHit> out;
    if (body.isEmpty()) return out;
    try {
        auto j = nlohmann::json::parse(body.toStdString());
        if (!j.contains("feed") || !j["feed"].contains("entry")) return out;
        int pos = 1;
        for (const auto& e0 : j["feed"]["entry"]) {
            RawHit h;
            h.source   = SRC_ItunesLegacy;
            h.position = pos++;
            if (e0.contains("im:name"))   h.title  = e0["im:name"].value("label", "");
            if (e0.contains("im:artist")) h.artist = e0["im:artist"].value("label", "");
            if (e0.contains("category") && e0["category"].contains("attributes"))
                h.genre = e0["category"]["attributes"].value("label", "");
            if (e0.contains("im:image") && e0["im:image"].is_array()
                && !e0["im:image"].empty()) {
                const auto& imgs = e0["im:image"];
                h.artworkUrl = imgs[imgs.size() - 1].value("label", std::string{});
            }
            out.push_back(std::move(h));
        }
        spdlog::info("AggregatedTrending: ItunesLegacy OK {} cc={}", out.size(), cc);
    } catch (const std::exception& ex) {
        spdlog::warn("AggregatedTrending: ItunesLegacy parse failed ({})", ex.what());
    }
    return out;
}

static std::vector<RawHit> parseDeezer(const std::string& /*cc*/, int timeoutMs) {
    auto body = httpGet("https://api.deezer.com/chart/0/tracks?limit=100", timeoutMs);
    std::vector<RawHit> out;
    if (body.isEmpty()) return out;
    try {
        auto j = nlohmann::json::parse(body.toStdString());
        if (!j.contains("data")) return out;
        int pos = 1;
        for (const auto& it : j["data"]) {
            RawHit h;
            h.source   = SRC_Deezer;
            h.position = pos++;
            h.title    = it.value("title", "");
            if (it.contains("artist") && it["artist"].is_object())
                h.artist = it["artist"].value("name", "");
            if (it.contains("album") && it["album"].is_object()) {
                const auto& alb = it["album"];
                if (alb.contains("cover_medium") && alb["cover_medium"].is_string())
                    h.artworkUrl = alb.value("cover_medium", std::string{});
                else
                    h.artworkUrl = alb.value("cover", std::string{});
            }
            out.push_back(std::move(h));
        }
        spdlog::info("AggregatedTrending: Deezer OK {} entries", out.size());
    } catch (const std::exception& ex) {
        spdlog::warn("AggregatedTrending: Deezer parse failed ({})", ex.what());
    }
    return out;
}

static std::vector<RawHit> parseKworbSpotifyPage(const juce::String& url, uint32_t srcFlag,
                                                 int timeoutMs) {
    auto body = httpGet(url, timeoutMs);
    std::vector<RawHit> out;
    if (body.isEmpty()) return out;
    try {
        const std::string src = body.toStdString();
        std::regex row(R"re(<td class="text mp"><div><a href="\.\./artist/[^"]*">([^<]+)</a> - <a href="\.\./track/[^"]*">([^<]+)</a>)re");
        auto begin = std::sregex_iterator(src.begin(), src.end(), row);
        auto end   = std::sregex_iterator();
        int pos = 1;
        for (auto it = begin; it != end && pos <= 100; ++it) {
            RawHit h;
            h.source   = srcFlag;
            h.position = pos++;
            h.artist   = (*it)[1].str();
            h.title    = (*it)[2].str();
            if (!h.title.empty() && !h.artist.empty())
                out.push_back(std::move(h));
        }
    } catch (const std::exception& ex) {
        spdlog::warn("AggregatedTrending: kworb parse failed ({}) — skipped", ex.what());
        return {};
    }
    return out;
}

static std::vector<RawHit> parseKworbSpotifyCountry(const std::string& cc, int timeoutMs) {
    std::vector<RawHit> out;
    if (!cc.empty() && cc != "global") {
        out = parseKworbSpotifyPage(
            juce::String("https://kworb.net/spotify/country/") + cc + "_daily.html",
            SRC_Spotify, timeoutMs);
    }
    if (out.empty()) {
        out = parseKworbSpotifyPage("https://kworb.net/spotify/country/global_daily.html",
                                    SRC_Spotify, timeoutMs);
    }
    spdlog::info("AggregatedTrending: Spotify(kworb) OK {} entries (cc={})", out.size(), cc);
    return out;
}

static std::vector<RawHit> parseKworbSpotifyGlobal(int timeoutMs) {
    auto out = parseKworbSpotifyPage("https://kworb.net/spotify/country/global_daily.html",
                                     SRC_SpotifyGlobal, timeoutMs);
    spdlog::info("AggregatedTrending: Spotify global(kworb) OK {} entries", out.size());
    return out;
}

struct ItunesSearchHit {
    std::string previewUrl;
    int durationMs = 0;
    std::string artworkUrl;
    std::string genre;
};

static std::optional<ItunesSearchHit> enrichItunes(const std::string& title,
                                                    const std::string& artist,
                                                    int timeoutMs) {
    const juce::String q = juce::URL::addEscapeChars(
        juce::String(title) + " " + juce::String(artist), true);
    auto body = httpGet("https://itunes.apple.com/search?term=" + q
                        + "&entity=song&limit=1", timeoutMs);
    if (body.isEmpty()) return std::nullopt;
    try {
        auto j = nlohmann::json::parse(body.toStdString());
        if (!j.contains("results") || j["results"].empty()) return std::nullopt;
        const auto& r = j["results"][0];
        ItunesSearchHit h;
        h.previewUrl = r.value("previewUrl", std::string{});
        h.durationMs = r.value("trackTimeMillis", 0);
        h.artworkUrl = r.value("artworkUrl100", std::string{});
        h.genre      = r.value("primaryGenreName", std::string{});
        return h;
    } catch (...) {}
    return std::nullopt;
}

} // namespace

AggregatedTrendingFetcher::AggregatedTrendingFetcher() = default;

const char* AggregatedTrendingFetcher::sourceName(uint32_t flag) {
    switch (flag) {
        case SRC_AppleMusicRSS:  return "Apple Music";
        case SRC_ItunesLegacy:   return "iTunes";
        case SRC_Deezer:         return "Deezer";
        case SRC_SpotifyGlobal:  return "Spotify Monde";
        case SRC_SoundCloud:     return "SoundCloud";
        case SRC_Spotify:        return "Spotify";
    }
    return "?";
}

void AggregatedTrendingFetcher::clearCache() {
    std::lock_guard<std::mutex> lk(cacheMutex_);
    cache_.clear();
}

std::vector<Streaming::PlayCountEntry>
AggregatedTrendingFetcher::toPlayCountEntries(const std::vector<AggregatedEntry>& agg) {
    std::vector<PlayCountEntry> out;
    out.reserve(agg.size());
    int64_t fakeId = -1;
    for (const auto& a : agg) {
        PlayCountEntry e = a.base;
        if (e.trackId == 0) e.trackId = fakeId--;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<AggregatedEntry>
AggregatedTrendingFetcher::fetch(const FetchOptions& opts) {
    const std::string cacheKey = opts.country + "|" + opts.genre
                               + (opts.electronicOnly ? "|E" : "|A");
    if (!opts.forceRefresh) {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = cache_.find(cacheKey);
        if (it != cache_.end()) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - it->second.at).count();
            if (age < opts.cacheTtlSec) {
                spdlog::info("AggregatedTrending: cache hit key='{}' age={}s",
                             cacheKey, age);
                return it->second.data;
            }
        }
    } else {
        spdlog::info("AggregatedTrending: forceRefresh — skipping cache for '{}'", cacheKey);
    }

    std::vector<RawHit> apple, itunes, deezer, spotifyCc, spotifyGlobal;
    std::thread tApple([&] { apple         = parseAppleMusicRSS(opts.country, opts.httpTimeoutMs); });
    std::thread tIt   ([&] { itunes        = parseItunesLegacy(opts.country, opts.httpTimeoutMs); });
    std::thread tDz   ([&] { deezer        = parseDeezer(opts.country, opts.httpTimeoutMs); });
    std::thread tSp   ([&] { spotifyCc     = parseKworbSpotifyCountry(opts.country, opts.httpTimeoutMs); });
    std::thread tSpG  ([&] { spotifyGlobal = parseKworbSpotifyGlobal(opts.httpTimeoutMs); });
    tApple.join(); tIt.join(); tDz.join(); tSp.join(); tSpG.join();

    struct Acc {
        AggregatedEntry entry;
        int bestPosition = 999; // lowest rank seen across sources
    };
    std::vector<Acc> buckets;
    buckets.reserve(apple.size() + itunes.size() + deezer.size()
                    + spotifyCc.size() + spotifyGlobal.size());

    auto addHit = [&](const RawHit& h) {
        if (h.title.empty() || h.artist.empty()) return;
        const std::string k = fuzzyKey(h.title, h.artist);

        int foundIdx = -1;
        for (int i = 0; i < static_cast<int>(buckets.size()); ++i) {
            const std::string existingKey =
                fuzzyKey(buckets[i].entry.base.title, buckets[i].entry.base.artist);
            if (existingKey == k) { foundIdx = i; break; }
        }
        if (foundIdx < 0) {
            const std::string nt = normalizeForMatch(h.title);
            const std::string na = normalizeForMatch(h.artist);
            for (int i = 0; i < static_cast<int>(buckets.size()); ++i) {
                const std::string et = normalizeForMatch(buckets[i].entry.base.title);
                const std::string ea = normalizeForMatch(buckets[i].entry.base.artist);
                if (std::abs((int)nt.size() - (int)et.size()) <= 3
                    && std::abs((int)na.size() - (int)ea.size()) <= 3
                    && levenshtein(nt, et) <= 2
                    && levenshtein(na, ea) <= 2) {
                    foundIdx = i; break;
                }
            }
        }
        if (foundIdx < 0) {
            Acc a;
            a.entry.base.title  = h.title;
            a.entry.base.artist = h.artist;
            a.entry.base.genre  = h.genre;
            a.entry.base.artworkUrl = h.artworkUrl;
            a.entry.sourceMask  = h.source;
            a.entry.sourceCount = 1;
            a.bestPosition      = h.position > 0 ? h.position : 100;
            buckets.push_back(std::move(a));
        } else {
            auto& a = buckets[foundIdx];
            if ((a.entry.sourceMask & h.source) == 0) {
                a.entry.sourceMask |= h.source;
                a.entry.sourceCount += 1;
            }
            if (h.position > 0 && h.position < a.bestPosition)
                a.bestPosition = h.position;
            if (a.entry.base.genre.empty() && !h.genre.empty())
                a.entry.base.genre = h.genre;
            if (a.entry.base.artworkUrl.empty() && !h.artworkUrl.empty())
                a.entry.base.artworkUrl = h.artworkUrl;
        }
    };

    for (const auto& h : apple)         addHit(h);
    for (const auto& h : itunes)        addHit(h);
    for (const auto& h : deezer)        addHit(h);
    for (const auto& h : spotifyCc)     addHit(h);
    for (const auto& h : spotifyGlobal) addHit(h);

    for (auto& a : buckets) {
        const double presence = static_cast<double>(a.entry.sourceCount);
        const double recency  = std::max(0.0, 1.0 - (a.bestPosition - 1) / 100.0);
        a.entry.aggregateScore = presence * 2.0 + recency;
        a.entry.base.trendScore = a.entry.aggregateScore;
    }

    if (opts.electronicOnly) {
        buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
            [](const Acc& a) { return !isDjFriendly(a.entry.base.genre); }),
            buckets.end());
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const Acc& x, const Acc& y) {
                  if (x.entry.aggregateScore != y.entry.aggregateScore)
                      return x.entry.aggregateScore > y.entry.aggregateScore;
                  return x.bestPosition < y.bestPosition;
              });

    std::vector<AggregatedEntry> result;
    result.reserve(std::min<size_t>(buckets.size(),
                                    static_cast<size_t>(opts.maxEntries)));
    for (auto& a : buckets) {
        if (static_cast<int>(result.size()) >= opts.maxEntries) break;
        // Stable fake-id so downstream lookups work.
        a.entry.base.trackId = -static_cast<int64_t>(result.size() + 1);
        result.push_back(std::move(a.entry));
    }

    // Enrichissement limité au top 50 pour la latence.
    if (opts.enrichPreview) {
        const int enrichMax = std::min<int>(50, static_cast<int>(result.size()));
        std::atomic<int> idx{0};
        auto worker = [&] {
            for (;;) {
                int i = idx.fetch_add(1);
                if (i >= enrichMax) return;
                auto h = enrichItunes(result[i].base.title,
                                      result[i].base.artist,
                                      opts.httpTimeoutMs);
                if (!h) continue;
                result[i].previewUrl = h->previewUrl;
                result[i].durationMs = h->durationMs;
                if (result[i].base.artworkUrl.empty())
                    result[i].base.artworkUrl = h->artworkUrl;
                if (result[i].base.genre.empty())
                    result[i].base.genre = h->genre;
            }
        };
        std::thread w1(worker), w2(worker), w3(worker), w4(worker);
        w1.join(); w2.join(); w3.join(); w4.join();
    }

    const int64_t fetchedAt = juce::Time::currentTimeMillis();
    for (auto& e : result) e.fetchedAtUnixMs = fetchedAt;

    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        CacheBucket b;
        b.at = std::chrono::steady_clock::now();
        b.data = result;
        cache_[cacheKey] = std::move(b);
    }

    spdlog::info("AggregatedTrending: merged {} entries from {} apple / {} itunes / "
                 "{} deezer / {} spotify-cc / {} spotify-global (cc={})",
                 result.size(), apple.size(), itunes.size(), deezer.size(),
                 spotifyCc.size(), spotifyGlobal.size(), opts.country);
    return result;
}

} // namespace BeatMate::Services::Trending
