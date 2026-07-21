#include "NowPlayingService.h"

#include <spdlog/spdlog.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace BeatMate::Services::Live {

namespace {

std::set<std::wstring> runningProcessSet()
{
    std::set<std::wstring> out;
#if JUCE_WINDOWS
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring n = pe.szExeFile;
            for (auto& c : n) c = (wchar_t) towlower(c);
            out.insert(std::move(n));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
#endif
    return out;
}

bool anyProcess(const std::set<std::wstring>& procs, std::initializer_list<const wchar_t*> names)
{
    for (const auto* n : names)
        if (procs.count(n) > 0) return true;
    return false;
}

juce::String extractTag(const juce::String& line, const juce::String& tag)
{
    const auto startTag = "<" + tag + ">";
    const auto endTag = "</" + tag + ">";
    const auto start = line.indexOf(startTag);
    const auto end = line.indexOf(endTag);
    if (start >= 0 && end > start)
        return line.substring(start + startTag.length(), end);
    return {};
}

void splitArtistTitleFromPath(const std::string& filePath, std::string& artist, std::string& title)
{
    if (filePath.empty()) return;
    const auto stem = juce::File(juce::String(filePath)).getFileNameWithoutExtension();
    const auto dash = stem.indexOf(" - ");
    if (dash > 0) {
        if (artist.empty()) artist = stem.substring(0, dash).trim().toStdString();
        if (title.empty()) title = stem.substring(dash + 3).trim().toStdString();
    } else if (title.empty()) {
        title = stem.trim().toStdString();
    }
}

constexpr juce::int64 kFileFreshWindowMs = 30 * 60 * 1000;
constexpr juce::int64 kTrackListFreshMs = 10 * 60 * 1000;

}

NowPlayingService::NowPlayingService()
    : juce::Thread("BeatMate NowPlaying")
{
}

NowPlayingService::~NowPlayingService()
{
    stop();
}

void NowPlayingService::start(int intervalMs)
{
    intervalMs_ = juce::jlimit(500, 5000, intervalMs);
    if (!isThreadRunning())
        startThread(juce::Thread::Priority::background);
}

void NowPlayingService::stop()
{
    stopThread(5000);
}

void NowPlayingService::pollNow()
{
    notify();
}

void NowPlayingService::setPreferredSource(const std::string& sourceName)
{
    std::lock_guard<std::mutex> lk(stateMutex_);
    preferredSource_ = sourceName;
}

std::optional<NowPlayingTrack> NowPlayingService::getCurrent() const
{
    std::lock_guard<std::mutex> lk(stateMutex_);
    return current_;
}

std::vector<std::string> NowPlayingService::runningSoftware() const
{
    std::lock_guard<std::mutex> lk(stateMutex_);
    return running_;
}

bool NowPlayingService::isAnySoftwareRunning() const
{
    std::lock_guard<std::mutex> lk(stateMutex_);
    return !running_.empty();
}

void NowPlayingService::run()
{
    while (!threadShouldExit()) {
        try {
            pollOnce();
        } catch (const std::exception& e) {
            spdlog::warn("[NowPlaying] pollOnce: {}", e.what());
        } catch (...) {
            spdlog::warn("[NowPlaying] pollOnce: unknown error");
        }
        wait(intervalMs_);
    }
}

