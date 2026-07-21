#include "AbletonExportService.h"
#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>

namespace BeatMate::Services::Export {

bool AbletonExportService::exportALS(const std::string& outputPath,
                                       const std::vector<AbletonTrackInfo>& tracks,
                                       const AbletonProjectOptions& options) {
    std::string xml = generateALSXml(tracks, options);

    if (!compressToGzip(xml, outputPath)) {
        std::ofstream ofs(outputPath);
        if (!ofs.is_open()) {
            spdlog::error("AbletonExportService: Failed to write {}", outputPath);
            return false;
        }
        ofs << xml;
    }

    spdlog::info("AbletonExportService: Exported ALS with {} tracks to {}", tracks.size(), outputPath);
    return true;
}

bool AbletonExportService::exportFromDJSet(const std::string& outputPath,
                                             const std::vector<Models::Track>& djTracks,
                                             const std::vector<double>& startTimes,
                                             double masterBpm) {
    if (masterBpm <= 0.0) {
        spdlog::error("AbletonExportService: refusing DJ set export without a valid master BPM");
        return false;
    }
    AbletonProjectOptions options;
    options.masterBpm = masterBpm;
    options.projectName = "DJ Set Export";

    std::vector<AbletonTrackInfo> abletonTracks;
    for (size_t i = 0; i < djTracks.size(); ++i) {
        AbletonTrackInfo track;
        track.name = djTracks[i].artist + " - " + djTracks[i].title;
        track.color = static_cast<int>(i % 16);

        double startBeat = (i < startTimes.size())
            ? (startTimes[i] / 60.0 * masterBpm)
            : 0.0;

        track.clips.push_back(createClip(djTracks[i], startBeat, masterBpm));
        abletonTracks.push_back(track);
    }

    return exportALS(outputPath, abletonTracks, options);
}

bool AbletonExportService::exportStemsProject(const std::string& outputPath,
                                                const std::string& trackTitle,
                                                const std::vector<std::string>& stemPaths,
                                                const std::vector<std::string>& stemNames,
                                                double bpm) {
    if (bpm <= 0.0) {
        spdlog::error("AbletonExportService: refusing stems export without a valid BPM");
        return false;
    }
    AbletonProjectOptions options;
    options.masterBpm = bpm;
    options.projectName = trackTitle + " - Stems";

    std::vector<AbletonTrackInfo> tracks;
    for (size_t i = 0; i < stemPaths.size(); ++i) {
        AbletonTrackInfo track;
        track.name = (i < stemNames.size()) ? stemNames[i] : ("Stem " + std::to_string(i + 1));
        track.color = static_cast<int>(i % 8);

        AbletonClip clip;
        clip.name = track.name;
        clip.filePath = stemPaths[i];
        clip.startBeat = 0.0;
        clip.bpm = bpm;
        clip.isWarped = true;
        track.clips.push_back(clip);
        tracks.push_back(track);
    }

    return exportALS(outputPath, tracks, options);
}

AbletonClip AbletonExportService::createClip(const Models::Track& track, double startBeat, double fallbackBpm) {
    AbletonClip clip;
    clip.name = track.artist + " - " + track.title;
    clip.filePath = track.filePath;
    clip.startBeat = startBeat;
    clip.bpm = (track.bpm > 0) ? track.bpm : fallbackBpm;
    clip.sampleRate = static_cast<double>(track.sampleRate);
    clip.lengthBeats = (track.duration / 60.0) * clip.bpm;
    clip.isWarped = true;
    return clip;
}

std::string AbletonExportService::generateALSXml(const std::vector<AbletonTrackInfo>& tracks,
                                                    const AbletonProjectOptions& options) const {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<Ableton MajorVersion=\"5\" MinorVersion=\"11.0\" SchemaChangeCount=\"3\" Creator=\"BeatMate\">\n";
    xml << "  <LiveSet>\n";
    xml << "    <NextPointeeId Value=\"" << (tracks.size() * 10 + 100) << "\"/>\n";
    xml << "    <OverwriteProtectionNumber Value=\"2816\"/>\n";

    xml << "    <MasterTrack>\n";
    xml << "      <AutomationEnvelopes/>\n";
    xml << "      <DeviceChain>\n";
    xml << "        <Mixer>\n";
    xml << "          <Tempo><Manual Value=\"" << options.masterBpm << "\"/></Tempo>\n";
    xml << "          <TimeSignature><TimeSignatures><RemoteableTimeSignature>\n";
    xml << "            <Numerator Value=\"" << options.timeSignatureNum << "\"/>\n";
    xml << "            <Denominator Value=\"" << options.timeSignatureDen << "\"/>\n";
    xml << "          </RemoteableTimeSignature></TimeSignatures></TimeSignature>\n";
    xml << "        </Mixer>\n";
    xml << "      </DeviceChain>\n";
    xml << "    </MasterTrack>\n";

    xml << "    <Tracks>\n";
    int trackId = 1;
    for (const auto& track : tracks) {
        xml << "      <AudioTrack Id=\"" << trackId++ << "\">\n";
        xml << "        <Name><EffectiveName Value=\"" << escapeXml(track.name) << "\"/></Name>\n";
        xml << "        <Color Value=\"" << track.color << "\"/>\n";
        xml << "        <DeviceChain>\n";
        xml << "          <Mixer>\n";
        xml << "            <Volume><Manual Value=\"" << track.volume << "\"/></Volume>\n";
        xml << "            <Pan><Manual Value=\"" << track.pan << "\"/></Pan>\n";
        xml << "            <SoloSink Value=\"" << (track.isSoloed ? "true" : "false") << "\"/>\n";
        xml << "          </Mixer>\n";
        xml << "          <MainSequencer>\n";

        int clipId = 1;
        for (const auto& clip : track.clips) {
            xml << "            <ClipSlotList>\n";
            xml << "              <ClipSlot Id=\"" << clipId++ << "\">\n";
            xml << "                <Value>\n";
            xml << "                  <AudioClip>\n";
            xml << "                    <Name Value=\"" << escapeXml(clip.name) << "\"/>\n";
            xml << "                    <CurrentStart Value=\"" << clip.startBeat << "\"/>\n";
            xml << "                    <CurrentEnd Value=\"" << (clip.startBeat + clip.lengthBeats) << "\"/>\n";
            xml << "                    <IsWarped Value=\"" << (clip.isWarped ? "true" : "false") << "\"/>\n";

            std::filesystem::path p(clip.filePath);
            xml << "                    <SampleRef>\n";
            xml << "                      <FileRef>\n";
            xml << "                        <Name Value=\"" << escapeXml(p.filename().string()) << "\"/>\n";
            xml << "                        <RelativePath Value=\"" << escapeXml(clip.filePath) << "\"/>\n";
            xml << "                      </FileRef>\n";
            xml << "                      <DefaultDuration Value=\"" << clip.sampleLength << "\"/>\n";
            xml << "                      <DefaultSampleRate Value=\"" << clip.sampleRate << "\"/>\n";
            xml << "                    </SampleRef>\n";

            xml << "                    <WarpMarkers>\n";
            xml << "                      <WarpMarker SecTime=\"0\" BeatTime=\"0\"/>\n";
            double endSec = clip.lengthBeats / clip.bpm * 60.0;
            xml << "                      <WarpMarker SecTime=\"" << endSec << "\" BeatTime=\"" << clip.lengthBeats << "\"/>\n";
            xml << "                    </WarpMarkers>\n";

            xml << "                  </AudioClip>\n";
            xml << "                </Value>\n";
            xml << "              </ClipSlot>\n";
            xml << "            </ClipSlotList>\n";
        }

        xml << "          </MainSequencer>\n";
        xml << "        </DeviceChain>\n";
        xml << "      </AudioTrack>\n";
    }
    xml << "    </Tracks>\n";

    if (options.createReturnTracks) {
        xml << "    <SendsPre/>\n";
    }

    xml << "  </LiveSet>\n";
    xml << "</Ableton>\n";

    return xml.str();
}

std::string AbletonExportService::escapeXml(const std::string& s) const {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c; break;
        }
    }
    return result;
}

bool AbletonExportService::compressToGzip(const std::string& xmlContent,
                                            const std::string& outputPath) const {
    juce::MemoryBlock mb(xmlContent.data(), xmlContent.size());
    juce::MemoryOutputStream mos;
    {
        juce::GZIPCompressorOutputStream gzip(mos);
        gzip.write(xmlContent.data(), xmlContent.size());
    }

    juce::File outFile(outputPath);
    auto fos = outFile.createOutputStream();
    if (!fos) return false;
    fos->write(mos.getData(), mos.getDataSize());
    return true;
}

}
