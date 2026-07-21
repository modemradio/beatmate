#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>

#include "../../models/Track.h"

namespace BeatMate::Services::Library {

class TrackDatabase;

struct BrowseFilter {
    std::optional<std::string> genre;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    std::optional<std::string> key;
    std::optional<int> ratingMin;
    std::optional<float> energyMin;
    std::optional<float> energyMax;
    std::optional<int> yearMin;
    std::optional<int> yearMax;
    std::optional<std::string> mood;
    std::optional<std::string> label;
    int limit = 500;
    std::string sortBy = "title";
    bool ascending = true;
};

struct BrowseCategory {
    std::string name;
    int trackCount = 0;
};

class SmartBrowser {
public:
    explicit SmartBrowser(std::shared_ptr<TrackDatabase> database);
    ~SmartBrowser() = default;

    std::vector<BrowseCategory> browseByGenre();
    std::vector<BrowseCategory> browseByArtist();
    std::vector<BrowseCategory> browseByAlbum();
    std::vector<BrowseCategory> browseByBPM(double step = 5.0);
    std::vector<BrowseCategory> browseByKey();
    std::vector<BrowseCategory> browseByYear();
    std::vector<BrowseCategory> browseByMood();

    std::vector<Models::Track> getFilteredTracks(const BrowseFilter& filter);

    std::vector<Models::Track> getRecentlyAdded(int limit = 50);
    std::vector<Models::Track> getRecentlyPlayed(int limit = 50);
    std::vector<Models::Track> getMostPlayed(int limit = 50);
    std::vector<Models::Track> getTopRated(int limit = 50);

private:
    std::string buildFilterQuery(const BrowseFilter& filter) const;
    std::shared_ptr<TrackDatabase> database_;
};

}
