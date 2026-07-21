#include "RekordboxProLink.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#endif

namespace BeatMate::Services::Rekordbox {

using BeatMate::Services::DJSoftware::PlayedTrack;

namespace {

static const uint8_t kDjLinkMagic[10] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c
};

static constexpr uint8_t kSubtypeCdjStatus = 0x03;

static constexpr size_t  kMinStatusLen     = 0xa4;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static uint16_t rdBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t rdBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
           (static_cast<uint32_t>(p[3]));
}

} // namespace

RekordboxProLink::RekordboxProLink() = default;

RekordboxProLink::~RekordboxProLink() { stop(); }

#ifdef _WIN32

bool RekordboxProLink::start()
{
    if (running_.load()) return true;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        spdlog::warn("[RBLink] WSAStartup failed");
        return false;
    }

    running_.store(true);
    worker_ = std::thread([this] { threadLoop(); });
    spdlog::info("[RBLink] listener started (UDP 50002 passive sniff)");
    return true;
}

void RekordboxProLink::stop()
{
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    WSACleanup();
    spdlog::info("[RBLink] listener stopped");
}

void RekordboxProLink::threadLoop()
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        spdlog::warn("[RBLink] socket() failed: {}", WSAGetLastError());
        return;
    }

    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    BOOL bcast = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&bcast), sizeof(bcast));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(50002);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        spdlog::warn("[RBLink] bind UDP 50002 failed (err={}) - rekordbox likely "
                     "holds the port exclusively. Disabling listener.", err);
        closesocket(sock);
        running_.store(false);
        return;
    }

    DWORD rxTimeoutMs = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rxTimeoutMs), sizeof(rxTimeoutMs));

    spdlog::info("[RBLink] bound UDP 50002, waiting for rekordbox LINK packets...");

    uint8_t buf[2048];
    while (running_.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        handlePacket(buf, static_cast<size_t>(n));
    }

    closesocket(sock);
}

void RekordboxProLink::handlePacket(const uint8_t* data, size_t len)
{
    if (len < kMinStatusLen) return;
    if (std::memcmp(data, kDjLinkMagic, sizeof(kDjLinkMagic)) != 0) return;
    if (data[0x0a] != kSubtypeCdjStatus) return;

    uint8_t player = data[0x21];
    if (player == 0 || player > 6) return;

    uint32_t rbId = rdBE32(data + 0x2c);
    uint16_t bpmRaw = rdBE16(data + 0x92);
    double   bpm = (bpmRaw == 0xffff) ? 0.0 : bpmRaw / 100.0;
    uint8_t  flagF = data[0x89];
    bool     master = (flagF & 0x20) != 0;
    bool     playing = (flagF & 0x40) != 0;

    lastPacketMs_.store(nowMs());

    std::lock_guard<std::mutex> lk(mutex_);
    DeckState& s = decks_[player];
    bool changed = (s.rekordboxId != rbId) || (s.master != master);
    s.rekordboxId = rbId;
    s.bpm         = bpm;
    s.playing     = playing;
    s.master      = master;
    s.lastSeenMs  = nowMs();
    if (master) masterPlayer_ = player;

    if (changed && master) {
        spdlog::info("[RBLink] deck {} MASTER, trackId={} bpm={:.2f} playing={}",
                     (int) player, rbId, bpm, playing);
    }
}

std::optional<PlayedTrack> RekordboxProLink::readNowPlaying()
{
    std::lock_guard<std::mutex> lk(mutex_);
    int p = masterPlayer_;
    if (p == 0) {
        for (int i = 1; i <= 6; ++i) {
            if (decks_[i].playing && decks_[i].rekordboxId != 0) { p = i; break; }
        }
    }
    if (p == 0) return std::nullopt;

    const DeckState& s = decks_[p];
    if (s.rekordboxId == 0) return std::nullopt;
    if (nowMs() - s.lastSeenMs > 10000) return std::nullopt;

    PlayedTrack pt;
    pt.source       = "Rekordbox";
    pt.bpm          = s.bpm;
    pt.title        = "Rekordbox track #" + std::to_string(s.rekordboxId);
    pt.artist       = "Deck " + std::to_string(p);
    pt.filePath     = "";
    pt.playedAtUnix = 0;
    return pt;
}

bool RekordboxProLink::isReceiving() const
{
    int64_t t = lastPacketMs_.load();
    return t != 0 && (nowMs() - t) < 5000;
}

uint32_t RekordboxProLink::currentMasterTrackId() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    int p = masterPlayer_;
    if (p == 0) {
        for (int i = 1; i <= 6; ++i) {
            if (decks_[i].playing && decks_[i].rekordboxId != 0) { p = i; break; }
        }
    }
    if (p == 0) return 0;
    if (nowMs() - decks_[p].lastSeenMs > 10000) return 0;
    return decks_[p].rekordboxId;
}

#else

bool RekordboxProLink::start() { return false; }
void RekordboxProLink::stop() {}
void RekordboxProLink::threadLoop() {}
void RekordboxProLink::handlePacket(const uint8_t*, size_t) {}
std::optional<PlayedTrack> RekordboxProLink::readNowPlaying() { return std::nullopt; }
bool RekordboxProLink::isReceiving() const { return false; }
uint32_t RekordboxProLink::currentMasterTrackId() const { return 0; }

#endif

} // namespace BeatMate::Services::Rekordbox
