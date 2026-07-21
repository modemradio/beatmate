#include "RekordboxAnlzParser.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <fstream>
#include <vector>

namespace BeatMate::Services::Rekordbox {

namespace {

static uint16_t rdBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static uint32_t rdBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

static bool isTag(const uint8_t* p, const char* tag) {
    return p[0] == uint8_t(tag[0]) && p[1] == uint8_t(tag[1]) &&
           p[2] == uint8_t(tag[2]) && p[3] == uint8_t(tag[3]);
}

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// Parse PQTZ section. Layout:
static void parsePQTZ(const uint8_t* p, size_t size,
                      std::vector<AnlzBeat>& out) {
    if (size < 24) return;
    uint32_t n = rdBE32(p + 20);
    if (n > 100000) return; // safety
    size_t off = 24;
    for (uint32_t i = 0; i < n; ++i) {
        if (off + 8 > size) break;
        AnlzBeat b;
        b.beatNumber = static_cast<uint8_t>(rdBE16(p + off));
        uint16_t t100 = rdBE16(p + off + 2);
        b.bpm        = (t100 > 0) ? (t100 / 100.0) : 0.0;
        uint32_t ms  = rdBE32(p + off + 4);
        b.timeSec    = ms / 1000.0;
        out.push_back(b);
        off += 8;
    }
}

// Parse PCO2 (extended cue / loop list). Sub-entries carry tag "PCPT".
static void parsePCO2(const uint8_t* p, size_t size,
                      std::vector<AnlzCue>& out) {
    if (size < 20) return;
    // Header: tag(4) + hdrLen(4) + totalLen(4) + kind(4) + count(4)
    uint32_t hdrLen = rdBE32(p + 4);
    if (hdrLen < 16 || hdrLen > size) return;
    size_t off = hdrLen;
    while (off + 8 <= size) {
        if (!isTag(p + off, "PCPT")) { ++off; continue; }
        uint32_t cpHdr = rdBE32(p + off + 4);
        uint32_t cpLen = rdBE32(p + off + 8);
        if (cpLen < 16 || off + cpLen > size) break;
        // Offsets per dysentery: hotCue u32 @ +12, status u32 @ +16,
        if (off + 36 <= size) {
            AnlzCue c;
            c.hotCueIndex = static_cast<int>(rdBE32(p + off + 12));
            uint32_t timeMs = rdBE32(p + off + 28);
            c.positionSec = timeMs / 1000.0;
            uint32_t loopEndMs = rdBE32(p + off + 32);
            c.loopEndSec = loopEndMs > 0 ? loopEndMs / 1000.0 : 0.0;
            if (off + 51 <= size) {
                uint8_t r = p[off + 48], g = p[off + 49], b = p[off + 50];
                c.rgbColor = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
            }
            out.push_back(c);
        }
        off += cpLen;
        (void) cpHdr;
    }
}

// Parse PWV4 (fixed-width 3-band color waveform, 1200 columns, 6 bytes each).
static void parsePWV4(const uint8_t* p, size_t size,
                      AnlzWaveform& out) {
    if (size < 32) return;
    uint32_t hdrLen = rdBE32(p + 4);
    uint32_t dataLen = rdBE32(p + 8);
    if (hdrLen == 0 || hdrLen > size) return;
    const uint8_t* body = p + hdrLen;
    size_t bodySize = (size >= hdrLen) ? (size - hdrLen) : 0;
    // 6 bytes per column per dysentery spec.
    const size_t bytesPerCol = 6;
    size_t cols = bodySize / bytesPerCol;
    if (cols == 0) return;
    out.columns.reserve(cols);
    for (size_t i = 0; i < cols; ++i) {
        const uint8_t* c = body + i * bytesPerCol;
        AnlzWavColumn col;
        col.heightBass = c[0] & 0x1F;
        col.heightMid  = c[2] & 0x1F;
        col.heightHigh = c[4] & 0x1F;
        out.columns.push_back(col);
    }
    (void) dataLen;
}

} // namespace

bool RekordboxAnlzParser::parseFile(const std::string& path,
                                    std::vector<AnlzBeat>*  outBeats,
                                    std::vector<AnlzCue>*   outCues,
                                    AnlzWaveform*           outWaveform)
{
    auto buf = readFile(path);
    if (buf.size() < 32) return false;
    const uint8_t* data = buf.data();
    size_t size = buf.size();

    // Header PMAI: tag(4) + hdrLen(4) + totalLen(4).
    if (!isTag(data, "PMAI")) {
        spdlog::debug("[ANLZ] no PMAI header at {}", path);
        return false;
    }
    uint32_t rootHdrLen = rdBE32(data + 4);
    if (rootHdrLen > size) return false;

    size_t off = rootHdrLen;
    while (off + 8 <= size) {
        const uint8_t* p = data + off;
        // Section header: tag(4) + hdrLen(4) + totalLen(4).
        uint32_t secHdr   = rdBE32(p + 4);
        uint32_t secTotal = rdBE32(p + 8);
        if (secTotal < secHdr || off + secTotal > size) break;
        if (isTag(p, "PQTZ") && outBeats) parsePQTZ(p, secTotal, *outBeats);
        else if (isTag(p, "PCO2") && outCues) parsePCO2(p, secTotal, *outCues);
        else if (isTag(p, "PWV4") && outWaveform) parsePWV4(p, secTotal, *outWaveform);
        off += secTotal;
        if (secTotal == 0) break;
    }
    return true;
}

std::vector<AnlzBeat> RekordboxAnlzParser::readBeats(const std::string& path)
{
    std::vector<AnlzBeat> b;
    parseFile(path, &b, nullptr, nullptr);
    return b;
}

} // namespace BeatMate::Services::Rekordbox
