#include "SeratoDatabase.h"

#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace BeatMate::Services::Serato {

bool SeratoDatabase::open(const std::string& path) {
    basePath_ = path.empty() ? findSeratoFolder() : path;

    if (basePath_.empty() || !fs::exists(basePath_)) {
        spdlog::warn("SeratoDatabase: Serato folder not found (basePath='{}')", basePath_);
        isOpen_ = false;
        return false;
    }

    isOpen_ = true;
    spdlog::info("SeratoDatabase: Opened at {}", basePath_);
    return true;
}

void SeratoDatabase::close() {
    isOpen_ = false;
}

std::string SeratoDatabase::seratoDir() const {
    // Accept either <Music> (needs /_Serato_) or already the _Serato_ dir
    fs::path p(basePath_);
    if (p.filename() == "_Serato_") return p.string();
    fs::path combined = p / "_Serato_";
    if (fs::exists(combined)) return combined.string();
    return combined.string(); // may not exist; caller handles
}

std::string SeratoDatabase::findSeratoFolder() {
#ifdef _WIN32
    char userProfile[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile) == S_OK) {
        std::string up(userProfile);
        for (const char* sub : { "/Music", "/Documents", "/Desktop" }) {
            std::string candidate = up + sub;
            std::error_code ec;
            if (fs::exists(candidate + "/_Serato_", ec)) {
                spdlog::info("SeratoDatabase: _Serato_ trouve sous {}", candidate);
                return candidate;
            }
        }
    }

    DWORD drives = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        char letter = static_cast<char>('A' + i);
        std::string root = std::string(1, letter) + ":";
        std::error_code ec;
        if (fs::exists(root + "/_Serato_", ec)) {
            spdlog::info("SeratoDatabase: _Serato_ trouve sur disque {}:\\", letter);
            return root;
        }
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        std::string h(home);
        for (const char* sub : { "/Music", "/Documents", "/Desktop" }) {
            std::string candidate = h + sub;
            std::error_code ec;
            if (fs::exists(candidate + "/_Serato_", ec)) {
                spdlog::info("SeratoDatabase: _Serato_ trouve sous {}", candidate);
                return candidate;
            }
        }
    }
#endif
    spdlog::warn("SeratoDatabase: aucun dossier _Serato_ localise");
    return "";
}

bool SeratoDatabase::loadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.seekg(0, std::ios::end);
    std::streamsize sz = f.tellg();
    if (sz <= 0) { out.clear(); return true; }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return f.good() || f.eof();
}

uint16_t SeratoDatabase::readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]);
}
uint32_t SeratoDatabase::readBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
uint64_t SeratoDatabase::readBE64(const uint8_t* p) {
    return (uint64_t(readBE32(p)) << 32) | uint64_t(readBE32(p + 4));
}

std::string SeratoDatabase::readUtf16BE(const uint8_t* p, size_t len) {
    // Serato stores strings as UTF-16 big-endian, no BOM, no terminator.
    std::string out;
    out.reserve(len / 2);
    size_t i = 0;
    while (i + 1 < len) {
        uint16_t ch = readBE16(p + i);
        i += 2;
        if (ch == 0) break;
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < len) {
            // surrogate pair
            uint16_t lo = readBE16(p + i);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                i += 2;
                uint32_t cp = 0x10000 + (((ch - 0xD800) << 10) | (lo - 0xDC00));
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6)  & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

// TLV: [4-byte tag][4-byte BE length][payload ...]

template <typename Visitor>
bool SeratoDatabase::walkTLV(const uint8_t* data, size_t size, Visitor&& visit) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        char tag[5] = {0};
        std::memcpy(tag, data + pos, 4);
        uint32_t len = readBE32(data + pos + 4);
        pos += 8;

        // Validate tag contains printable ASCII (sanity check against drift)
        bool validTag = true;
        for (int i = 0; i < 4; ++i) {
            unsigned char c = static_cast<unsigned char>(tag[i]);
            if (c < 0x20 || c > 0x7E) { validTag = false; break; }
        }
        if (!validTag || pos + len > size) {
            // corrupted / truncated - bail out
            return false;
        }

        visit(std::string(tag, 4), data + pos, static_cast<size_t>(len));
        pos += len;
    }
    return true;
}

