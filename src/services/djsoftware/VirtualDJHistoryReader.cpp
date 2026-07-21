#include "VirtualDJHistoryReader.h"

#include <juce_core/juce_core.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace BeatMate::Services::DJSoftware {

namespace {

static juce::File vdjHistoryDir()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("VirtualDJ")
        .getChildFile("History");
}

static juce::String vdjTag(const juce::String& line, const char* tag)
{
    const juce::String open  = juce::String("<")  + tag + ">";
    const juce::String close = juce::String("</") + tag + ">";
    const int a = line.indexOfIgnoreCase(open);
    if (a < 0) return {};
    const int start = a + open.length();
    const int b = line.indexOfIgnoreCase(start, close);
    if (b < 0) return {};
    return line.substring(start, b).trim();
}

static int64_t parseVdjWhen(const juce::String& raw)
{
    const juce::String v = raw.trim();
    if (v.isEmpty()) return 0;
    if (v.containsOnly("0123456789")) {
        int64_t n = v.getLargeIntValue();
        if (n > 100000000000LL) n /= 1000;
        return n;
    }
    const juce::Time t = juce::Time::fromISO8601(v.replaceCharacter(' ', 'T'));
    return t.toMilliseconds() > 0 ? t.toMilliseconds() / 1000 : 0;
}

static juce::String stripVdjStatus(juce::String title)
{
    for (;;) {
        const juce::String t = title.trimEnd();
        if (!t.endsWithChar(')')) return t;
        const int open = t.lastIndexOfChar('(');
        if (open <= 0) return t;
        const juce::String inside = t.substring(open + 1, t.length() - 1).trim().toUpperCase();
        if (inside == "PLAYING NOW" || inside == "NOW PLAYING" ||
            inside == "PLAYING"     || inside == "PLAYED"      || inside == "OLD") {
            title = t.substring(0, open);
            continue;
        }
        return t;
    }
}

static void parseM3U(const juce::File& f, std::vector<PlayedTrack>& out, int maxTracks)
{
    juce::StringArray lines;
    f.readLines(lines);

    const int64_t fileWhen = f.getLastModificationTime().toMilliseconds() / 1000;

    juce::String pendingTitle, pendingArtist, pendingKey;
    double  pendingDuration = 0.0, pendingBpm = 0.0;
    int64_t pendingWhen = 0;

    auto resetPending = [&]() {
        pendingTitle.clear(); pendingArtist.clear(); pendingKey.clear();
        pendingDuration = 0.0; pendingBpm = 0.0; pendingWhen = 0;
    };

    for (auto& raw : lines) {
        juce::String line = raw.trim();
        if (line.isEmpty()) continue;

        if (line.startsWithIgnoreCase("#EXTVDJ:")) {
            juce::String lpt = vdjTag(line, "lastplaytime");
            if (lpt.isEmpty()) lpt = vdjTag(line, "time");
            const int64_t when = parseVdjWhen(lpt);
            if (when > 0) pendingWhen = when;

            const juce::String b = vdjTag(line, "bpm");
            if (b.isNotEmpty()) pendingBpm = b.getDoubleValue();
            const juce::String k = vdjTag(line, "key");
            if (k.isNotEmpty()) pendingKey = k;
            const juce::String a = vdjTag(line, "artist");
            if (a.isNotEmpty()) pendingArtist = a;
            const juce::String ti = vdjTag(line, "title");
            if (ti.isNotEmpty()) pendingTitle = ti;
            continue;
        }

        if (line.startsWithIgnoreCase("#EXTINF:")) {
            auto rest  = line.substring(8);
            auto comma = rest.indexOfChar(',');
            if (comma > 0) {
                if (pendingDuration <= 0.0)
                    pendingDuration = rest.substring(0, comma).getDoubleValue();
                auto meta = rest.substring(comma + 1);
                auto dash = meta.indexOf(" - ");
                if (dash > 0) {
                    if (pendingArtist.isEmpty()) pendingArtist = meta.substring(0, dash).trim();
                    if (pendingTitle.isEmpty())  pendingTitle  = meta.substring(dash + 3).trim();
                } else if (pendingTitle.isEmpty()) {
                    pendingTitle = meta.trim();
                }
            }
            continue;
        }
        if (line.startsWithChar('#')) continue;

        PlayedTrack pt;
        pt.filePath     = line.toStdString();
        pt.title        = stripVdjStatus(pendingTitle).toStdString();
        pt.artist       = pendingArtist.toStdString();
        pt.camelotKey   = pendingKey.toStdString();
        pt.bpm          = pendingBpm;
        pt.durationSec  = pendingDuration;
        pt.playedAtUnix = pendingWhen > 0 ? pendingWhen : fileWhen;
        pt.source       = "VirtualDJ";
        if (pt.title.empty())
            pt.title = juce::File(line).getFileNameWithoutExtension().toStdString();
        out.push_back(std::move(pt));
        resetPending();
        if ((int) out.size() >= maxTracks) return;
    }
}

static void parseHistoryXml(const juce::File& f, std::vector<PlayedTrack>& out, int maxTracks)
{
    auto xml = juce::XmlDocument::parse(f);
    if (!xml) return;

    const int64_t fallbackWhen = f.getLastModificationTime().toMilliseconds() / 1000;
    for (auto* song : xml->getChildWithTagNameIterator("Song")) {
        if ((int) out.size() >= maxTracks) return;
        PlayedTrack pt;
        pt.filePath = song->getStringAttribute("FilePath").toStdString();
        pt.title    = song->getStringAttribute("Title").toStdString();
        pt.artist   = song->getStringAttribute("Artist").toStdString();
        pt.album    = song->getStringAttribute("Album").toStdString();
        pt.genre    = song->getStringAttribute("Genre").toStdString();
        pt.bpm      = song->getDoubleAttribute("Bpm", 0.0);
        pt.durationSec = song->getDoubleAttribute("SongLength", 0.0);
        juce::Time t = juce::Time::fromISO8601(song->getStringAttribute("Time"));
        pt.playedAtUnix = t.toMilliseconds() > 0 ? t.toMilliseconds() / 1000 : fallbackWhen;
        pt.source = "VirtualDJ";
        out.push_back(std::move(pt));
    }
}

} // namespace

std::vector<PlayedTrack> VirtualDJHistoryReader::readRecentHistory(int maxTracks)
{
    std::vector<PlayedTrack> result;
    try {
        auto dir = vdjHistoryDir();
        if (!dir.isDirectory()) {
            spdlog::debug("[VirtualDJHistoryReader] dir not found: {}",
                          dir.getFullPathName().toStdString());
            return {};
        }

        juce::File xml = dir.getChildFile("History.xml");
        if (xml.existsAsFile()) {
            parseHistoryXml(xml, result, maxTracks);
            if (!result.empty()) return result;
        }

        juce::Array<juce::File> m3us;
        dir.findChildFiles(m3us, juce::File::findFiles, false, "*.m3u");
        dir.findChildFiles(m3us, juce::File::findFiles, false, "*.m3u8");
        std::sort(m3us.begin(), m3us.end(),
            [](const juce::File& a, const juce::File& b) {
                return a.getLastModificationTime() > b.getLastModificationTime();
            });

        for (auto& f : m3us) {
            parseM3U(f, result, maxTracks);
            if ((int) result.size() >= maxTracks) break;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[VirtualDJHistoryReader] failed: {}", e.what());
        return {};
    } catch (...) {
        spdlog::warn("[VirtualDJHistoryReader] failed (unknown)");
        return {};
    }
    return result;
}

} // namespace BeatMate::Services::DJSoftware
