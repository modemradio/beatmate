#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../../models/TraktorTrack.h"

namespace BeatMate::Services::Traktor {

class TraktorCollectionParser;

class TraktorService {
public:
    TraktorService();
    ~TraktorService();

    bool initialize();
    bool isAvailable() const;

    std::vector<Models::TraktorTrack> readCollection();
    std::string findCollectionPath() const;

private:
    std::unique_ptr<TraktorCollectionParser> parser_;
    bool initialized_ = false;
};

} // namespace BeatMate::Services::Traktor
