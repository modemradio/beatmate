#include "ChartsService.h"
#include "StreamingServiceBase.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <regex>
#include <set>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {

namespace {

const std::map<std::string, const char*>& deezerChartPlaylists() {
    static const std::map<std::string, const char*> m = {
        { "global", "3155776842" },
        { "fr",     "1109890291" },
        { "us",     "1313621735" },
        { "gb",     "1111142221" },
        { "de",     "1111143121" },
        { "es",     "1116190041" },
        { "it",     "1116187241" },
        { "jp",     "1362508955" },
        { "br",     "1111141961" },
    };
    return m;
}

std::string jstr(const json& o, const char* key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

std::string todayIso() {
    return juce::Time::getCurrentTime().formatted("%Y-%m-%d").toStdString();
}

std::string parseRfcFeedDate(const std::string& updated) {
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    juce::StringArray tok;
    tok.addTokens(juce::String(updated).replaceCharacter(',', ' '), true);
    int day = 0, mon = 0, year = 0;
    for (const auto& t : tok) {
        if (t.containsOnly("0123456789")) {
            if (t.length() == 4) year = t.getIntValue();
            else if (day == 0 && t.getIntValue() >= 1 && t.getIntValue() <= 31)
                day = t.getIntValue();
        } else {
            for (int m = 0; m < 12; ++m)
                if (t.startsWithIgnoreCase(months[m])) { mon = m + 1; break; }
        }
    }
    if (day <= 0 || mon <= 0 || year <= 0) return {};
    return juce::String::formatted("%04d-%02d-%02d", year, mon, day).toStdString();
}

std::string decodeHtmlEntities(std::string s) {
    struct Pair { const char* from; const char* to; };
    static const Pair pairs[] = {
        { "&amp;", "&" }, { "&#39;", "'" }, { "&quot;", "\"" },
        { "&lt;", "<" }, { "&gt;", ">" },
    };
    for (const auto& p : pairs) {
        size_t pos = 0;
        const size_t flen = std::strlen(p.from);
        while ((pos = s.find(p.from, pos)) != std::string::npos) {
            s.replace(pos, flen, p.to);
            pos += std::strlen(p.to);
        }
    }
    return s;
}

} // namespace

ChartsService& ChartsService::instance() {
    static ChartsService inst;
    return inst;
}

std::string ChartsService::normalizeKey(const std::string& text) {
    juce::String s = juce::String::fromUTF8(text.c_str()).toLowerCase();
    juce::String folded;
    folded.preallocateBytes(static_cast<size_t>(s.length()) * 2 + 8);
    for (auto t = s.getCharPointer(); !t.isEmpty(); ++t) {
        const juce::juce_wchar c = *t;
        const char* repl = nullptr;
        switch (c) {
            case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: repl = "a"; break;
            case 0xe7: repl = "c"; break;
            case 0xe8: case 0xe9: case 0xea: case 0xeb: repl = "e"; break;
            case 0xec: case 0xed: case 0xee: case 0xef: repl = "i"; break;
            case 0xf1: repl = "n"; break;
            case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf8: repl = "o"; break;
            case 0xf9: case 0xfa: case 0xfb: case 0xfc: repl = "u"; break;
            case 0xfd: case 0xff: repl = "y"; break;
            case 0xe6: repl = "ae"; break;
            case 0x153: repl = "oe"; break;
            default: break;
        }
        if (repl != nullptr) folded << repl;
        else folded << juce::String::charToString(c);
    }
    std::string out;
    out.reserve(static_cast<size_t>(folded.length()));
    int depth = 0;
    for (auto t = folded.getCharPointer(); !t.isEmpty(); ++t) {
        const juce::juce_wchar c = *t;
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; continue; }
        if (depth > 0) continue;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))
            out.push_back(static_cast<char>(c));
        else if (!out.empty() && out.back() != ' ')
            out.push_back(' ');
    }
    for (const char* cut : { " feat ", " ft ", " featuring ", " avec " }) {
        const auto p = out.find(cut);
        if (p != std::string::npos) out.erase(p);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string ChartsService::matchKey(const std::string& title, const std::string& artist) {
    return normalizeKey(title) + "|" + normalizeKey(artist);
}

juce::File ChartsService::cacheFileFor(const std::string& country) const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate").getChildFile("charts")
        .getChildFile("chart_" + juce::String(country) + ".json");
}

