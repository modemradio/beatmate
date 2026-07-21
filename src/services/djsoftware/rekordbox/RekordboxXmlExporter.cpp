#include "RekordboxXmlExporter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Rekordbox {

void RekordboxXmlExporter::addTrack(const ExportTrack& track)
{
    tracks_.push_back(track);
}

void RekordboxXmlExporter::addPlaylist(const ExportPlaylist& playlist)
{
    playlists_.push_back(playlist);
}

std::string RekordboxXmlExporter::escapeXml(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

std::string RekordboxXmlExporter::colorToRGB(const std::string& hexColor, const std::string& attr)
{
    if (hexColor.size() < 7 || hexColor[0] != '#') return "";
    unsigned int r = 0, g = 0, b = 0;
    std::sscanf(hexColor.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return " " + attr + "Red=\"" + std::to_string(r)
         + "\" " + attr + "Green=\"" + std::to_string(g)
         + "\" " + attr + "Blue=\"" + std::to_string(b) + "\"";
}

RekordboxXmlExporter::ExportTrack RekordboxXmlExporter::fromBeatMateTrack(
    const Models::Track& track, const std::vector<Models::CuePoint>& cues)
{
    ExportTrack et;
    et.trackId = track.id;
    et.filePath = track.filePath;
    et.title = track.title;
    et.artist = track.artist;
    et.album = track.album;
    et.genre = track.genre;
    et.comment = track.comment;
    et.key = track.camelotKey.empty() ? track.key : track.camelotKey;
    et.bpm = track.bpm;
    et.rating = static_cast<int>(track.rating * 51); // 0-5 -> 0-255
    et.duration = track.duration;
    et.year = track.year;
    et.label = track.label;

    // Grouping = role + venue + tags (visible dans Rekordbox)
    {
        std::string g;
        if (!track.role.empty())  g  = "[" + track.role + "]";
        if (!track.venue.empty()) g += (g.empty() ? "" : " ") + std::string("@") + track.venue;
        for (const auto& tag : track.myTags) g += " #" + tag;
        et.grouping = g;
    }

    // Append tags to Comments so DJ apps without a Grouping column still see them.
    if (!track.myTags.empty()) {
        std::string tagStr;
        for (const auto& tag : track.myTags) { tagStr += "#" + tag + " "; }
        if (!et.comment.empty()) et.comment += "  ";
        et.comment += tagStr;
    }

    // Intro/Outro markers -> POSITION_MARK Type=1 (Fade-In) / Type=2 (Fade-Out).
    auto addMarker = [&](double seconds, int type, const char* name) {
        if (seconds < 0.0) return;
        ExportTrack::CuePointExport cp;
        cp.number = -1;  // Memory cue / fade marker (not a hot cue)
        cp.type = type;
        cp.startMs = seconds * 1000.0;
        cp.endMs = -1.0;
        cp.name = name;
        cp.red = 0; cp.green = 204; cp.blue = 204;
        et.cuePoints.push_back(cp);
    };
    addMarker(track.introStart, 1, "Intro In");
    addMarker(track.introEnd,   1, "Intro Out");
    addMarker(track.outroStart, 2, "Outro In");
    addMarker(track.outroEnd,   2, "Outro Out");

    for (auto& cue : cues) {
        ExportTrack::CuePointExport cp;
        cp.number = cue.number - 1; // BeatMate 1-based -> Rekordbox 0-based
        using CT = Models::CuePointType;
        if (cue.type == CT::Loop)                                     cp.type = 4;
        else if (cue.type == CT::IntroStart || cue.type == CT::IntroEnd)   cp.type = 1;
        else if (cue.type == CT::OutroStart || cue.type == CT::OutroEnd)   cp.type = 2;
        else                                                          cp.type = 0;
        cp.startMs = cue.position * 1000.0;
        cp.endMs = (cue.length > 0) ? (cue.position + cue.length) * 1000.0 : -1.0;
        cp.name = cue.name;

        if (cue.color.size() >= 7 && cue.color[0] == '#') {
            unsigned int r = 0, g = 0, b = 0;
            std::sscanf(cue.color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            cp.red = static_cast<uint8_t>(r);
            cp.green = static_cast<uint8_t>(g);
            cp.blue = static_cast<uint8_t>(b);
        } else {
            static const uint8_t defaultColors[][3] = {
                {0xC0,0x29,0x28}, {0xF8,0x70,0x00}, {0xF0,0xDB,0x00}, {0x1F,0xAD,0x26},
                {0x03,0x94,0xBC}, {0x28,0x60,0xD7}, {0x92,0x3D,0xC8}, {0xE4,0x48,0x7E}
            };
            int idx = std::min(cp.number, 7);
            cp.red = defaultColors[idx][0];
            cp.green = defaultColors[idx][1];
            cp.blue = defaultColors[idx][2];
        }

        et.cuePoints.push_back(cp);
    }

    return et;
}

std::string RekordboxXmlExporter::generateXml()
{
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<DJ_PLAYLISTS Version=\"1.0.0\">\n";
    xml << "  <PRODUCT Name=\"BeatMate Pro\" Version=\"12.0.0\" Company=\"BeatMate\"/>\n";
    xml << "  <COLLECTION Entries=\"" << tracks_.size() << "\">\n";

    for (auto& t : tracks_) {
        xml << "    <TRACK TrackID=\"" << t.trackId << "\"";
        xml << " Name=\"" << escapeXml(t.title) << "\"";
        xml << " Artist=\"" << escapeXml(t.artist) << "\"";
        xml << " Album=\"" << escapeXml(t.album) << "\"";
        xml << " Genre=\"" << escapeXml(t.genre) << "\"";
        xml << " Comments=\"" << escapeXml(t.comment) << "\"";
        xml << " Tonality=\"" << escapeXml(t.key) << "\"";

        // File path: Rekordbox uses file:///localhost/ prefix
        std::string loc = t.filePath;
        for (auto& c : loc) if (c == '\\') c = '/';
        xml << " Location=\"file://localhost/" << escapeXml(loc) << "\"";

        xml << std::fixed << std::setprecision(2);
        xml << " AverageBpm=\"" << t.bpm << "\"";
        xml << " TotalTime=\"" << static_cast<int>(t.duration) << "\"";
        xml << " Rating=\"" << t.rating << "\"";
        xml << " SampleRate=\"" << t.sampleRate << "\"";
        xml << " BitRate=\"" << t.bitRate << "\"";
        if (t.year > 0) xml << " Year=\"" << t.year << "\"";
        if (!t.remixer.empty()) xml << " Remixer=\"" << escapeXml(t.remixer) << "\"";
        if (!t.label.empty()) xml << " Label=\"" << escapeXml(t.label) << "\"";
        if (!t.grouping.empty()) xml << " Grouping=\"" << escapeXml(t.grouping) << "\"";
        if (!t.dateAdded.empty()) xml << " DateAdded=\"" << escapeXml(t.dateAdded) << "\"";

        const bool hasTempo = t.bpm > 0.0f;
        if (t.cuePoints.empty() && !hasTempo) {
            xml << "/>\n";
        } else {
            xml << ">\n";
            if (hasTempo) {
                xml << std::fixed << std::setprecision(3);
                xml << "      <TEMPO Inizio=\""
                    << (t.firstBeatSec >= 0.0 ? t.firstBeatSec : 0.0)
                    << "\" Bpm=\"" << t.bpm
                    << "\" Metro=\"4/4\" Battito=\"1\"/>\n";
            }
            for (auto& cp : t.cuePoints) {
                xml << "      <POSITION_MARK";
                xml << " Name=\"" << escapeXml(cp.name) << "\"";
                xml << " Type=\"" << cp.type << "\"";
                xml << " Num=\"" << cp.number << "\"";
                xml << std::fixed << std::setprecision(3);
                xml << " Start=\"" << cp.startMs << "\"";
                if (cp.endMs > 0) xml << " End=\"" << cp.endMs << "\"";
                xml << " Red=\"" << static_cast<int>(cp.red) << "\"";
                xml << " Green=\"" << static_cast<int>(cp.green) << "\"";
                xml << " Blue=\"" << static_cast<int>(cp.blue) << "\"";
                xml << "/>\n";
            }
            xml << "    </TRACK>\n";
        }
    }

    xml << "  </COLLECTION>\n";

    xml << "  <PLAYLISTS>\n";
    xml << "    <NODE Type=\"0\" Name=\"ROOT\" Count=\"" << playlists_.size() << "\">\n";
    for (auto& pl : playlists_) {
        xml << "      <NODE Name=\"" << escapeXml(pl.name) << "\" Type=\"1\" KeyType=\"0\" Entries=\""
            << pl.trackIds.size() << "\">\n";
        for (auto id : pl.trackIds) {
            xml << "        <TRACK Key=\"" << id << "\"/>\n";
        }
        xml << "      </NODE>\n";
    }
    xml << "    </NODE>\n";
    xml << "  </PLAYLISTS>\n";
    xml << "</DJ_PLAYLISTS>\n";

    return xml.str();
}

bool RekordboxXmlExporter::exportToFile(const std::string& outputPath)
{
    std::string xml = generateXml();

    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("[RekordboxXmlExporter] Cannot open file: {}", outputPath);
        return false;
    }

    file.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    file.close();

    spdlog::info("[RekordboxXmlExporter] Exported {} tracks, {} playlists to {}",
                 tracks_.size(), playlists_.size(), outputPath);
    return true;
}

}