void SeratoDatabase::parseOtrk(const uint8_t* payload, size_t size, Models::SeratoTrack& track) {
    walkTLV(payload, size, [&](const std::string& tag, const uint8_t* p, size_t len) {
        if (tag == "pfil" || tag == "ptrk") {
            track.externalPath = readUtf16BE(p, len);
        } else if (tag == "tsng") {
            track.title = readUtf16BE(p, len);
        } else if (tag == "tart") {
            track.artist = readUtf16BE(p, len);
        } else if (tag == "talb") {
            track.album = readUtf16BE(p, len);
        } else if (tag == "tgen") {
            track.genre = readUtf16BE(p, len);
        } else if (tag == "tcom") {
            track.comment = readUtf16BE(p, len);
        } else if (tag == "tgrp") {
            track.grouping = readUtf16BE(p, len);
        } else if (tag == "tlbl") {
            track.label = readUtf16BE(p, len);
        } else if (tag == "tkey") {
            track.key = readUtf16BE(p, len);
            track.camelotKey = track.key;
        } else if (tag == "tbpm" || tag == "pbpm") {
            // BPM stored as UTF-16BE text ("128.00")
            auto s = readUtf16BE(p, len);
            try { if (!s.empty()) track.bpm = std::stod(s); } catch (...) {}
        } else if (tag == "tlen") {
            auto s = readUtf16BE(p, len);
            try { if (!s.empty()) track.duration = std::stod(s); } catch (...) {}
        } else if (tag == "tadd" || tag == "uadd") {
            // Date added (as text)
            track.seratoDateAdded = readUtf16BE(p, len);
        } else if (tag == "tyer") {
            auto s = readUtf16BE(p, len);
            try { if (!s.empty()) track.year = std::stoi(s); } catch (...) {}
        } else if (tag == "tart" || tag == "trmx" || tag == "tcmp" || tag == "tcor" ||
                   tag == "tsiz" || tag == "tbit" || tag == "tsmp" || tag == "ttrk" ||
                   tag == "ttyp" || tag == "udsc" || tag == "ugrp" || tag == "ulbl" ||
                   tag == "uinv" || tag == "ucol") {
            // Text string fields (kept in map-style via extra fields not modeled; ignore silently)
            (void)0;
        }
        else if (tag == "utpc") {
            // Play count (4-byte BE uint)
            if (len >= 4) track.playCount = static_cast<int>(readBE32(p));
        } else if (tag == "urtg") {
            // Rating (4-byte BE uint; Serato 0 or 20/40/60/80/100 scaled)
            if (len >= 4) {
                uint32_t r = readBE32(p);
                if (r > 5) r = r / 20; // normalize to 0..5
                track.rating = static_cast<int>(r);
            }
        } else if (tag == "utme") {
            // Last played / mod time (8-byte BE epoch)
            if (len >= 8)       track.lastPlayed = static_cast<int64_t>(readBE64(p));
            else if (len >= 4)  track.lastPlayed = static_cast<int64_t>(readBE32(p));
        } else if (tag == "bbgl") {
            // BPM lock boolean
            if (len >= 1) track.seratoBpmLock = (p[0] != 0) ? 1 : 0;
        } else if (tag == "bhrt" || tag == "bmis" || tag == "biro" ||
                   tag == "bply" || tag == "blop" || tag == "bitu") {
            // Boolean flags (ignored but recognized)
            (void)0;
        }
        else if (tag == "sbav" || tag == "bbgr") {
            track.seratoBeatGrid.assign(p, p + len);
        } else {
            // Unknown tag — skip silently (forward-compat)
        }
    });
}

