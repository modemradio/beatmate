#include "BillboardService.h"
#include "StreamingServiceBase.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>

#include <algorithm>

using json = nlohmann::json;

namespace BeatMate::Services::Streaming {


std::vector<ChartEntry> BillboardService::getHot100() {
    return getChart("hot-100").entries;
}

std::vector<ChartEntry> BillboardService::getBillboard200() {
    return getChart("billboard-200").entries;
}

ChartData BillboardService::getChart(const std::string& chartName) {
    if (cacheEnabled_ && isCached(chartName)) {
        spdlog::debug("BillboardService: Returning cached chart '{}'", chartName);
        return getCachedChart(chartName);
    }

    std::string response = fetchChart(chartName);
    if (response.empty()) return {};

    ChartData data = parseFullChartResponse(response, chartName);

    if (cacheEnabled_ && !data.entries.empty()) {
        cacheChart(chartName, data);
    }

    return data;
}

ChartData BillboardService::getChartForDate(const std::string& chartName, const std::string& date) {
    std::string cacheKey = chartName + "_" + date;
    if (cacheEnabled_ && isCached(cacheKey)) {
        return getCachedChart(cacheKey);
    }

    std::string response = fetchChart(chartName, date);
    if (response.empty()) return {};

    ChartData data = parseFullChartResponse(response, chartName);
    data.chartDate = date;

    if (cacheEnabled_ && !data.entries.empty()) {
        cacheChart(cacheKey, data);
    }

    return data;
}


std::vector<ChartEntry> BillboardService::getHotDanceElectronic() {
    return getChart("hot-dance-electronic-songs").entries;
}

std::vector<ChartEntry> BillboardService::getHotRnBHipHop() {
    return getChart("hot-r-and-b-hip-hop-songs").entries;
}

std::vector<ChartEntry> BillboardService::getPopSongs() {
    return getChart("pop-songs").entries;
}

std::vector<ChartEntry> BillboardService::getRockSongs() {
    return getChart("rock-songs").entries;
}

std::vector<ChartEntry> BillboardService::getCountrySongs() {
    return getChart("country-songs").entries;
}

std::vector<ChartEntry> BillboardService::getLatinSongs() {
    return getChart("latin-songs").entries;
}

std::vector<ChartEntry> BillboardService::getGospelSongs() {
    return getChart("hot-gospel-songs").entries;
}

std::vector<ChartEntry> BillboardService::getJazzAlbums() {
    return getChart("jazz-albums").entries;
}


std::vector<ChartEntry> BillboardService::getGlobalExclUS() {
    return getChart("billboard-global-excl-us").entries;
}

std::vector<ChartEntry> BillboardService::getCanadianHot100() {
    return getChart("canadian-hot-100").entries;
}

std::vector<ChartEntry> BillboardService::getUKSingles() {
    return getChart("official-uk-singles").entries;
}


std::vector<ChartInfo> BillboardService::getAvailableCharts() {
    return {
        {"hot-100",                    "Hot 100",                    "core",    "The week's most popular current songs across all genres"},
        {"billboard-200",              "Billboard 200",              "core",    "The week's most popular albums"},
        {"hot-dance-electronic-songs", "Hot Dance/Electronic Songs", "genre",   "Top dance/electronic songs"},
        {"dance-electronic-albums",    "Dance/Electronic Albums",    "genre",   "Top dance/electronic albums"},
        {"dance-mix-show-airplay",     "Dance Mix/Show Airplay",     "genre",   "Top dance club play chart"},
        {"hot-r-and-b-hip-hop-songs",  "Hot R&B/Hip-Hop Songs",     "genre",   "Top R&B and hip-hop songs"},
        {"pop-songs",                  "Pop Songs",                  "genre",   "Top 40-format radio airplay"},
        {"rock-songs",                 "Rock Songs",                 "genre",   "Top rock songs"},
        {"country-songs",              "Country Songs",              "genre",   "Top country songs"},
        {"latin-songs",                "Latin Songs",                "genre",   "Top Latin songs"},
        {"hot-gospel-songs",           "Hot Gospel Songs",           "genre",   "Top gospel songs"},
        {"jazz-albums",                "Jazz Albums",                "genre",   "Top jazz albums"},
        {"billboard-global-200",       "Billboard Global 200",       "country", "Global top 200"},
        {"billboard-global-excl-us",   "Billboard Global (excl. US)","country", "Global top 200 excluding US"},
        {"canadian-hot-100",           "Canadian Hot 100",           "country", "Canada's top songs"},
        {"official-uk-singles",        "UK Singles",                 "country", "UK official singles chart"},
        {"artist-100",                 "Artist 100",                 "format",  "Top artists"},
        {"streaming-songs",            "Streaming Songs",            "format",  "Top streaming songs"},
        {"digital-song-sales",         "Digital Song Sales",         "format",  "Top digital song sales"},
        {"radio-songs",                "Radio Songs",                "format",  "Top radio airplay songs"}
    };
}

std::vector<ChartInfo> BillboardService::getChartsByCategory(const std::string& category) {
    std::vector<ChartInfo> filtered;
    for (const auto& c : getAvailableCharts()) {
        if (c.category == category) {
            filtered.push_back(c);
        }
    }
    return filtered;
}


void BillboardService::registerUpdateCallback(BillboardUpdateCallback callback) {
    updateCallbacks_.push_back(std::move(callback));
}

void BillboardService::checkForWeeklyUpdates() {
    spdlog::info("BillboardService: Checking for weekly chart updates...");

    auto charts = getAvailableCharts();
    int updated = 0;

    for (const auto& chart : charts) {
        if (isChartStale(chart.slug)) {
            spdlog::info("BillboardService: Updating chart '{}'", chart.slug);
            ChartData data = getChart(chart.slug);

            if (!data.entries.empty()) {
                lastUpdateDates_[chart.slug] = data.chartDate;
                updated++;

                for (const auto& cb : updateCallbacks_) {
                    cb(chart.slug, data);
                }
            }
        }
    }

    spdlog::info("BillboardService: Updated {} charts", updated);
}

bool BillboardService::isChartStale(const std::string& chartName) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = chartCache_.find(chartName);
    if (it == chartCache_.end()) return true;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t age = now - it->second.fetchedAt;
    return age > (cacheDurationMinutes_ * 60);
}