void NowPlayingService::pollOnce()
{
    const juce::int64 nowScanMs = juce::Time::currentTimeMillis();
    if (cachedProcs_.empty() || nowScanMs - lastProcScanMs_ > 4000) {
        cachedProcs_ = runningProcessSet();
        lastProcScanMs_ = nowScanMs;
    }
    const auto& procs = cachedProcs_;
    const bool vdjUp = anyProcess(procs, { L"virtualdj.exe", L"virtualdj_pro.exe", L"virtualdj pro.exe", L"virtualdj8.exe" });
    const bool seratoUp = anyProcess(procs, { L"serato dj pro.exe", L"serato dj lite.exe", L"seratodj.exe", L"serato_dj.exe", L"serato dj.exe" });
    const bool rekordboxUp = anyProcess(procs, { L"rekordbox.exe" });
    const bool engineUp = anyProcess(procs, { L"engine dj.exe", L"enginedj.exe" });
    const bool traktorUp = anyProcess(procs, { L"traktor.exe" });

    std::vector<std::string> running;
    if (vdjUp) running.push_back("VirtualDJ");
    if (seratoUp) running.push_back("Serato");
    if (rekordboxUp) running.push_back("Rekordbox");
    if (engineUp) running.push_back("Engine DJ");
    if (traktorUp) running.push_back("Traktor");

    std::vector<NowPlayingTrack> candidates;
    auto push = [&candidates](std::optional<NowPlayingTrack> t) {
        if (t.has_value() && !t->title.empty())
            candidates.push_back(std::move(*t));
    };
    if (vdjUp && !threadShouldExit()) push(probeVirtualDJ());
    if (seratoUp && !threadShouldExit()) push(probeSerato());
    if (rekordboxUp && !threadShouldExit()) push(probeRekordbox());
    if (engineUp && !threadShouldExit()) push(probeEngineDJ());
    if (traktorUp && !threadShouldExit()) push(probeTraktor());

    std::string preferred;
    std::string currentSource;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        preferred = preferredSource_;
        if (current_.has_value()) currentSource = current_->source;
        running_ = std::move(running);
    }

    if (candidates.empty()) return;

    NowPlayingTrack best = candidates.front();
    for (size_t i = 1; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        const juce::int64 d = c.updatedAtMs - best.updatedAtMs;
        if (d > 5000) {
            best = c;
        } else if (d >= -5000) {
            const bool cPref = (c.source == preferred);
            const bool bPref = (best.source == preferred);
            if (cPref && !bPref) {
                best = c;
            } else if (cPref == bPref) {
                if (c.source == currentSource && best.source != currentSource)
                    best = c;
            }
        }
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        changed = !current_.has_value()
               || current_->title != best.title
               || current_->artist != best.artist
               || current_->source != best.source;
        current_ = best;
    }

    if (changed) {
        spdlog::info("[NowPlaying] {} : '{}' - '{}' (bpm {:.1f})",
                     best.source, best.artist, best.title, best.bpm);
        persistLast(best);
        if (onTrackChanged) onTrackChanged(best);
    }
}

static juce::File resolveVdjHomeFolder()
{
   #if JUCE_WINDOWS
    const auto reg = juce::WindowsRegistry::getValue (
        "HKEY_CURRENT_USER\\Software\\VirtualDJ\\HomeFolder").trim();
    if (reg.isNotEmpty()) {
        const juce::File f (reg);
        if (f.isDirectory()) return f;
    }
    const auto local = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                           .getChildFile ("VirtualDJ");
    if (local.isDirectory()) return local;
   #endif
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("VirtualDJ");
}

std::optional<NowPlayingTrack> NowPlayingService::probeVirtualDJ()
{
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    if (!vdjRemote_.isConnected() && nowMs - lastVdjConnectMs_ > 15000) {
        lastVdjConnectMs_ = nowMs;
        vdjRemote_.connectAuto("127.0.0.1");
    }
    if (vdjRemote_.isConnected()) {
        try {
            auto decks = vdjRemote_.getDecks();
            for (const auto& dk : decks) {
                if (dk.isPlaying && !dk.title.empty()) {
                    NowPlayingTrack t;
                    t.source = "VirtualDJ";
                    t.title = dk.title;
                    t.artist = dk.artist;
                    t.filePath = dk.filePath;
                    if (dk.bpm > 0.0) t.bpm = dk.bpm;
                    t.updatedAtMs = nowMs;
                    return t;
                }
            }
        } catch (...) {}
    }

    const auto vdjDir = resolveVdjHomeFolder();

    auto trackList = vdjDir.getChildFile("History").getChildFile("tracklist.txt");
    if (!trackList.existsAsFile())
        trackList = vdjDir.getChildFile("tracklist.txt");
    if (trackList.existsAsFile()) {
        const juce::int64 mtimeMs = trackList.getLastModificationTime().toMilliseconds();
        if (nowMs - mtimeMs < kTrackListFreshMs) {
            juce::StringArray lines;
            trackList.readLines(lines);
            for (int i = lines.size() - 1; i >= 0; --i) {
                const auto line = lines[i].trim();
                if (line.isEmpty() || line.startsWith("-") || line.startsWithIgnoreCase("VirtualDJ"))
                    continue;
                const auto sep = line.indexOf(" : ");
                const juce::String meta = sep >= 0 ? line.substring(sep + 3).trim() : line;
                const auto dash = meta.indexOf(" - ");
                NowPlayingTrack t;
                t.source = "VirtualDJ";
                t.updatedAtMs = mtimeMs;
                if (dash > 0) {
                    t.artist = meta.substring(0, dash).trim().toStdString();
                    t.title = meta.substring(dash + 3).trim().toStdString();
                } else {
                    t.title = meta.toStdString();
                }
                if (!t.title.empty()) return t;
            }
        }
    }

    const auto histDir = vdjDir.getChildFile("History");
    if (!histDir.isDirectory()) return std::nullopt;
    auto m3us = histDir.findChildFiles(juce::File::findFiles, false, "*.m3u");
    if (m3us.isEmpty()) return std::nullopt;
    juce::File newest;
    juce::Time newestT;
    for (auto& f : m3us) {
        if (f.getLastModificationTime() > newestT) {
            newestT = f.getLastModificationTime();
            newest = f;
        }
    }
    juce::StringArray lines;
    newest.readLines(lines);
    const juce::int64 nowSec = nowMs / 1000;
    for (int i = lines.size() - 1; i >= 0; --i) {
        const auto& line = lines[i];
        if (!line.startsWith("#EXTVDJ:")) continue;
        const auto artist = extractTag(line, "artist");
        const auto title = extractTag(line, "title");
        const juce::int64 lastPlay = extractTag(line, "lastplaytime").getLargeIntValue();
        const double songLen = extractTag(line, "songlength").getDoubleValue();
        const double bpm = extractTag(line, "bpm").getDoubleValue();
        const juce::int64 margin = (juce::int64) (songLen > 0.0 ? songLen + 60.0 : 600.0);
        if (lastPlay > 0 && nowSec - lastPlay > margin) return std::nullopt;
        NowPlayingTrack t;
        t.source = "VirtualDJ";
        t.title = title.toStdString();
        t.artist = artist.toStdString();
        if (bpm > 0.0) t.bpm = bpm;
        t.updatedAtMs = lastPlay > 0 ? lastPlay * 1000 : newestT.toMilliseconds();
        if (i + 1 < lines.size() && !lines[i + 1].startsWith("#"))
            t.filePath = lines[i + 1].toStdString();
        splitArtistTitleFromPath(t.filePath, t.artist, t.title);
        if (t.title.empty()) return std::nullopt;
        return t;
    }
    return std::nullopt;
}

