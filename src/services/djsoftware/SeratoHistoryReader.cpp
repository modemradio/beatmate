#include "SeratoHistoryReader.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace BeatMate::Services::DJSoftware {

namespace {


static std::string seratoSessionsDir()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    auto dir = home.getChildFile("Music")
                   .getChildFile("_Serato_")
                   .getChildFile("History")
                   .getChildFile("Sessions");
    return dir.getFullPathName().toStdString();
}

static uint32_t readBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

static std::string decodeUtf16BE(const uint8_t* data, size_t len)
{
    if (!data || len < 2) return {};
    juce::MemoryBlock mb(data, len);
    juce::String s = juce::String::createStringFromData(mb.getData(), (int) mb.getSize());
    if (s.isEmpty() || s.containsNonWhitespaceChars() == false) {
        juce::String out;
        for (size_t i = 0; i + 1 < len; i += 2) {
            juce::juce_wchar c = (juce::juce_wchar) ((data[i] << 8) | data[i + 1]);
            if (c == 0) break;
            out += juce::String::charToString(c);
        }
        return out.toStdString();
    }
    return s.toStdString();
}

struct OentFields {
    std::string filePath;
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    double      bpm          = 0.0;
    int64_t     playedAtUnix = 0;
};

static void parseOentPayload(const uint8_t* data, size_t len, OentFields& out)
{
    size_t pos = 0;
    while (pos + 8 <= len) {
        char tag[5] = {0,0,0,0,0};
        std::memcpy(tag, data + pos, 4);
        uint32_t subSize = readBE32(data + pos + 4);
        pos += 8;
        if (subSize > len || pos + subSize > len) break;

        const uint8_t* sub = data + pos;
        std::string t(tag);

        if (t == "pfil") {
            out.filePath = decodeUtf16BE(sub, subSize);
        } else if (t == "tsng") {
            out.title = decodeUtf16BE(sub, subSize);
        } else if (t == "tart") {
            out.artist = decodeUtf16BE(sub, subSize);
        } else if (t == "talb") {
            out.album = decodeUtf16BE(sub, subSize);
        } else if (t == "tgen") {
            out.genre = decodeUtf16BE(sub, subSize);
        } else if (t == "tbpm") {
            std::string s = decodeUtf16BE(sub, subSize);
            try { out.bpm = std::stod(s); } catch (...) {}
        } else if (t == "adat") {
            parseOentPayload(sub, subSize, out);
        } else if (t == "tadd" || t == "ttyr") {
            std::string s = decodeUtf16BE(sub, subSize);
            juce::Time when = juce::Time::fromISO8601(juce::String(s));
            if (when.toMilliseconds() > 0) {
                out.playedAtUnix = when.toMilliseconds() / 1000;
            }
        } else if (t == "utme") {
            if (subSize >= 4) {
                out.playedAtUnix = (int64_t) readBE32(sub);
            }
        }
        pos += subSize;
    }
}

static bool parseSingleSessionFile(const juce::File& f, std::vector<PlayedTrack>& out)
{
    juce::FileInputStream in(f);
    if (!in.openedOk()) return false;

    const auto size = (size_t) in.getTotalLength();
    if (size < 8) return false;

    juce::MemoryBlock buf;
    buf.setSize(size);
    in.read(buf.getData(), (int) size);

    const uint8_t* data = static_cast<const uint8_t*>(buf.getData());
    size_t pos = 0;

    if (size >= 4 && std::memcmp(data, "vrsn", 4) != 0) {
        return false;
    }

    if (size >= 8) {
        uint32_t vrsnSize = readBE32(data + 4);
        pos = 8 + (size_t) vrsnSize;
    }

    const int64_t fileTimeUnix =
        f.getLastModificationTime().toMilliseconds() / 1000;

    while (pos + 8 <= size) {
        char tag[5] = {0,0,0,0,0};
        std::memcpy(tag, data + pos, 4);
        uint32_t chunkSize = readBE32(data + pos + 4);
        pos += 8;
        if (chunkSize > size || pos + chunkSize > size) break;

        if (std::strncmp(tag, "oent", 4) == 0) {
            OentFields fields;
            parseOentPayload(data + pos, chunkSize, fields);

            if (!fields.title.empty() || !fields.filePath.empty()) {
                PlayedTrack pt;
                pt.filePath     = fields.filePath;
                pt.title        = fields.title;
                pt.artist       = fields.artist;
                pt.album        = fields.album;
                pt.genre        = fields.genre;
                pt.bpm          = fields.bpm;
                pt.playedAtUnix = fields.playedAtUnix > 0 ? fields.playedAtUnix : fileTimeUnix;
                pt.source       = "Serato";
                out.push_back(std::move(pt));
            }
        }
        pos += chunkSize;
    }
    return !out.empty();
}

}


