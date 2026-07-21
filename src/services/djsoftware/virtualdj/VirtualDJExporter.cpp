#include "VirtualDJExporter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::VirtualDJ {

std::string VirtualDJExporter::escapeXml(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s) {
        switch(c) { case '&': r+="&amp;"; break; case '<': r+="&lt;"; break;
                     case '>': r+="&gt;"; break; case '"': r+="&quot;"; break;
                     default: r+=c; }
    }
    return r;
}

void VirtualDJExporter::addTrack(const ExportTrack& t) { tracks_.push_back(t); }

VirtualDJExporter::ExportTrack VirtualDJExporter::fromBeatMateTrack(
    const Models::Track& t, const std::vector<Models::CuePoint>& cues) {
    ExportTrack et;
    et.filePath = t.filePath; et.title = t.title; et.artist = t.artist;
    et.album = t.album; et.genre = t.genre; et.comment = t.comment;
    et.key = t.camelotKey.empty() ? t.key : t.camelotKey;
    et.bpm = t.bpm; et.duration = t.duration;

    // Pack pro metadata into the comment field (VirtualDJ has no dedicated
    std::string meta;
    if (!t.role.empty())  meta  = "[" + t.role + "]";
    if (!t.venue.empty()) meta += (meta.empty() ? "" : " ") + std::string("@") + t.venue;
    for (const auto& tag : t.myTags) meta += " #" + tag;
    if (!meta.empty()) {
        if (!et.comment.empty()) et.comment += "  ";
        et.comment += meta;
    }

    for (auto& c : cues) {
        ExportTrack::Poi p;
        using CT = Models::CuePointType;
        if (c.type == CT::Loop)                                     p.type = "loop";
        else if (c.type == CT::IntroStart || c.type == CT::IntroEnd ||
                 c.type == CT::OutroStart || c.type == CT::OutroEnd)
            p.type = "automix";
        else                                                        p.type = "cue";
        p.position = c.position;
        p.length = c.length;
        p.name = c.name;
        p.number = c.number;
        et.pois.push_back(p);
    }

    // Automix in/out from cached fields (VirtualDJ uses "automix" POI type
    auto addAutomix = [&](double seconds, const char* name) {
        if (seconds < 0.0) return;
        ExportTrack::Poi p;
        p.type = "automix";
        p.position = seconds;
        p.length = 0.0;
        p.name = name;
        p.number = 0;
        et.pois.push_back(p);
    };
    addAutomix(t.introStart, "Intro In");
    addAutomix(t.introEnd,   "Intro Out");
    addAutomix(t.outroStart, "Outro In");
    addAutomix(t.outroEnd,   "Outro Out");

    return et;
}

std::string VirtualDJExporter::buildSongXml(const ExportTrack& t) {
    std::ostringstream xml;
    xml << "  <Song FilePath=\"" << escapeXml(t.filePath) << "\">\n";
    xml << "    <Tags Author=\"" << escapeXml(t.artist)
        << "\" Title=\"" << escapeXml(t.title)
        << "\" Genre=\"" << escapeXml(t.genre)
        << "\" Album=\"" << escapeXml(t.album)
        << "\" Key=\"" << escapeXml(t.key) << "\"";
    if (t.bpm > 0)
        xml << std::fixed << std::setprecision(6) << " Bpm=\"" << (60.0 / t.bpm) << "\"";
    xml << "/>\n";

    xml << "    <Infos SongLength=\"" << std::fixed << std::setprecision(2)
        << t.duration << "\"/>\n";

    if (t.bpm > 0) {
        xml << "    <Scan Version=\"801\" Bpm=\"" << std::fixed << std::setprecision(6)
            << (60.0 / t.bpm) << "\"";
        if (!t.key.empty()) xml << " Key=\"" << escapeXml(t.key) << "\"";
        if (t.firstBeatSec >= 0.0)
            xml << " FirstBeat=\"" << std::fixed << std::setprecision(4) << t.firstBeatSec << "\"";
        xml << "/>\n";
    }

    if (!t.comment.empty())
        xml << "    <Comment>" << escapeXml(t.comment) << "</Comment>\n";

    // POIs (Points of Interest)
    for (auto& p : t.pois) {
        xml << "    <Poi Type=\"" << p.type
            << "\" Pos=\"" << std::fixed << std::setprecision(4) << p.position << "\"";
        if (p.length > 0)
            xml << " Length=\"" << p.length << "\"";
        if (!p.name.empty())
            xml << " Name=\"" << escapeXml(p.name) << "\"";
        xml << " Num=\"" << p.number << "\"/>\n";
    }

    xml << "  </Song>\n";
    return xml.str();
}

bool VirtualDJExporter::exportToFile(const std::string& path) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<VirtualDJ_Database Version=\"8\">\n";
    for (auto& t : tracks_)
        xml << buildSongXml(t);
    xml << "</VirtualDJ_Database>\n";

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) { spdlog::error("[VDJ] Cannot write: {}", path); return false; }
    auto s = xml.str();
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    spdlog::info("[VDJ] Exported {} tracks to {}", tracks_.size(), path);
    return true;
}

bool VirtualDJExporter::exportToDatabase(const std::string& vdjDatabaseXmlPath) {
    juce::File dbFile{ juce::String(vdjDatabaseXmlPath) };
    if (!dbFile.existsAsFile()) {
        spdlog::warn("[VDJ] database.xml introuvable ({}) - export fichier autonome a la place",
                     vdjDatabaseXmlPath);
        return exportToFile(vdjDatabaseXmlPath);
    }

    auto xml = juce::parseXML(dbFile);
    if (!xml || !xml->hasTagName("VirtualDJ_Database")) {
        spdlog::error("[VDJ] database.xml illisible ou format inattendu: {}", vdjDatabaseXmlPath);
        return false;
    }

    int updated = 0, added = 0;
    for (const auto& t : tracks_) {
        auto songXml = juce::parseXML(juce::String(buildSongXml(t)));
        if (!songXml) continue;

        juce::XmlElement* existing = nullptr;
        for (auto* song : xml->getChildWithTagNameIterator("Song")) {
            if (song->getStringAttribute("FilePath")
                    .equalsIgnoreCase(juce::String(t.filePath))) {
                existing = song;
                break;
            }
        }
        if (existing) {
            xml->replaceChildElement(existing, songXml.release());
            ++updated;
        } else {
            xml->addChildElement(songXml.release());
            ++added;
        }
    }

    auto backup = dbFile.getSiblingFile(dbFile.getFileName() + ".bak");
    backup.deleteFile();
    dbFile.copyFileTo(backup);

    if (!xml->writeTo(dbFile, {})) {
        spdlog::error("[VDJ] Ecriture database.xml impossible: {}", vdjDatabaseXmlPath);
        return false;
    }
    spdlog::info("[VDJ] database.xml fusionne: {} maj, {} ajout(s) (backup: {})",
                 updated, added, backup.getFullPathName().toStdString());
    return true;
}

} // namespace BeatMate::Services::VirtualDJ