std::optional<NowPlayingTrack> NowPlayingService::probeSerato()
{
    if (!seratoOpened_) seratoOpened_ = seratoDb_.open();
    if (!seratoOpened_) return std::nullopt;

    auto sessionsDir = juce::File(juce::String(seratoDb_.seratoDir()))
                           .getChildFile("History").getChildFile("Sessions");
    if (!sessionsDir.isDirectory()) return std::nullopt;
    auto files = sessionsDir.findChildFiles(juce::File::findFiles, false, "*.session");
    if (files.isEmpty()) return std::nullopt;
    juce::File newest;
    juce::Time newestT;
    for (auto& f : files) {
        if (f.getLastModificationTime() > newestT) {
            newestT = f.getLastModificationTime();
            newest = f;
        }
    }
    const juce::int64 mtimeMs = newestT.toMilliseconds();
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    if (nowMs - mtimeMs > kFileFreshWindowMs) return std::nullopt;
    if (mtimeMs == seratoSessionMtimeMs_ && seratoCache_.has_value()) return seratoCache_;

    auto sessions = seratoDb_.getHistorySessions();
    const Serato::SeratoSession* best = nullptr;
    for (const auto& s : sessions)
        if (best == nullptr || s.startTime > best->startTime) best = &s;
    if (best == nullptr || best->entries.empty()) return std::nullopt;

    const auto& e = best->entries.back();
    NowPlayingTrack t;
    t.source = "Serato";
    t.title = e.title;
    t.artist = e.artist;
    t.filePath = e.trackPath;
    t.updatedAtMs = mtimeMs;
    splitArtistTitleFromPath(t.filePath, t.artist, t.title);
    if (t.title.empty()) return std::nullopt;
    seratoSessionMtimeMs_ = mtimeMs;
    seratoCache_ = t;
    return t;
}

std::optional<NowPlayingTrack> NowPlayingService::probeRekordbox()
{
    auto pt = rekordboxReader_.readNowPlaying();
    if (!pt.has_value() || pt->title.empty()) return std::nullopt;
    NowPlayingTrack t;
    t.source = "Rekordbox";
    t.title = pt->title;
    t.artist = pt->artist;
    t.filePath = pt->filePath;
    t.key = pt->camelotKey;
    if (pt->bpm > 0.0) t.bpm = pt->bpm;
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    t.updatedAtMs = pt->playedAtUnix > 0 ? pt->playedAtUnix * 1000 : nowMs;
    if (t.updatedAtMs > nowMs) t.updatedAtMs = nowMs;
    return t;
}

std::optional<NowPlayingTrack> NowPlayingService::probeEngineDJ()
{
    auto pt = engineReader_.readNowPlaying();
    if (!pt.has_value() || pt->title.empty()) return std::nullopt;
    NowPlayingTrack t;
    t.source = "Engine DJ";
    t.title = pt->title;
    t.artist = pt->artist;
    t.filePath = pt->filePath;
    t.key = pt->camelotKey;
    if (pt->bpm > 0.0) t.bpm = pt->bpm;
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    t.updatedAtMs = pt->playedAtUnix > 0 ? pt->playedAtUnix * 1000 : nowMs;
    if (t.updatedAtMs > nowMs) t.updatedAtMs = nowMs;
    return t;
}