LiveChart ChartsService::readCacheLocked(const std::string& country) {
    auto it = memCache_.find(country);
    if (it != memCache_.end()) return it->second;

    LiveChart c;
    c.country = country;
    const auto f = cacheFileFor(country);
    if (!f.existsAsFile()) return c;
    try {
        auto j = json::parse(f.loadFileAsString().toStdString());
        c.source      = j.value("source", std::string{});
        c.chartDate   = j.value("chartDate", std::string{});
        c.fetchedAtMs = j.value("fetchedAtMs", static_cast<int64_t>(0));
        if (j.contains("entries") && j["entries"].is_array()) {
            for (const auto& e0 : j["entries"]) {
                LiveChartEntry e;
                e.position         = e0.value("position", 0);
                e.previousPosition = e0.value("previousPosition", 0);
                e.delta            = e0.value("delta", 0);
                e.isNew            = e0.value("isNew", false);
                e.title            = jstr(e0, "title");
                e.artist           = jstr(e0, "artist");
                e.artworkUrl       = jstr(e0, "artworkUrl");
                e.previewUrl       = jstr(e0, "previewUrl");
                if (!e.title.empty() && !e.artist.empty())
                    c.entries.push_back(std::move(e));
            }
        }
        memCache_[country] = c;
    } catch (const std::exception& ex) {
        spdlog::warn("ChartsService: cache illisible pour {} ({})", country, ex.what());
        c.entries.clear();
    }
    return c;
}

void ChartsService::writeCacheLocked(const LiveChart& chart) {
    try {
        json j;
        j["country"]     = chart.country;
        j["source"]      = chart.source;
        j["chartDate"]   = chart.chartDate;
        j["fetchedAtMs"] = chart.fetchedAtMs;
        j["entries"]     = json::array();
        for (const auto& e : chart.entries) {
            json e0;
            e0["position"]         = e.position;
            e0["previousPosition"] = e.previousPosition;
            e0["delta"]            = e.delta;
            e0["isNew"]            = e.isNew;
            e0["title"]            = e.title;
            e0["artist"]           = e.artist;
            e0["artworkUrl"]       = e.artworkUrl;
            e0["previewUrl"]       = e.previewUrl;
            j["entries"].push_back(std::move(e0));
        }
        const auto f = cacheFileFor(chart.country);
        f.getParentDirectory().createDirectory();
        f.replaceWithText(juce::String(j.dump()));
    } catch (const std::exception& ex) {
        spdlog::warn("ChartsService: ecriture cache impossible ({})", ex.what());
    }
}