std::string BillboardService::getLastUpdateDate(const std::string& chartName) const {
    auto it = lastUpdateDates_.find(chartName);
    return (it != lastUpdateDates_.end()) ? it->second : "";
}


std::vector<ChartEntry> BillboardService::getNewEntries(const std::string& chartName) {
    ChartData data = getChart(chartName);
    std::vector<ChartEntry> newEntries;
    for (const auto& entry : data.entries) {
        if (entry.isNew || entry.weeksOnChart <= 1) {
            newEntries.push_back(entry);
        }
    }
    spdlog::info("BillboardService: {} new entries on '{}'", newEntries.size(), chartName);
    return newEntries;
}

std::vector<ChartEntry> BillboardService::getBiggestMovers(const std::string& chartName, int limit) {
    ChartData data = getChart(chartName);
    std::vector<ChartEntry> movers = data.entries;

    for (auto& e : movers) {
        if (e.lastWeekPosition > 0) {
            e.positionChange = e.lastWeekPosition - e.position; // positive = moved up
        }
    }

    std::sort(movers.begin(), movers.end(), [](const ChartEntry& a, const ChartEntry& b) {
        return std::abs(a.positionChange) > std::abs(b.positionChange);
    });

    if (static_cast<int>(movers.size()) > limit) movers.resize(limit);
    return movers;
}

std::vector<ChartEntry> BillboardService::getLongestRunning(const std::string& chartName, int limit) {
    ChartData data = getChart(chartName);
    std::vector<ChartEntry> sorted = data.entries;

    std::sort(sorted.begin(), sorted.end(), [](const ChartEntry& a, const ChartEntry& b) {
        return a.weeksOnChart > b.weeksOnChart;
    });

    if (static_cast<int>(sorted.size()) > limit) sorted.resize(limit);
    return sorted;
}

int BillboardService::getChartPosition(const std::string& chartName, const std::string& title,
                                        const std::string& artist) {
    ChartData data = getChart(chartName);

    std::string lowerTitle = title;
    std::string lowerArtist = artist;
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
    std::transform(lowerArtist.begin(), lowerArtist.end(), lowerArtist.begin(), ::tolower);

    for (const auto& entry : data.entries) {
        std::string entryTitle = entry.title;
        std::string entryArtist = entry.artist;
        std::transform(entryTitle.begin(), entryTitle.end(), entryTitle.begin(), ::tolower);
        std::transform(entryArtist.begin(), entryArtist.end(), entryArtist.begin(), ::tolower);

        if (entryTitle.find(lowerTitle) != std::string::npos &&
            entryArtist.find(lowerArtist) != std::string::npos) {
            return entry.position;
        }
    }
    return -1;
}


std::vector<ChartData> BillboardService::getChartHistory(const std::string& chartName, int weeks) {
    std::vector<ChartData> history;

    history.push_back(getChart(chartName));

    auto now = std::chrono::system_clock::now();
    for (int w = 1; w < weeks; ++w) {
        auto date = now - std::chrono::hours(24 * 7 * w);
        auto tt = std::chrono::system_clock::to_time_t(date);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &tt);
#else
        localtime_r(&tt, &tm_buf);
#endif
        char dateStr[16];
        std::strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &tm_buf);

        history.push_back(getChartForDate(chartName, dateStr));
    }

    return history;
}