std::optional<NowPlayingTrack> NowPlayingService::probeTraktor()
{
    if (!icecastStarted_) icecastStarted_ = traktorIcecast_.startServer(8000);
    if (auto tk = traktorIcecast_.getLatestTrack()) {
        if (!tk->title.empty()) {
            NowPlayingTrack t;
            t.source = "Traktor";
            t.title = tk->title;
            t.artist = tk->artist;
            t.updatedAtMs = tk->lastUpdateMs > 0 ? tk->lastUpdateMs : juce::Time::currentTimeMillis();
            return t;
        }
    }

    const auto niRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("Native Instruments");
    if (!niRoot.isDirectory()) return std::nullopt;
    juce::File newest;
    juce::Time newestT;
    for (auto& d : niRoot.findChildFiles(juce::File::findDirectories, false, "Traktor*")) {
        auto hist = d.getChildFile("History");
        if (!hist.isDirectory()) continue;
        for (auto& f : hist.findChildFiles(juce::File::findFiles, false, "history_*.nml")) {
            if (f.getLastModificationTime() > newestT) {
                newestT = f.getLastModificationTime();
                newest = f;
            }
        }
    }
    if (!newest.existsAsFile()) return std::nullopt;
    const juce::int64 mtimeMs = newestT.toMilliseconds();
    const juce::int64 nowMs = juce::Time::currentTimeMillis();
    if (nowMs - mtimeMs > kFileFreshWindowMs) return std::nullopt;
    if (mtimeMs == traktorNmlMtimeMs_ && traktorCache_.has_value()) return traktorCache_;

    auto xml = juce::XmlDocument::parse(newest);
    if (!xml) return std::nullopt;
    auto* collection = xml->getChildByName("COLLECTION");
    if (!collection) return std::nullopt;
    juce::XmlElement* last = nullptr;
    for (auto* entry : collection->getChildWithTagNameIterator("ENTRY"))
        last = entry;
    if (!last) return std::nullopt;

    NowPlayingTrack t;
    t.source = "Traktor";
    t.title = last->getStringAttribute("TITLE").toStdString();
    t.artist = last->getStringAttribute("ARTIST").toStdString();
    if (auto* tempo = last->getChildByName("TEMPO")) {
        const double bpm = tempo->getDoubleAttribute("BPM", 0.0);
        if (bpm > 0.0) t.bpm = bpm;
    }
    if (auto* info = last->getChildByName("INFO")) {
        if (t.bpm <= 0.0) {
            const double bpm = info->getDoubleAttribute("BPM", 0.0);
            if (bpm > 0.0) t.bpm = bpm;
        }
        t.key = info->getStringAttribute("KEY").toStdString();
    }
    if (auto* loc = last->getChildByName("LOCATION")) {
        const juce::String volume = loc->getStringAttribute("VOLUME");
        const juce::String dir = loc->getStringAttribute("DIR").replace("/:", "/");
        const juce::String file = loc->getStringAttribute("FILE");
        t.filePath = (volume + dir + file).toStdString();
    }
    splitArtistTitleFromPath(t.filePath, t.artist, t.title);
    if (t.title.empty()) return std::nullopt;
    t.updatedAtMs = mtimeMs;
    traktorNmlMtimeMs_ = mtimeMs;
    traktorCache_ = t;
    return t;
}

juce::File NowPlayingService::persistFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate").getChildFile("live_now_playing.json");
}

void NowPlayingService::persistLast(const NowPlayingTrack& t)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("source", juce::String(t.source));
    obj->setProperty("title", juce::String(t.title));
    obj->setProperty("artist", juce::String(t.artist));
    obj->setProperty("filePath", juce::String(t.filePath));
    obj->setProperty("key", juce::String(t.key));
    obj->setProperty("bpm", t.bpm);
    obj->setProperty("updatedAtMs", t.updatedAtMs);
    auto f = persistFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(obj)));
}

std::optional<NowPlayingTrack> NowPlayingService::loadLastPersisted()
{
    const auto f = persistFile();
    if (!f.existsAsFile()) return std::nullopt;
    const auto v = juce::JSON::parse(f.loadFileAsString());
    if (!v.isObject()) return std::nullopt;
    NowPlayingTrack t;
    t.source = v.getProperty("source", "").toString().toStdString();
    t.title = v.getProperty("title", "").toString().toStdString();
    t.artist = v.getProperty("artist", "").toString().toStdString();
    t.filePath = v.getProperty("filePath", "").toString().toStdString();
    t.key = v.getProperty("key", "").toString().toStdString();
    t.bpm = (double) v.getProperty("bpm", 0.0);
    t.updatedAtMs = (juce::int64) v.getProperty("updatedAtMs", 0);
    if (t.title.empty()) return std::nullopt;
    return t;
}

}