bool ChartsService::fetchDeezer(const std::string& country, LiveChart& out) {
    const auto& ids = deezerChartPlaylists();
    auto it = ids.find(country);
    if (it == ids.end()) return false;

    auto resp = StreamingServiceBase::httpGet(
        std::string("https://api.deezer.com/playlist/") + it->second, {});
    if (!resp.ok() || resp.body.empty()) {
        spdlog::warn("ChartsService: Deezer HTTP {} pour {}", resp.status, country);
        return false;
    }
    try {
        auto j = json::parse(resp.body);
        if (!j.contains("tracks") || !j["tracks"].is_object()
            || !j["tracks"].contains("data") || !j["tracks"]["data"].is_array())
            return false;
        const std::string mod = jstr(j, "mod_date");
        out.chartDate = mod.size() >= 10 ? mod.substr(0, 10) : todayIso();
        out.source = "Deezer";
        int pos = 1;
        for (const auto& tr : j["tracks"]["data"]) {
            LiveChartEntry e;
            e.position = pos++;
            e.title = jstr(tr, "title");
            if (tr.contains("artist") && tr["artist"].is_object())
                e.artist = jstr(tr["artist"], "name");
            e.previewUrl = jstr(tr, "preview");
            if (tr.contains("album") && tr["album"].is_object())
                e.artworkUrl = jstr(tr["album"], "cover_medium");
            if (!e.title.empty() && !e.artist.empty())
                out.entries.push_back(std::move(e));
        }
    } catch (const std::exception& ex) {
        spdlog::warn("ChartsService: parse Deezer KO ({})", ex.what());
        out.entries.clear();
        return false;
    }
    spdlog::info("ChartsService: Deezer {} -> {} entrees (chart du {})",
                 country, out.entries.size(), out.chartDate);
    return out.entries.size() >= 10;
}

bool ChartsService::fetchItunesRss(const std::string& country, LiveChart& out) {
    if (country == "global") return false;
    auto resp = StreamingServiceBase::httpGet(
        "https://rss.marketingtools.apple.com/api/v2/" + country
            + "/music/most-played/100/songs.json",
        "User-Agent: BeatMate/12.0 (Charts)\r\n");
    if (!resp.ok() || resp.body.empty()) {
        spdlog::warn("ChartsService: iTunes RSS HTTP {} pour {}", resp.status, country);
        return false;
    }
    try {
        auto j = json::parse(resp.body);
        if (!j.contains("feed") || !j["feed"].contains("results")
            || !j["feed"]["results"].is_array())
            return false;
        const auto date = parseRfcFeedDate(jstr(j["feed"], "updated"));
        out.chartDate = date.empty() ? todayIso() : date;
        out.source = "Apple Music";
        int pos = 1;
        for (const auto& r : j["feed"]["results"]) {
            LiveChartEntry e;
            e.position   = pos++;
            e.title      = jstr(r, "name");
            e.artist     = jstr(r, "artistName");
            e.artworkUrl = jstr(r, "artworkUrl100");
            if (!e.title.empty() && !e.artist.empty())
                out.entries.push_back(std::move(e));
        }
    } catch (const std::exception& ex) {
        spdlog::warn("ChartsService: parse iTunes RSS KO ({})", ex.what());
        out.entries.clear();
        return false;
    }
    spdlog::info("ChartsService: iTunes RSS {} -> {} entrees (chart du {})",
                 country, out.entries.size(), out.chartDate);
    return out.entries.size() >= 10;
}

bool ChartsService::fetchKworb(const std::string& country, LiveChart& out) {
    const std::string page = (country == "global") ? "global" : country;
    auto resp = StreamingServiceBase::httpGet(
        "https://kworb.net/spotify/country/" + page + "_daily.html",
        "User-Agent: BeatMate/12.0 (Charts)\r\n");
    if (!resp.ok() || resp.body.empty()) {
        spdlog::warn("ChartsService: kworb HTTP {} pour {}", resp.status, country);
        return false;
    }
    try {
        const std::string& src = resp.body;
        std::smatch dm;
        std::regex dateRe(R"((\d{4})/(\d{2})/(\d{2}))");
        out.chartDate = std::regex_search(src, dm, dateRe)
            ? (dm[1].str() + "-" + dm[2].str() + "-" + dm[3].str())
            : todayIso();
        out.source = "Spotify (kworb)";
        std::regex rowRe(R"re(<td class="text mp"><div><a href="\.\./artist/[^"]*">([^<]+)</a> - <a href="\.\./track/[^"]*">([^<]+)</a>)re");
        auto begin = std::sregex_iterator(src.begin(), src.end(), rowRe);
        auto end   = std::sregex_iterator();
        int pos = 1;
        for (auto it = begin; it != end && pos <= 100; ++it) {
            LiveChartEntry e;
            e.position = pos++;
            e.artist   = decodeHtmlEntities((*it)[1].str());
            e.title    = decodeHtmlEntities((*it)[2].str());
            if (!e.title.empty() && !e.artist.empty())
                out.entries.push_back(std::move(e));
        }
    } catch (const std::exception& ex) {
        spdlog::warn("ChartsService: parse kworb KO ({})", ex.what());
        out.entries.clear();
        return false;
    }
    spdlog::info("ChartsService: kworb {} -> {} entrees (chart du {})",
                 country, out.entries.size(), out.chartDate);
    return out.entries.size() >= 10;
}

