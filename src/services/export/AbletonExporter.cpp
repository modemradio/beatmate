#include "AbletonExporter.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <sstream>

namespace BeatMate::Services::Export {

namespace {

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:  out += c; break;
        }
    }
    return out;
}

std::string beatsToSeconds(double seconds, double bpm) {
    const double beats = seconds * (bpm / 60.0);
    std::ostringstream oss;
    oss.precision(6);
    oss << std::fixed << beats;
    return oss.str();
}

} // namespace

std::string AbletonExporter::buildXml(const Project& project) const {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<Ableton MajorVersion=\"5\" MinorVersion=\"11.0_11202\" "
           "SchemaChangeCount=\"3\" Creator=\"BeatMate V12\" Revision=\"beatmate\">\n";
    xml << "  <LiveSet>\n";
    xml << "    <Tempo>\n";
    xml << "      <Manual Value=\"" << project.tempo << "\"/>\n";
    xml << "    </Tempo>\n";
    xml << "    <TimeSignature>\n";
    xml << "      <Numerator Value=\"" << project.timeSigNum << "\"/>\n";
    xml << "      <Denominator Value=\"" << project.timeSigDen << "\"/>\n";
    xml << "    </TimeSignature>\n";
    xml << "    <Tracks>\n";

    int trackId = 0;
    int clipIdGlobal = 0;
    for (const auto& track : project.tracks) {
        xml << "      <AudioTrack Id=\"" << trackId << "\">\n";
        xml << "        <Name>\n";
        xml << "          <EffectiveName Value=\"" << xmlEscape(track.name) << "\"/>\n";
        xml << "          <UserName Value=\"" << xmlEscape(track.name) << "\"/>\n";
        xml << "        </Name>\n";
        xml << "        <DeviceChain>\n";
        xml << "          <MainSequencer>\n";
        xml << "            <Sample>\n";
        xml << "              <ArrangerAutomation>\n";
        xml << "                <Events>\n";

        for (const auto& clip : track.clips) {
            const double durSec = std::max(0.0, clip.endSec - clip.startSec);
            const double startBeats = clip.startSec * project.tempo / 60.0;
            const double endBeats   = (clip.startSec + durSec) * project.tempo / 60.0;

            juce::File f{juce::String(clip.filePath)};
            const juce::String fileName = f.getFileName();
            const juce::String absPath  = f.getFullPathName();

            xml << "                  <AudioClip Id=\"" << clipIdGlobal++
                << "\" Time=\"" << startBeats << "\">\n";
            xml << "                    <Name Value=\"" << xmlEscape(clip.name) << "\"/>\n";
            xml << "                    <CurrentStart Value=\"" << startBeats << "\"/>\n";
            xml << "                    <CurrentEnd Value=\"" << endBeats << "\"/>\n";
            xml << "                    <Loop>\n";
            xml << "                      <LoopStart Value=\"0\"/>\n";
            xml << "                      <LoopEnd Value=\"" << beatsToSeconds(durSec, project.tempo) << "\"/>\n";
            xml << "                      <StartRelative Value=\"0\"/>\n";
            xml << "                      <LoopOn Value=\"false\"/>\n";
            xml << "                    </Loop>\n";
            xml << "                    <SampleRef>\n";
            xml << "                      <FileRef>\n";
            xml << "                        <RelativePathType Value=\"3\"/>\n";
            xml << "                        <RelativePath Value=\""
                << xmlEscape(fileName.toStdString()) << "\"/>\n";
            xml << "                        <Path Value=\""
                << xmlEscape(absPath.toStdString()) << "\"/>\n";
            xml << "                        <Type Value=\"1\"/>\n";
            xml << "                      </FileRef>\n";
            xml << "                    </SampleRef>\n";
            if (clip.warpBpm > 0) {
                xml << "                    <WarpMode Value=\"4\"/>\n";
                xml << "                    <WarpMarkers>\n";
                xml << "                      <WarpMarker SecTime=\"0\" BeatTime=\"0\"/>\n";
                xml << "                      <WarpMarker SecTime=\"" << durSec
                    << "\" BeatTime=\"" << (durSec * clip.warpBpm / 60.0) << "\"/>\n";
                xml << "                    </WarpMarkers>\n";
            }
            xml << "                  </AudioClip>\n";
        }

        xml << "                </Events>\n";
        xml << "              </ArrangerAutomation>\n";
        xml << "            </Sample>\n";
        xml << "          </MainSequencer>\n";
        xml << "        </DeviceChain>\n";
        xml << "      </AudioTrack>\n";
        ++trackId;
    }

    xml << "    </Tracks>\n";
    xml << "  </LiveSet>\n";
    xml << "</Ableton>\n";
    return xml.str();
}

bool AbletonExporter::exportToFile(const Project& project, const std::string& alsPath) const {
    if (alsPath.empty()) return false;

    const std::string xml = buildXml(project);

    juce::File out{juce::String(alsPath)};
    out.getParentDirectory().createDirectory();
    out.deleteFile();

    juce::FileOutputStream raw{out};
    if (!raw.openedOk()) {
        spdlog::error("AbletonExporter: cannot open {}", alsPath);
        return false;
    }

    juce::GZIPCompressorOutputStream gz(&raw, 6,
        false,
        juce::GZIPCompressorOutputStream::windowBitsGZIP);
    if (!gz.write(xml.data(), xml.size())) {
        spdlog::error("AbletonExporter: gzip write failed");
        return false;
    }
    gz.flush();
    raw.flush();
    spdlog::info("AbletonExporter: wrote {} ({} bytes xml, {} tracks)",
                 alsPath, xml.size(), project.tracks.size());
    return true;
}

} // namespace BeatMate::Services::Export
