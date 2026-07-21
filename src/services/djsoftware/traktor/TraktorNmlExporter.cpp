#include "TraktorNmlExporter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Traktor {

std::string TraktorNmlExporter::escapeXml(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

int TraktorNmlExporter::keyToCamelotValue(const std::string& key)
{
    // Map Camelot notation to Traktor MUSICAL_KEY values (0-23)
    static const std::map<std::string, int> camelotMap = {
        {"1A",0},{"1B",1},{"2A",2},{"2B",3},{"3A",4},{"3B",5},
        {"4A",6},{"4B",7},{"5A",8},{"5B",9},{"6A",10},{"6B",11},
        {"7A",12},{"7B",13},{"8A",14},{"8B",15},{"9A",16},{"9B",17},
        {"10A",18},{"10B",19},{"11A",20},{"11B",21},{"12A",22},{"12B",23}
    };
    auto it = camelotMap.find(key);
    return it != camelotMap.end() ? it->second : -1;
}

void TraktorNmlExporter::addTrack(const ExportTrack& track) { tracks_.push_back(track); }

TraktorNmlExporter::ExportTrack TraktorNmlExporter::fromBeatMateTrack(
    const Models::Track& t, const std::vector<Models::CuePoint>& cues)
{
    ExportTrack et;
    et.filePath = t.filePath;
    et.title = t.title; et.artist = t.artist; et.album = t.album;
    et.genre = t.genre; et.comment = t.comment;
    et.key = t.camelotKey.empty() ? t.key : t.camelotKey;
    et.bpm = t.bpm; et.duration = t.duration;

    // Append role/venue/tags to Traktor COMMENT (no dedicated field).
    std::string meta;
    if (!t.role.empty())  meta  = "[" + t.role + "]";
    if (!t.venue.empty()) meta += (meta.empty() ? "" : " ") + std::string("@") + t.venue;
    for (const auto& tag : t.myTags) meta += " #" + tag;
    if (!meta.empty()) {
        if (!et.comment.empty()) et.comment += "  ";
        et.comment += meta;
    }

    for (auto& c : cues) {
        ExportTrack::Cue cue;
        using CT = Models::CuePointType;
        if (c.type == CT::Loop)                                   cue.type = 5;
        else if (c.type == CT::IntroStart || c.type == CT::IntroEnd)   cue.type = 1;
        else if (c.type == CT::OutroStart || c.type == CT::OutroEnd)   cue.type = 2;
        else                                                      cue.type = 0;
        cue.startMs = c.position * 1000.0;
        cue.lengthMs = c.length * 1000.0;
        cue.name = c.name;
        cue.hotcue = c.number - 1;
        et.cues.push_back(cue);
    }

    // Emit fade markers from Track cached intro/outro if no explicit cues set them.
    auto addFade = [&](double seconds, int type, const char* name) {
        if (seconds < 0.0) return;
        ExportTrack::Cue cue;
        cue.type = type;
        cue.startMs = seconds * 1000.0;
        cue.lengthMs = 0.0;
        cue.name = name;
        cue.hotcue = -1;
        et.cues.push_back(cue);
    };
    addFade(t.introStart, 1, "Intro In");
    addFade(t.introEnd,   1, "Intro Out");
    addFade(t.outroStart, 2, "Outro In");
    addFade(t.outroEnd,   2, "Outro Out");

    return et;
}

std::string TraktorNmlExporter::generateNml()
{
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    xml << "<NML VERSION=\"19\">\n";
    xml << "  <HEAD COMPANY=\"BeatMate\" PROGRAM=\"BeatMate Pro 12.0\"/>\n";
    xml << "  <COLLECTION ENTRIES=\"" << tracks_.size() << "\">\n";

    for (auto& t : tracks_) {
        xml << "    <ENTRY TITLE=\"" << escapeXml(t.title)
            << "\" ARTIST=\"" << escapeXml(t.artist) << "\">\n";

        // Location — Traktor expects VOLUME="C:" and DIR with "/:" separators
        std::string path = t.filePath;
        for (auto& c : path) if (c == '\\') c = '/';
        std::string volume;
        if (path.size() >= 2 && path[1] == ':') {
            volume = path.substr(0, 2);
            path = path.substr(2);
        }
        size_t lastSlash = path.rfind('/');
        std::string dir = (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "/";
        std::string file = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
        std::string traktorDir;
        traktorDir.reserve(dir.size() * 2);
        for (char c : dir) {
            if (c == '/') traktorDir += "/:";
            else traktorDir += c;
        }
        xml << "      <LOCATION DIR=\"" << escapeXml(traktorDir)
            << "\" FILE=\"" << escapeXml(file)
            << "\" VOLUME=\"" << escapeXml(volume) << "\"/>\n";

        xml << "      <ALBUM TITLE=\"" << escapeXml(t.album) << "\"/>\n";
        xml << "      <INFO GENRE=\"" << escapeXml(t.genre)
            << "\" COMMENT=\"" << escapeXml(t.comment)
            << "\" PLAYTIME=\"" << static_cast<int>(t.duration) << "\"/>\n";

        xml << std::fixed << std::setprecision(6);
        if (t.bpm > 0.0f)
            xml << "      <TEMPO BPM=\"" << t.bpm << "\" BPM_QUALITY=\"100\"/>\n";

        int keyVal = keyToCamelotValue(t.key);
        if (keyVal >= 0)
            xml << "      <MUSICAL_KEY VALUE=\"" << keyVal << "\"/>\n";

        // Beatgrid anchor (TYPE=4 AutoGrid) so Traktor locks the grid to the
        if (t.bpm > 0.0f) {
            xml << "      <CUE_V2 NAME=\"AutoGrid\" DISPL_ORDER=\"0\" TYPE=\"4\" START=\""
                << (t.firstBeatSec >= 0.0 ? t.firstBeatSec * 1000.0 : 0.0)
                << "\" LEN=\"0\" REPEATS=\"-1\" HOTCUE=\"-1\"/>\n";
        }

        for (size_t i = 0; i < t.cues.size(); ++i) {
            auto& c = t.cues[i];
            xml << "      <CUE_V2 NAME=\"" << escapeXml(c.name)
                << "\" DISPL_ORDER=\"" << (i + 1)
                << "\" TYPE=\"" << c.type
                << "\" START=\"" << c.startMs
                << "\" LEN=\"" << c.lengthMs
                << "\" HOTCUE=\"" << c.hotcue << "\"/>\n";
        }

        xml << "    </ENTRY>\n";
    }

    xml << "  </COLLECTION>\n";
    xml << "</NML>\n";
    return xml.str();
}

bool TraktorNmlExporter::exportToFile(const std::string& path)
{
    auto nml = generateNml();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        spdlog::error("[TraktorNmlExporter] Cannot write: {}", path);
        return false;
    }
    f.write(nml.data(), static_cast<std::streamsize>(nml.size()));
    spdlog::info("[TraktorNmlExporter] Exported {} tracks to {}", tracks_.size(), path);
    return true;
}

} // namespace BeatMate::Services::Traktor
