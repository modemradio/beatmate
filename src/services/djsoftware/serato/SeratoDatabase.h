#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>

#include "../../../models/SeratoTrack.h"

namespace BeatMate::Services::Serato {

struct SeratoSessionEntry {
    std::string trackPath;
    std::string title;
    std::string artist;
    int64_t startTime = 0;
    int64_t endTime   = 0;
    int32_t playtimeSec = 0;
    int32_t deck = 0;
    std::string sessionName;
};

struct SeratoSession {
    std::string name;
    std::string filePath;
    int64_t startTime = 0;
    std::vector<SeratoSessionEntry> entries;
};

class SeratoDatabase {
public:
    SeratoDatabase() = default;
    ~SeratoDatabase() = default;

    bool open(const std::string& path = "");
    void close();
    bool isOpen() const { return isOpen_; }

    std::vector<Models::SeratoTrack> readAllTracks();

    std::vector<std::string> readCrateNames();
    std::vector<Models::SeratoTrack> readCrateTracks(const std::string& crateName);
    std::map<std::string, std::vector<std::string>> getCrates();

    std::vector<SeratoSession> getHistorySessions();

    const std::string& basePath() const { return basePath_; }
    std::string seratoDir() const;

    static std::string findSeratoFolder();

private:
    static bool loadFile(const std::string& path, std::vector<uint8_t>& out);

    static uint16_t readBE16(const uint8_t* p);
    static uint32_t readBE32(const uint8_t* p);
    static uint64_t readBE64(const uint8_t* p);
    static std::string readUtf16BE(const uint8_t* p, size_t len);

    template <typename Visitor>
    static bool walkTLV(const uint8_t* data, size_t size, Visitor&& visit);

    void parseDatabaseV2(const std::string& path, std::vector<Models::SeratoTrack>& out);
    bool parseCrateFile(const std::string& path, std::vector<std::string>& trackPaths);
    bool parseSessionFile(const std::string& path, SeratoSession& session);

    static void parseOtrk(const uint8_t* payload, size_t size, Models::SeratoTrack& track);
    static void parseOent(const uint8_t* payload, size_t size, SeratoSessionEntry& entry);

    std::string basePath_;
    bool isOpen_ = false;
};

}