void SeratoDatabase::parseDatabaseV2(const std::string& path,
                                     std::vector<Models::SeratoTrack>& out) {
    std::vector<uint8_t> data;
    if (!loadFile(path, data)) {
        spdlog::error("SeratoDatabase: cannot open {}", path);
        return;
    }

    if (data.size() < 8) {
        spdlog::warn("SeratoDatabase: database V2 too small ({} bytes)", data.size());
        return;
    }

    int count = 0;
    bool ok = walkTLV(data.data(), data.size(),
        [&](const std::string& tag, const uint8_t* p, size_t len) {
            if (tag == "otrk") {
                Models::SeratoTrack t;
                t.source = Models::TrackSource::Serato;
                parseOtrk(p, len, t);
                if (!t.externalPath.empty()) {
                    out.push_back(std::move(t));
                    ++count;
                }
            } else if (tag == "vrsn") {
                auto v = readUtf16BE(p, len);
                spdlog::info("SeratoDatabase: database V2 version='{}'", v);
            }
            // osrt / ovct: sort/view config — ignore
        });

    if (!ok) {
        spdlog::warn("SeratoDatabase: parse of {} stopped early (truncation)", path);
    }
    spdlog::info("SeratoDatabase: database V2 -> {} tracks parsed", count);
}

std::vector<Models::SeratoTrack> SeratoDatabase::readAllTracks() {
    std::vector<Models::SeratoTrack> tracks;
    if (!isOpen_) return tracks;

    fs::path dbPath = fs::path(seratoDir()) / "database V2";
    if (!fs::exists(dbPath)) {
        spdlog::warn("SeratoDatabase: 'database V2' not found at {}", dbPath.string());
        return tracks;
    }
    parseDatabaseV2(dbPath.string(), tracks);

    size_t n = std::min<size_t>(5, tracks.size());
    for (size_t i = 0; i < n; ++i) {
        const auto& t = tracks[i];
        spdlog::info("  [{}] {} - {} ({} BPM, key={}) path={}",
                     i, t.artist, t.title, t.bpm, t.key, t.externalPath);
    }
    spdlog::info("SeratoDatabase: total tracks = {}", tracks.size());
    return tracks;
}

bool SeratoDatabase::parseCrateFile(const std::string& path,
                                    std::vector<std::string>& trackPaths) {
    std::vector<uint8_t> data;
    if (!loadFile(path, data)) return false;
    if (data.size() < 8) return false;

    bool ok = walkTLV(data.data(), data.size(),
        [&](const std::string& tag, const uint8_t* p, size_t len) {
            if (tag == "otrk") {
                // Nested: look for ptrk inside otrk payload
                walkTLV(p, len,
                    [&](const std::string& subtag, const uint8_t* sp, size_t slen) {
                        if (subtag == "ptrk") {
                            auto path = readUtf16BE(sp, slen);
                            if (!path.empty()) trackPaths.push_back(std::move(path));
                        }
                    });
            } else if (tag == "ptrk") {
                // Flat form (older crates)
                auto pth = readUtf16BE(p, len);
                if (!pth.empty()) trackPaths.push_back(std::move(pth));
            }
            // vrsn / osrt / ovct: skip
        });
    return ok;
}

std::vector<std::string> SeratoDatabase::readCrateNames() {
    std::vector<std::string> crates;
    if (!isOpen_) return crates;

    for (const auto& sub : { "Subcrates", "Crates" }) {
        fs::path dir = fs::path(seratoDir()) / sub;
        if (!fs::exists(dir)) continue;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".crate") {
                crates.push_back(e.path().stem().string());
            }
        }
    }
    std::sort(crates.begin(), crates.end());
    crates.erase(std::unique(crates.begin(), crates.end()), crates.end());
    return crates;
}

std::vector<Models::SeratoTrack> SeratoDatabase::readCrateTracks(const std::string& crateName) {
    std::vector<Models::SeratoTrack> tracks;
    if (!isOpen_) return tracks;

    fs::path cratePath;
    for (const auto& sub : { "Subcrates", "Crates" }) {
        fs::path candidate = fs::path(seratoDir()) / sub / (crateName + ".crate");
        if (fs::exists(candidate)) { cratePath = candidate; break; }
    }
    if (cratePath.empty()) {
        spdlog::warn("SeratoDatabase: Crate file not found: {}", crateName);
        return tracks;
    }

    std::vector<std::string> trackPaths;
    parseCrateFile(cratePath.string(), trackPaths);
    tracks.reserve(trackPaths.size());
    for (auto& p : trackPaths) {
        Models::SeratoTrack t;
        t.externalPath = std::move(p);
        t.source = Models::TrackSource::Serato;
        t.crateNames.push_back(crateName);
        tracks.push_back(std::move(t));
    }
    return tracks;
}