std::string BillboardService::fetchChart(const std::string& chartName, const std::string& date) {
    std::string endpoint = "https://billboard-api.p.rapidapi.com/chart/" + chartName;
    if (!date.empty()) endpoint += "?date=" + date;

    juce::String headers;
    if (!apiKey_.empty()) {
        headers = "X-RapidAPI-Key: " + juce::String(apiKey_) +
                  "\r\nX-RapidAPI-Host: billboard-api.p.rapidapi.com";
    }

    auto resp = StreamingServiceBase::httpGet(endpoint, headers);
    if (!resp.ok()) {
        spdlog::error("BillboardService: API error: status={} for chart '{}'", resp.status, chartName);
        return {};
    }
    return resp.body;
}

std::vector<ChartEntry> BillboardService::parseChartResponse(const std::string& response) {
    return parseFullChartResponse(response, "").entries;
}

ChartData BillboardService::parseFullChartResponse(const std::string& response,
                                                     const std::string& chartName) {
    ChartData data;
    data.chartName = chartName;
    data.fetchedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    try {
        auto j = json::parse(response);

        data.chartDate = j.value("date", j.value("chart_date", ""));

        if (j.contains("chart") && j["chart"].contains("entries")) {
            for (const auto& item : j["chart"]["entries"]) {
                ChartEntry entry;
                entry.position = item.value("position", item.value("rank", 0));
                entry.title = item.value("title", item.value("name", ""));
                entry.artist = item.value("artist", "");
                entry.lastWeekPosition = item.value("lastWeek", item.value("last_week", 0));
                entry.peakPosition = item.value("peak", item.value("peak_position", 0));
                entry.weeksOnChart = item.value("weeksOnChart", item.value("weeks_on_chart", 0));
                entry.imageUrl = item.value("image", "");
                entry.isrc = item.value("isrc", "");
                entry.isNew = (entry.lastWeekPosition == 0 && entry.weeksOnChart <= 1);
                entry.positionChange = (entry.lastWeekPosition > 0)
                                           ? (entry.lastWeekPosition - entry.position)
                                           : 0;
                data.entries.push_back(entry);
            }
        }
        else if (j.is_array()) {
            for (const auto& item : j) {
                ChartEntry entry;
                entry.position = item.value("rank", item.value("position", 0));
                entry.title = item.value("name", item.value("title", ""));
                entry.artist = item.value("artist", "");
                entry.lastWeekPosition = item.value("last_week", 0);
                entry.peakPosition = item.value("peak_position", 0);
                entry.weeksOnChart = item.value("weeks_on_chart", 0);
                entry.isNew = (entry.weeksOnChart <= 1);
                data.entries.push_back(entry);
            }
        }
        else if (j.contains("content")) {
            for (auto& [key, val] : j["content"].items()) {
                ChartEntry entry;
                entry.position = std::stoi(key);
                entry.title = val.value("title", "");
                entry.artist = val.value("artist", "");
                entry.lastWeekPosition = val.value("last_week", 0);
                entry.peakPosition = val.value("peak_position", 0);
                entry.weeksOnChart = val.value("weeks_on_chart", 0);
                entry.imageUrl = val.value("image", "");
                entry.isNew = (entry.lastWeekPosition == 0);
                entry.positionChange = (entry.lastWeekPosition > 0)
                                           ? (entry.lastWeekPosition - entry.position) : 0;
                data.entries.push_back(entry);
            }
            std::sort(data.entries.begin(), data.entries.end(),
                      [](const ChartEntry& a, const ChartEntry& b) { return a.position < b.position; });
        }
    } catch (const std::exception& e) {
        spdlog::error("BillboardService: Parse error: {}", e.what());
    }

    spdlog::info("BillboardService: Parsed {} entries for chart '{}'",
                 data.entries.size(), chartName);
    return data;
}


bool BillboardService::isCached(const std::string& chartName) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = chartCache_.find(chartName);
    if (it == chartCache_.end()) return false;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return (now - it->second.fetchedAt) < (cacheDurationMinutes_ * 60);
}

ChartData BillboardService::getCachedChart(const std::string& chartName) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = chartCache_.find(chartName);
    if (it != chartCache_.end()) return it->second;
    return {};
}

void BillboardService::cacheChart(const std::string& chartName, const ChartData& data) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    chartCache_[chartName] = data;
}

} // namespace BeatMate::Services::Streaming