std::vector<PlayedTrack> SeratoHistoryReader::parseBinarySessions(
    const std::string& sessionsDir, int maxTracks)
{
    std::vector<PlayedTrack> result;
    juce::File dir(sessionsDir);
    if (!dir.isDirectory()) return {};

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.session");
    std::sort(files.begin(), files.end(),
        [](const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });

    for (const auto& f : files) {
        if ((int) result.size() >= maxTracks) break;
        try {
            parseSingleSessionFile(f, result);
        } catch (const std::exception& e) {
            spdlog::warn("[SeratoHistoryReader] parse failed on {}: {}",
                         f.getFullPathName().toStdString(), e.what());
        } catch (...) {
            spdlog::warn("[SeratoHistoryReader] unknown parse error on {}",
                         f.getFullPathName().toStdString());
        }
    }

    if ((int) result.size() > maxTracks) result.resize(maxTracks);
    return result;
}

std::vector<PlayedTrack> SeratoHistoryReader::parseCsvFallback(
    const std::string& sessionsDir, int maxTracks)
{
    std::vector<PlayedTrack> result;
    juce::File dir(sessionsDir);
    if (!dir.isDirectory()) return {};

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*.csv");
    juce::File hist = dir.getParentDirectory().getChildFile("History.csv");
    if (hist.existsAsFile()) files.add(hist);

    for (const auto& f : files) {
        juce::StringArray lines;
        f.readLines(lines);

        bool first = true;
        for (auto& line : lines) {
            if (first) { first = false; continue; }
            if (line.trim().isEmpty()) continue;

            juce::StringArray cols;
            cols.addTokens(line, ",", "\"");
            auto getCol = [&](int i) -> std::string {
                if (i < 0 || i >= cols.size()) return {};
                return cols[i].unquoted().toStdString();
            };

            PlayedTrack pt;
            pt.title  = getCol(0);
            pt.artist = getCol(1);
            juce::Time when = juce::Time::fromISO8601(cols.size() > 2 ? cols[2] : juce::String());
            pt.playedAtUnix = when.toMilliseconds() > 0
                              ? when.toMilliseconds() / 1000
                              : f.getLastModificationTime().toMilliseconds() / 1000;
            pt.source = "Serato";

            if (!pt.title.empty() || !pt.artist.empty()) {
                result.push_back(std::move(pt));
                if ((int) result.size() >= maxTracks) return result;
            }
        }
    }
    return result;
}

std::vector<PlayedTrack> SeratoHistoryReader::readRecentHistory(int maxTracks)
{
    try {
        const std::string sessionsDir = seratoSessionsDir();
        juce::File dir(sessionsDir);
        if (!dir.isDirectory()) {
            spdlog::debug("[SeratoHistoryReader] sessions dir not found: {}", sessionsDir);
            return {};
        }

        auto binary = parseBinarySessions(sessionsDir, maxTracks);
        if (!binary.empty()) return binary;

        spdlog::info("[SeratoHistoryReader] binary parse empty, falling back to CSV");
        return parseCsvFallback(sessionsDir, maxTracks);
    } catch (const std::exception& e) {
        spdlog::warn("[SeratoHistoryReader] readRecentHistory failed: {}", e.what());
    } catch (...) {
        spdlog::warn("[SeratoHistoryReader] readRecentHistory failed (unknown)");
    }
    return {};
}

}
