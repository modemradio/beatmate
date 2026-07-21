#include "SmartBrowser.h"
#include "TrackDatabase.h"

#include <spdlog/spdlog.h>
#include <sstream>
#include <cmath>

namespace BeatMate::Services::Library {

SmartBrowser::SmartBrowser(std::shared_ptr<TrackDatabase> database)
    : database_(std::move(database)) {
}

std::vector<BrowseCategory> SmartBrowser::browseByGenre() {
    auto tracks = database_->getTracksByQuery(
        "SELECT genre, COUNT(*) as cnt FROM tracks WHERE genre != '' GROUP BY genre ORDER BY cnt DESC"
    );

    std::vector<BrowseCategory> categories;
    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> genreCount;
    for (const auto& track : allTracks) {
        if (!track.genre.empty()) {
            genreCount[track.genre]++;
        }
    }

    for (const auto& [genre, count] : genreCount) {
        categories.push_back({genre, count});
    }

    spdlog::debug("SmartBrowser: {} genres found", categories.size());
    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByArtist() {
    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> artistCount;
    for (const auto& track : allTracks) {
        if (!track.artist.empty()) {
            artistCount[track.artist]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [artist, count] : artistCount) {
        categories.push_back({artist, count});
    }

    spdlog::debug("SmartBrowser: {} artists found", categories.size());
    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByAlbum() {
    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> albumCount;
    for (const auto& track : allTracks) {
        if (!track.album.empty()) {
            albumCount[track.album]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [album, count] : albumCount) {
        categories.push_back({album, count});
    }

    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByBPM(double step) {
    auto allTracks = database_->getAllTracks();
    std::map<int, int> bpmCount;

    for (const auto& track : allTracks) {
        if (track.bpm > 0) {
            int bucket = static_cast<int>(std::floor(track.bpm / step) * step);
            bpmCount[bucket]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [bpm, count] : bpmCount) {
        std::string label = std::to_string(bpm) + "-" + std::to_string(bpm + static_cast<int>(step));
        categories.push_back({label, count});
    }

    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByKey() {
    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> keyCount;
    for (const auto& track : allTracks) {
        std::string k = !track.camelotKey.empty() ? track.camelotKey : track.key;
        if (!k.empty()) {
            keyCount[k]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [key, count] : keyCount) {
        categories.push_back({key, count});
    }

    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByYear() {
    auto allTracks = database_->getAllTracks();
    std::map<int, int> yearCount;
    for (const auto& track : allTracks) {
        if (track.year > 0) {
            yearCount[track.year]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [year, count] : yearCount) {
        categories.push_back({std::to_string(year), count});
    }

    return categories;
}

std::vector<BrowseCategory> SmartBrowser::browseByMood() {
    auto allTracks = database_->getAllTracks();
    std::map<std::string, int> moodCount;
    for (const auto& track : allTracks) {
        if (!track.mood.empty()) {
            moodCount[track.mood]++;
        }
    }

    std::vector<BrowseCategory> categories;
    for (const auto& [mood, count] : moodCount) {
        categories.push_back({mood, count});
    }

    return categories;
}

std::vector<Models::Track> SmartBrowser::getFilteredTracks(const BrowseFilter& filter) {
    std::string sql = buildFilterQuery(filter);
    return database_->getTracksByQuery(sql);
}

std::vector<Models::Track> SmartBrowser::getRecentlyAdded(int limit) {
    return database_->getTracksByQuery(
        "SELECT * FROM tracks ORDER BY date_added DESC LIMIT " + std::to_string(limit)
    );
}

std::vector<Models::Track> SmartBrowser::getRecentlyPlayed(int limit) {
    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE last_played > 0 ORDER BY last_played DESC LIMIT " + std::to_string(limit)
    );
}

std::vector<Models::Track> SmartBrowser::getMostPlayed(int limit) {
    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE play_count > 0 ORDER BY play_count DESC LIMIT " + std::to_string(limit)
    );
}

std::vector<Models::Track> SmartBrowser::getTopRated(int limit) {
    return database_->getTracksByQuery(
        "SELECT * FROM tracks WHERE rating > 0 ORDER BY rating DESC LIMIT " + std::to_string(limit)
    );
}

std::string SmartBrowser::buildFilterQuery(const BrowseFilter& filter) const {
    std::ostringstream sql;
    sql << "SELECT * FROM tracks WHERE 1=1";

    if (filter.genre) sql << " AND genre = '" << *filter.genre << "'";
    if (filter.artist) sql << " AND artist = '" << *filter.artist << "'";
    if (filter.album) sql << " AND album = '" << *filter.album << "'";
    if (filter.bpmMin) sql << " AND bpm >= " << *filter.bpmMin;
    if (filter.bpmMax) sql << " AND bpm <= " << *filter.bpmMax;
    if (filter.key) sql << " AND (key = '" << *filter.key << "' OR camelot_key = '" << *filter.key << "')";
    if (filter.ratingMin) sql << " AND rating >= " << *filter.ratingMin;
    if (filter.energyMin) sql << " AND energy >= " << *filter.energyMin;
    if (filter.energyMax) sql << " AND energy <= " << *filter.energyMax;
    if (filter.yearMin) sql << " AND year >= " << *filter.yearMin;
    if (filter.yearMax) sql << " AND year <= " << *filter.yearMax;
    if (filter.mood) sql << " AND mood = '" << *filter.mood << "'";
    if (filter.label) sql << " AND label = '" << *filter.label << "'";

    sql << " ORDER BY " << filter.sortBy << (filter.ascending ? " ASC" : " DESC");
    sql << " LIMIT " << filter.limit;

    return sql.str();
}

}
