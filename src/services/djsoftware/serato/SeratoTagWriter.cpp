#include "SeratoTagWriter.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <spdlog/spdlog.h>

#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/frames/generalencapsulatedobjectframe.h>
#include <taglib/toolkit/tbytevector.h>

namespace BeatMate::Services::Serato {

void SeratoTagWriter::writeUint32BE(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

void SeratoTagWriter::writeUint8(std::vector<uint8_t>& buf, uint8_t value) {
    buf.push_back(value);
}

void SeratoTagWriter::writeString(std::vector<uint8_t>& buf, const std::string& s) {
    for (char c : s) buf.push_back(static_cast<uint8_t>(c));
    buf.push_back(0);
}

std::string SeratoTagWriter::seratoBase64Encode(const std::vector<uint8_t>& data) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() * 4 / 3) + 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? table[n & 0x3F] : '=';
    }
    return result;
}

std::vector<SeratoTagWriter::SeratoCue> SeratoTagWriter::fromBeatMateCues(
    const std::vector<Models::CuePoint>& cues)
{
    std::vector<SeratoCue> result;
    for (auto& c : cues) {
        if (c.type == Models::CuePointType::Loop) continue;
        SeratoCue sc;
        sc.index = c.number - 1;
        sc.positionMs = c.position * 1000.0;
        sc.name = c.name;
        if (c.color.size() >= 7 && c.color[0] == '#') {
            unsigned int r = 0, g = 0, b = 0;
            std::sscanf(c.color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            sc.r = static_cast<uint8_t>(r);
            sc.g = static_cast<uint8_t>(g);
            sc.b = static_cast<uint8_t>(b);
        } else {
            static const uint8_t defaults[][3] = {
                {0xCC,0x00,0x00},{0xCC,0x88,0x00},{0xCC,0xCC,0x00},{0x00,0xCC,0x00},
                {0x00,0xCC,0xCC},{0x00,0x00,0xCC},{0xCC,0x00,0xCC},{0xCC,0x00,0x88}
            };
            int idx = std::min(sc.index, 7);
            sc.r = defaults[idx][0]; sc.g = defaults[idx][1]; sc.b = defaults[idx][2];
        }
        result.push_back(sc);
    }
    return result;
}

std::vector<uint8_t> SeratoTagWriter::generateMarkers2(
    const std::vector<SeratoCue>& cues, const std::vector<SeratoLoop>& loops)
{

    std::vector<uint8_t> payload;

    payload.push_back(0x01);
    payload.push_back(0x01);

    for (auto& cue : cues) {
        writeString(payload, "CUE");

        std::vector<uint8_t> entry;
        writeUint8(entry, 0x00);
        writeUint8(entry, static_cast<uint8_t>(cue.index));
        writeUint32BE(entry, static_cast<uint32_t>(cue.positionMs));
        writeUint8(entry, 0x00);
        writeUint8(entry, cue.r);
        writeUint8(entry, cue.g);
        writeUint8(entry, cue.b);
        writeUint8(entry, 0x00);
        writeUint8(entry, 0x00);
        writeString(entry, cue.name);

        writeUint32BE(payload, static_cast<uint32_t>(entry.size()));
        payload.insert(payload.end(), entry.begin(), entry.end());
    }

    for (auto& loop : loops) {
        writeString(payload, "LOOP");

        std::vector<uint8_t> entry;
        writeUint8(entry, 0x00);
        writeUint8(entry, static_cast<uint8_t>(loop.index));
        writeUint32BE(entry, static_cast<uint32_t>(loop.startMs));
        writeUint32BE(entry, static_cast<uint32_t>(loop.endMs));
        writeUint32BE(entry, 0xFFFFFFFF);
        writeUint8(entry, 0x00);
        writeUint8(entry, loop.r);
        writeUint8(entry, loop.g);
        writeUint8(entry, loop.b);
        writeUint8(entry, loop.locked ? 0x01 : 0x00);
        writeString(entry, loop.name);

        writeUint32BE(payload, static_cast<uint32_t>(entry.size()));
        payload.insert(payload.end(), entry.begin(), entry.end());
    }

    payload.push_back(0x00);
    return payload;
}

static void removeSeratoGeob(TagLib::ID3v2::Tag* tag, const TagLib::String& description)
{
    std::vector<TagLib::ID3v2::Frame*> doomed;
    for (auto* frame : tag->frameList("GEOB")) {
        auto* geob = dynamic_cast<TagLib::ID3v2::GeneralEncapsulatedObjectFrame*>(frame);
        if (geob && geob->description() == description)
            doomed.push_back(frame);
    }
    for (auto* frame : doomed)
        tag->removeFrame(frame, true);
}

static void addSeratoGeob(TagLib::ID3v2::Tag* tag, const TagLib::String& description,
                          const std::vector<uint8_t>& data)
{
    auto* geob = new TagLib::ID3v2::GeneralEncapsulatedObjectFrame();
    geob->setTextEncoding(TagLib::String::Latin1);
    geob->setMimeType("application/octet-stream");
    geob->setFileName("");
    geob->setDescription(description);
    geob->setObject(TagLib::ByteVector(reinterpret_cast<const char*>(data.data()),
                                       (unsigned int) data.size()));
    tag->addFrame(geob);
}

bool SeratoTagWriter::writeToFile(const std::string& filePath,
                                    const std::vector<SeratoCue>& cues,
                                    const std::vector<SeratoLoop>& loops,
                                    float bpm,
                                    double firstBeatSec)
{
    auto markers2 = generateMarkers2(cues, loops);
    auto encoded = seratoBase64Encode(markers2);

    std::vector<uint8_t> markersGeob;
    markersGeob.push_back(0x01);
    markersGeob.push_back(0x01);
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (i > 0 && i % 72 == 0) markersGeob.push_back('\n');
        markersGeob.push_back(static_cast<uint8_t>(encoded[i]));
    }
    markersGeob.push_back(0x00);

    std::string ext;
    {
        auto dot = filePath.find_last_of('.');
        if (dot != std::string::npos) ext = filePath.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char) std::tolower(c); });
    }

    if (ext == "mp3") {
#ifdef _WIN32
        std::filesystem::path fsPath = std::filesystem::u8path(filePath);
        TagLib::MPEG::File file(fsPath.wstring().c_str());
#else
        TagLib::MPEG::File file(filePath.c_str());
#endif
        if (!file.isValid()) {
            spdlog::error("[SeratoTagWriter] Cannot open MP3 for tagging: {}", filePath);
            return false;
        }
        auto* tag = file.ID3v2Tag(true);
        if (!tag) {
            spdlog::error("[SeratoTagWriter] No ID3v2 tag available: {}", filePath);
            return false;
        }

        removeSeratoGeob(tag, "Serato Markers2");
        addSeratoGeob(tag, "Serato Markers2", markersGeob);

        if (bpm > 0.0f) {
            std::vector<uint8_t> grid;
            grid.push_back(0x01);
            grid.push_back(0x00);
            writeUint32BE(grid, 1u);
            const float pos = (float) (firstBeatSec >= 0.0 ? firstBeatSec : 0.0);
            uint32_t posBits = 0, bpmBits = 0;
            std::memcpy(&posBits, &pos, sizeof(float));
            std::memcpy(&bpmBits, &bpm, sizeof(float));
            writeUint32BE(grid, posBits);
            writeUint32BE(grid, bpmBits);
            grid.push_back(0x00);
            removeSeratoGeob(tag, "Serato BeatGrid");
            addSeratoGeob(tag, "Serato BeatGrid", grid);
        }

        if (!file.save()) {
            spdlog::error("[SeratoTagWriter] Failed to save ID3 tags: {}", filePath);
            return false;
        }
        spdlog::info("[SeratoTagWriter] Wrote GEOB Markers2 ({} cues, {} loops{}) into {}",
                     cues.size(), loops.size(), bpm > 0.0f ? " + BeatGrid" : "", filePath);
        return true;
    }

    std::string sidecarPath = filePath + ".serato_markers2";
    std::ofstream f(sidecarPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        spdlog::error("[SeratoTagWriter] Cannot write sidecar: {}", sidecarPath);
        return false;
    }
    f.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    f.close();
    spdlog::warn("[SeratoTagWriter] {} is not MP3 - markers saved as sidecar only ({})",
                 filePath, sidecarPath);
    return true;
}

}