void ChartsService::carryDeltas(LiveChart& fresh, const LiveChart& previous) {
    if (previous.entries.empty()) return;

    if (previous.chartDate == fresh.chartDate && previous.source == fresh.source) {
        std::map<std::string, const LiveChartEntry*> old;
        for (const auto& e : previous.entries)
            old[matchKey(e.title, e.artist)] = &e;
        for (auto& e : fresh.entries) {
            auto it = old.find(matchKey(e.title, e.artist));
            if (it != old.end()) {
                e.previousPosition = it->second->previousPosition;
                e.delta            = it->second->delta;
                e.isNew            = it->second->isNew;
            } else {
                e.isNew = true;
            }
        }
        return;
    }

    std::map<std::string, int> oldPos;
    for (const auto& e : previous.entries)
        oldPos[matchKey(e.title, e.artist)] = e.position;
    for (auto& e : fresh.entries) {
        auto it = oldPos.find(matchKey(e.title, e.artist));
        if (it != oldPos.end()) {
            e.previousPosition = it->second;
            e.delta = it->second - e.position;
        } else {
            e.isNew = true;
        }
    }
}

LiveChart ChartsService::getChart(const std::string& country, bool forceRefresh) {
    LiveChart cached;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cached = readCacheLocked(country);
    }
    const int64_t now = juce::Time::currentTimeMillis();
    if (!forceRefresh && !cached.entries.empty()
        && now - cached.fetchedAtMs
               < static_cast<int64_t>(kRefreshAfterHours) * 3600 * 1000) {
        cached.fromCache = true;
        return cached;
    }

    LiveChart fresh;
    fresh.country = country;
    bool ok = fetchDeezer(country, fresh);
    if (!ok) {
        fresh = LiveChart{};
        fresh.country = country;
        ok = fetchItunesRss(country, fresh);
    }
    if (!ok) {
        fresh = LiveChart{};
        fresh.country = country;
        ok = fetchKworb(country, fresh);
    }
    if (!ok) {
        if (!cached.entries.empty()) {
            cached.fromCache = true;
            cached.errorMessage = "Sources injoignables - donnees du " + cached.chartDate;
            return cached;
        }
        LiveChart err;
        err.country = country;
        err.errorMessage = "Aucune source de charts accessible (Deezer, Apple Music, Spotify/kworb)";
        return err;
    }

    if (fresh.chartDate.empty()) fresh.chartDate = todayIso();
    carryDeltas(fresh, cached);
    fresh.fetchedAtMs = juce::Time::currentTimeMillis();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        memCache_[country] = fresh;
        writeCacheLocked(fresh);
    }
    return fresh;
}

LiveChart ChartsService::getCachedChart(const std::string& country) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto c = readCacheLocked(country);
    c.fromCache = true;
    return c;
}

