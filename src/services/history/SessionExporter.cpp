#include "SessionExporter.h"

#include "../library/TrackDatabase.h"
#include "../djsoftware/rekordbox/RekordboxXmlExporter.h"
#include "../djsoftware/traktor/TraktorNmlExporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace BeatMate::Services::History {

namespace {

std::string csvEscape(const std::string& in) {
    const bool needsQuotes = in.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes) return in;
    std::string out;
    out.reserve(in.size() + 2);
    out.push_back('"');
    for (char c : in) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string formatTimestamp(int64_t ts) {
    if (ts <= 0) return "";
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void writeBE32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void writeTag(std::vector<uint8_t>& buf, const char tag[5]) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(tag[i]));
}

std::vector<uint8_t> utf16BE(const std::string& utf8) {
    std::vector<uint8_t> out;
    juce::String s = juce::String::fromUTF8(utf8.c_str());
    auto it = s.getCharPointer();
    while (!it.isEmpty()) {
        juce::juce_wchar c = it.getAndAdvance();
        if (c == 0) break;
        if (c > 0xFFFF) c = '?';
        uint16_t u = static_cast<uint16_t>(c);
        out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    return out;
}

void appendChunk(std::vector<uint8_t>& buf, const char tag[5],
                 const std::vector<uint8_t>& payload) {
    writeTag(buf, tag);
    writeBE32(buf, static_cast<uint32_t>(payload.size()));
    buf.insert(buf.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> buildSeratoCrate(const std::vector<std::string>& paths) {
    std::vector<uint8_t> buf;
    auto vrsn = utf16BE("1.0/Serato ScratchLive Crate");
    appendChunk(buf, "vrsn", vrsn);
    for (const auto& p : paths) {
        std::vector<uint8_t> otrk;
        appendChunk(otrk, "ptrk", utf16BE(p));
        appendChunk(buf, "otrk", otrk);
    }
    return buf;
}

} // namespace

SessionExporter::SessionExporter(
    std::shared_ptr<SessionHistoryRecorder> recorder,
    std::shared_ptr<Services::Library::TrackDatabase> db)
  : m_recorder(std::move(recorder)), m_db(std::move(db)) {}

std::vector<SessionExporter::EventTrack>
SessionExporter::hydrate(const std::string& sessionId) const {
    std::vector<EventTrack> out;
    if (!m_recorder || !m_db) return out;

    auto events = m_recorder->getSessionEvents(sessionId);
    if (events.empty()) return out;

    std::string ids;
    ids.reserve(events.size() * 8);
    for (size_t i = 0; i < events.size(); ++i) {
        if (i) ids.push_back(',');
        ids += std::to_string(events[i].trackId);
    }

    std::unordered_map<int64_t, Models::Track> byId;
    try {
        auto rows = m_db->getTracksByQuery(
            "SELECT * FROM tracks WHERE id IN (" + ids + ")");
        for (auto& t : rows) byId.emplace(t.id, std::move(t));
    } catch (...) {
        return out;
    }

    out.reserve(events.size());
    for (auto& e : events) {
        EventTrack et;
        et.event = std::move(e);
        auto it = byId.find(et.event.trackId);
        if (it != byId.end()) et.track = it->second;
        out.push_back(std::move(et));
    }
    return out;
}

bool SessionExporter::exportAsM3U8(const std::string& sessionId,
                                   const juce::File& outFile) {
    auto rows = hydrate(sessionId);
    if (rows.empty()) return false;

    juce::String body;
    body << "#EXTM3U\n";
    body << "#PLAYLIST:BeatMate session " << juce::String(sessionId) << "\n";
    for (const auto& r : rows) {
        const int durSec = (int) std::round(r.track.duration);
        body << "#EXTINF:" << durSec
             << "," << juce::String(r.track.artist)
             << " - " << juce::String(r.track.title) << "\n";
        body << juce::String(r.track.filePath) << "\n";
    }
    outFile.getParentDirectory().createDirectory();
    return outFile.replaceWithText(body, false, false);
}

bool SessionExporter::exportAsRekordboxXML(const std::string& sessionId,
                                           const juce::File& outFile) {
    auto rows = hydrate(sessionId);
    if (rows.empty()) return false;

    Services::Rekordbox::RekordboxXmlExporter xml;
    Services::Rekordbox::RekordboxXmlExporter::ExportPlaylist pl;
    pl.name = "BeatMate Session " + sessionId;

    for (const auto& r : rows) {
        // Réutilise la conversion canonique (titres/clés/cues cohérents).
        auto et = Services::Rekordbox::RekordboxXmlExporter::fromBeatMateTrack(r.track, {});
        if (et.trackId == 0) et.trackId = r.track.id;
        xml.addTrack(et);
        pl.trackIds.push_back(et.trackId);
    }
    xml.addPlaylist(pl);

    outFile.getParentDirectory().createDirectory();
    return xml.exportToFile(outFile.getFullPathName().toStdString());
}

bool SessionExporter::exportAsSeratoCrate(const std::string& sessionId,
                                          const juce::File& outFile) {
    auto rows = hydrate(sessionId);
    if (rows.empty()) return false;

    std::vector<std::string> paths;
    paths.reserve(rows.size());
    for (const auto& r : rows)
        if (!r.track.filePath.empty()) paths.push_back(r.track.filePath);
    if (paths.empty()) return false;

    auto bytes = buildSeratoCrate(paths);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    const void* ptr = bytes.empty() ? nullptr : static_cast<const void*>(bytes.data());
    return outFile.replaceWithData(ptr, bytes.size());
}

bool SessionExporter::exportAsTraktorNML(const std::string& sessionId,
                                         const juce::File& outFile) {
    auto rows = hydrate(sessionId);
    if (rows.empty()) return false;

    Services::Traktor::TraktorNmlExporter nml;
    for (const auto& r : rows) {
        nml.addTrack(Services::Traktor::TraktorNmlExporter::fromBeatMateTrack(r.track, {}));
    }
    outFile.getParentDirectory().createDirectory();
    return nml.exportToFile(outFile.getFullPathName().toStdString());
}

bool SessionExporter::exportAsCSV(const std::string& sessionId,
                                  const juce::File& outFile) {
    auto rows = hydrate(sessionId);
    if (rows.empty()) return false;

    std::ostringstream oss;
    oss << "timestamp,title,artist,bpm,key,energy\n";
    for (const auto& r : rows) {
        const std::string camelot = !r.track.camelotKey.empty()
            ? r.track.camelotKey : r.track.key;

        oss << csvEscape(formatTimestamp(r.event.playedAt)) << ','
            << csvEscape(r.track.title)  << ','
            << csvEscape(r.track.artist) << ','
            << (r.track.bpm > 0.0 ? std::to_string(r.track.bpm) : std::string("")) << ','
            << csvEscape(camelot) << ','
            << (r.track.energy > 0.0f ? std::to_string(r.track.energy) : std::string(""))
            << '\n';
    }

    outFile.getParentDirectory().createDirectory();
    return outFile.replaceWithText(juce::String(oss.str()), false, false);
}

} // namespace BeatMate::Services::History
