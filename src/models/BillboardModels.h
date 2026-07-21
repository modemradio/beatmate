#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models {

struct BillboardEntry {
    int position = 0;
    int previousPosition = 0;
    int peakPosition = 0;
    int weeksOnChart = 0;
    bool isNew = false;
    bool isReEntry = false;

    std::string title;
    std::string artist;
    std::string album;
    std::string label;
    std::string isrc;
    std::string artworkUrl;

    int positionChange = 0;
    std::string trend;

    BillboardEntry() = default;

    bool operator==(const BillboardEntry& other) const {
        return position == other.position && title == other.title;
    }

    bool operator<(const BillboardEntry& other) const {
        return position < other.position;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BillboardEntry,
        position, previousPosition, peakPosition, weeksOnChart,
        isNew, isReEntry, title, artist, album, label, isrc, artworkUrl,
        positionChange, trend
    )
};

struct BillboardChart {
    std::string chartName;
    std::string chartId;
    std::string date;
    std::string previousDate;
    std::string nextDate;
    std::string url;
    int totalEntries = 0;

    std::vector<BillboardEntry> entries;

    BillboardChart() = default;

    bool operator==(const BillboardChart& other) const {
        return chartId == other.chartId && date == other.date;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BillboardChart,
        chartName, chartId, date, previousDate, nextDate,
        url, totalEntries, entries
    )
};

struct BillboardArtistChart {
    std::string artist;
    std::string chartName;
    std::vector<BillboardEntry> entries;
    int totalWeeksOnChart = 0;
    int numberOfOnes = 0;
    int topTenHits = 0;

    BillboardArtistChart() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BillboardArtistChart,
        artist, chartName, entries, totalWeeksOnChart, numberOfOnes, topTenHits
    )
};

struct BillboardChartHistory {
    std::string title;
    std::string artist;
    std::string chartName;

    struct ChartWeek {
        std::string date;
        int position = 0;
        bool isNew = false;

        ChartWeek() = default;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ChartWeek, date, position, isNew)
    };

    std::vector<ChartWeek> weeks;
    int peakPosition = 0;
    int totalWeeks = 0;
    std::string firstWeek;
    std::string lastWeek;

    BillboardChartHistory() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BillboardChartHistory,
        title, artist, chartName, weeks, peakPosition, totalWeeks,
        firstWeek, lastWeek
    )
};

}