std::vector<SimilarStreamingTrack> ChartsService::getSimilarTracks(
    const std::string& title, const std::string& artist, int maxResults) {
    std::vector<SimilarStreamingTrack> out;
    if (artist.empty() || maxResults <= 0) return out;

    auto esc = [](const std::string& s) {
        return juce::URL::addEscapeChars(juce::String::fromUTF8(s.c_str()), true)
            .toStdString();
    };

    int64_t artistId = 0;
    std::string artistName = artist;
    {
        auto resp = StreamingServiceBase::httpGet(
            "https://api.deezer.com/search/artist?q=" + esc(artist) + "&limit=1", {});
        if (resp.ok()) {
            try {
                auto j = json::parse(resp.body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    artistId = j["data"][0].value("id", static_cast<int64_t>(0));
                    const auto n = jstr(j["data"][0], "name");
                    if (!n.empty()) artistName = n;
                }
            } catch (...) {}
        }
    }
    if (artistId == 0) {
        auto resp = StreamingServiceBase::httpGet(
            "https://api.deezer.com/search?q=" + esc(artist + " " + title) + "&limit=1", {});
        if (resp.ok()) {
            try {
                auto j = json::parse(resp.body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()
                    && j["data"][0].contains("artist")
                    && j["data"][0]["artist"].is_object()) {
                    artistId = j["data"][0]["artist"].value("id", static_cast<int64_t>(0));
                    const auto n = jstr(j["data"][0]["artist"], "name");
                    if (!n.empty()) artistName = n;
                }
            } catch (...) {}
        }
    }
    if (artistId == 0) {
        spdlog::info("ChartsService: artiste introuvable sur Deezer pour '{}'", artist);
        return out;
    }

    const std::string currentTitleKey = normalizeKey(title);
    std::set<std::string> seen;
    seen.insert(matchKey(title, artist));

    auto addTopTracks = [&](int64_t id, const std::string& name, bool same, int limit) {
        if (static_cast<int>(out.size()) >= maxResults) return;
        auto resp = StreamingServiceBase::httpGet(
            "https://api.deezer.com/artist/" + std::to_string(id)
                + "/top?limit=" + std::to_string(limit), {});
        if (!resp.ok()) return;
        try {
            auto j = json::parse(resp.body);
            if (!j.contains("data") || !j["data"].is_array()) return;
            for (const auto& tr : j["data"]) {
                if (static_cast<int>(out.size()) >= maxResults) return;
                SimilarStreamingTrack s;
                s.title = jstr(tr, "title");
                if (tr.contains("artist") && tr["artist"].is_object())
                    s.artist = jstr(tr["artist"], "name");
                if (s.title.empty() || s.artist.empty()) continue;
                if (same && normalizeKey(s.title) == currentTitleKey) continue;
                if (!seen.insert(matchKey(s.title, s.artist)).second) continue;
                s.previewUrl = jstr(tr, "preview");
                if (tr.contains("album") && tr["album"].is_object())
                    s.artworkUrl = jstr(tr["album"], "cover_medium");
                s.sameArtist = same;
                s.reason = same ? std::string("M\xc3\xaame artiste")
                                : std::string("Artiste li\xc3\xa9 : ") + name;
                out.push_back(std::move(s));
            }
        } catch (...) {}
    };

    addTopTracks(artistId, artistName, true, 12);

    auto resp = StreamingServiceBase::httpGet(
        "https://api.deezer.com/artist/" + std::to_string(artistId) + "/related?limit=8", {});
    if (resp.ok()) {
        try {
            auto j = json::parse(resp.body);
            if (j.contains("data") && j["data"].is_array()) {
                int used = 0;
                for (const auto& a : j["data"]) {
                    if (used >= 5 || static_cast<int>(out.size()) >= maxResults) break;
                    const int64_t rid = a.value("id", static_cast<int64_t>(0));
                    const auto rname = jstr(a, "name");
                    if (rid == 0 || rname.empty()) continue;
                    addTopTracks(rid, rname, false, 6);
                    ++used;
                }
            }
        } catch (...) {}
    }

    spdlog::info("ChartsService: {} pistes similaires pour '{} - {}'",
                 out.size(), artist, title);
    return out;
}

} // namespace BeatMate::Services::Streaming
