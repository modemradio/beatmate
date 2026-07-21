#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../../models/Track.h"

namespace BeatMate::Services::Streaming {

struct ChartEntry {
    int position = 0;
    std::string title;
    std::string artist;
    int lastWeekPosition = 0;
    int peakPosition = 0;
    int weeksOnChart = 0;
    std::string imageUrl;
    std::string isrc;
    int positionChange = 0;
    bool isNew = false;
};

struct ChartData {
    std::string chartName;
    std::string chartDate;
    std::vector<ChartEntry> entries;
    int64_t fetchedAt = 0;
};

struct ChartInfo {
    std::string slug;
    std::string displayName;
    std::string category;
    std::string description;
};

using BillboardUpdateCallback = std::function<void(const std::string& chartName, const ChartData& data)>;

class BillboardService {
public:
    BillboardService() = default;
    ~BillboardService() = default;

    void setApiKey(const std::string& apiKey) { apiKey_ = apiKey; }
    void setCacheEnabled(bool enabled) { cacheEnabled_ = enabled; }
    void setCacheDurationMinutes(int minutes) { cacheDurationMinutes_ = minutes; }

    std::vector<ChartEntry> getHot100();
    std::vector<ChartEntry> getBillboard200();
    ChartData getChart(const std::string& chartName);
    ChartData getChartForDate(const std::string& chartName, const std::string& date);

    std::vector<ChartEntry> getHotDanceElectronic();
    std::vector<ChartEntry> getHotRnBHipHop();
    std::vector<ChartEntry> getPopSongs();
    std::vector<ChartEntry> getRockSongs();
    std::vector<ChartEntry> getCountrySongs();
    std::vector<ChartEntry> getLatinSongs();
    std::vector<ChartEntry> getGospelSongs();
    std::vector<ChartEntry> getJazzAlbums();

    std::vector<ChartEntry> getGlobalExclUS();
    std::vector<ChartEntry> getCanadianHot100();
    std::vector<ChartEntry> getUKSingles();

    std::vector<ChartInfo> getAvailableCharts();
    std::vector<ChartInfo> getChartsByCategory(const std::string& category);

    void registerUpdateCallback(BillboardUpdateCallback callback);
    void checkForWeeklyUpdates();
    bool isChartStale(const std::string& chartName) const;
    std::string getLastUpdateDate(const std::string& chartName) const;

    std::vector<ChartEntry> getNewEntries(const std::string& chartName);
    std::vector<ChartEntry> getBiggestMovers(const std::string& chartName, int limit = 10);
    std::vector<ChartEntry> getLongestRunning(const std::string& chartName, int limit = 10);
    int getChartPosition(const std::string& chartName, const std::string& title,
                         const std::string& artist);

    std::vector<ChartData> getChartHistory(const std::string& chartName, int weeks = 4);

private:
    std::string fetchChart(const std::string& chartName, const std::string& date = "");
    std::vector<ChartEntry> parseChartResponse(const std::string& response);
    ChartData parseFullChartResponse(const std::string& response, const std::string& chartName);

    bool isCached(const std::string& chartName) const;
    ChartData getCachedChart(const std::string& chartName) const;
    void cacheChart(const std::string& chartName, const ChartData& data);

    std::string apiKey_;
    bool cacheEnabled_ = true;
    int cacheDurationMinutes_ = 360;

    mutable std::mutex cacheMutex_;
    std::map<std::string, ChartData> chartCache_;

    std::map<std::string, std::string> lastUpdateDates_;
    std::vector<BillboardUpdateCallback> updateCallbacks_;
};

}
