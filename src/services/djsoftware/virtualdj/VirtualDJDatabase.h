#pragma once

#include <string>
#include <vector>

#include "../../../models/VirtualDJTrack.h"

namespace BeatMate::Services::VirtualDJ {

class VirtualDJDatabase {
public:
    VirtualDJDatabase() = default;
    ~VirtualDJDatabase() = default;

    bool open(const std::string& path = "");
    void close();
    bool isOpen() const { return isOpen_; }

    std::vector<Models::VirtualDJTrack> readAllTracks();
    std::string getDatabasePath() const { return dbPath_; }

private:
    bool parseXmlDatabase(const std::string& path);
    bool parseSqliteDatabase(const std::string& path);
    std::string findDefaultPath() const;

    std::string dbPath_;
    bool isOpen_ = false;
    std::vector<Models::VirtualDJTrack> tracks_;
};

}