std::map<std::string, std::vector<std::string>> SeratoDatabase::getCrates() {
    std::map<std::string, std::vector<std::string>> result;
    if (!isOpen_) return result;
    auto names = readCrateNames();
    for (const auto& n : names) {
        std::vector<std::string> paths;
        fs::path cratePath;
        for (const auto& sub : { "Subcrates", "Crates" }) {
            fs::path cand = fs::path(seratoDir()) / sub / (n + ".crate");
            if (fs::exists(cand)) { cratePath = cand; break; }
        }
        if (!cratePath.empty()) {
            parseCrateFile(cratePath.string(), paths);
        }
        result.emplace(n, std::move(paths));
    }
    spdlog::info("SeratoDatabase: {} crates loaded", result.size());
    return result;
}

// Session .session files : TLV format. Tags : oent/adat/ttme/tstp/ttyp/pfil/tsng/tart.
void SeratoDatabase::parseOent(const uint8_t* payload, size_t size, SeratoSessionEntry& entry) {
    // oent can either contain an 'adat' sub-blob, or flat fields. Handle both.
    walkTLV(payload, size, [&](const std::string& tag, const uint8_t* p, size_t len) {
        if (tag == "adat") {
            // Nested TLV: parse recursively
            parseOent(p, len, entry);
            return;
        }
        if (tag == "pfil" || tag == "ptrk") {
            entry.trackPath = readUtf16BE(p, len);
        } else if (tag == "tsng") {
            entry.title = readUtf16BE(p, len);
        } else if (tag == "tart") {
            entry.artist = readUtf16BE(p, len);
        } else if (tag == "ttme" || tag == "sttm") {
            if (len >= 8)      entry.startTime = static_cast<int64_t>(readBE64(p));
            else if (len >= 4) entry.startTime = static_cast<int64_t>(readBE32(p));
        } else if (tag == "tstp" || tag == "sttp") {
            if (len >= 8)      entry.endTime = static_cast<int64_t>(readBE64(p));
            else if (len >= 4) entry.endTime = static_cast<int64_t>(readBE32(p));
        } else if (tag == "sttr" || tag == "tplp") {
            if (len >= 4) entry.playtimeSec = static_cast<int32_t>(readBE32(p));
        } else if (tag == "tdek" || tag == "dek ") {
            if (len >= 4) entry.deck = static_cast<int32_t>(readBE32(p));
        }
        // unknown -> skip
    });
}

bool SeratoDatabase::parseSessionFile(const std::string& path, SeratoSession& session) {
    std::vector<uint8_t> data;
    if (!loadFile(path, data)) return false;
    if (data.size() < 8) return false;

    session.filePath = path;
    session.name     = fs::path(path).stem().string();

    bool ok = walkTLV(data.data(), data.size(),
        [&](const std::string& tag, const uint8_t* p, size_t len) {
            if (tag == "oent") {
                SeratoSessionEntry e;
                e.sessionName = session.name;
                parseOent(p, len, e);
                if (session.startTime == 0 && e.startTime != 0) {
                    session.startTime = e.startTime;
                }
                session.entries.push_back(std::move(e));
            }
        });
    return ok;
}

std::vector<SeratoSession> SeratoDatabase::getHistorySessions() {
    std::vector<SeratoSession> sessions;
    if (!isOpen_) return sessions;

    fs::path dir = fs::path(seratoDir()) / "History" / "Sessions";
    if (!fs::exists(dir)) {
        // fallback: plain History/
        dir = fs::path(seratoDir()) / "History";
        if (!fs::exists(dir)) {
            spdlog::info("SeratoDatabase: no History folder");
            return sessions;
        }
    }

    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".session") continue;

        SeratoSession s;
        if (parseSessionFile(e.path().string(), s)) {
            spdlog::info("SeratoDatabase: session '{}' -> {} entries",
                         s.name, s.entries.size());
            sessions.push_back(std::move(s));
        } else {
            spdlog::warn("SeratoDatabase: failed to parse session {}", e.path().string());
        }
    }

    spdlog::info("SeratoDatabase: {} history sessions loaded", sessions.size());
    return sessions;
}

} // namespace BeatMate::Services::Serato
