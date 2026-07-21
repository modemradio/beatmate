#include "AudioIntegrityChecker.h"
#include "../export/VideoExportService.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace BeatMate::Services::Analysis {

namespace {

juce::String truncatedDetails(const juce::String& raw)
{
    auto lines = juce::StringArray::fromLines(raw.trim());
    lines.removeEmptyStrings();
    juce::String out;
    const int n = juce::jmin(6, lines.size());
    for (int i = 0; i < n; ++i)
        out << lines[i].substring(0, 220) << (i + 1 < n ? "\n" : "");
    if (lines.size() > n)
        out << "\n(+" << juce::String(lines.size() - n) << " lignes)";
    return out;
}

} // namespace

AudioIntegrityChecker::AudioIntegrityChecker() { load(); }

juce::File AudioIntegrityChecker::reportFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate").getChildFile("integrity_report.json");
}

bool AudioIntegrityChecker::isAvailable()
{
    return Export::VideoExportService::findFfmpeg().size() > 0;
}

juce::String AudioIntegrityChecker::statusLabel(Status s)
{
    switch (s)
    {
        case Status::Ok:         return juce::String::fromUTF8("Intact");
        case Status::Warning:    return juce::String::fromUTF8("Suspect");
        case Status::Corrupt:    return juce::String::fromUTF8("Corrompu");
        case Status::Unreadable: return juce::String::fromUTF8("Illisible");
        case Status::Repaired:   return juce::String::fromUTF8("R\xc3\xa9par\xc3\xa9");
        default:                 return juce::String::fromUTF8("Non v\xc3\xa9rifi\xc3\xa9");
    }
}

juce::String AudioIntegrityChecker::statusToKey(Status s)
{
    switch (s)
    {
        case Status::Ok:         return "ok";
        case Status::Warning:    return "warning";
        case Status::Corrupt:    return "corrupt";
        case Status::Unreadable: return "unreadable";
        case Status::Repaired:   return "repaired";
        default:                 return "unknown";
    }
}

AudioIntegrityChecker::Status AudioIntegrityChecker::keyToStatus(const juce::String& k)
{
    if (k == "ok") return Status::Ok;
    if (k == "warning") return Status::Warning;
    if (k == "corrupt") return Status::Corrupt;
    if (k == "unreadable") return Status::Unreadable;
    if (k == "repaired") return Status::Repaired;
    return Status::Unknown;
}

void AudioIntegrityChecker::load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    auto f = reportFile();
    if (! f.existsAsFile()) return;
    try
    {
        auto j = nlohmann::json::parse(f.loadFileAsString().toStdString());
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            Entry e;
            e.status = juce::String::fromUTF8(it.value().value("status", "unknown").c_str());
            e.details = juce::String::fromUTF8(it.value().value("details", "").c_str());
            e.checkedAt = it.value().value("checkedAt", (int64_t) 0);
            e.fileSize = it.value().value("fileSize", (int64_t) 0);
            e.fileMtime = it.value().value("fileMtime", (int64_t) 0);
            entries_[juce::String::fromUTF8(it.key().c_str())] = std::move(e);
        }
    }
    catch (const std::exception& ex)
    {
        spdlog::warn("[Integrity] rapport illisible: {}", ex.what());
    }
}

void AudioIntegrityChecker::save() const
{
    nlohmann::json j = nlohmann::json::object();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : entries_)
        {
            nlohmann::json e;
            e["status"] = kv.second.status.toStdString();
            e["details"] = kv.second.details.toStdString();
            e["checkedAt"] = kv.second.checkedAt;
            e["fileSize"] = kv.second.fileSize;
            e["fileMtime"] = kv.second.fileMtime;
            j[kv.first.toStdString()] = std::move(e);
        }
    }
    reportFile().replaceWithText(juce::String(j.dump(1)));
}

void AudioIntegrityChecker::store(const juce::String& filePath, Status s, const juce::String& details)
{
    juce::File f(filePath);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry e;
        e.status = statusToKey(s);
        e.details = details;
        e.checkedAt = (int64_t) (juce::Time::getCurrentTime().toMilliseconds() / 1000);
        e.fileSize = f.existsAsFile() ? (int64_t) f.getSize() : 0;
        e.fileMtime = f.existsAsFile() ? (int64_t) (f.getLastModificationTime().toMilliseconds() / 1000) : 0;
        entries_[filePath] = std::move(e);
    }
    save();
}

AudioIntegrityChecker::Report AudioIntegrityChecker::statusFor(const juce::String& filePath) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(filePath);
    if (it == entries_.end()) return {};
    juce::File f(filePath);
    if (! f.existsAsFile()) return {};
    if ((int64_t) f.getSize() != it->second.fileSize
        || (int64_t) (f.getLastModificationTime().toMilliseconds() / 1000) != it->second.fileMtime)
        return {};
    Report r;
    r.status = keyToStatus(it->second.status);
    r.details = it->second.details;
    r.checkedAt = it->second.checkedAt;
    return r;
}

AudioIntegrityChecker::Report AudioIntegrityChecker::check(const juce::String& filePath)
{
    Report r;
    juce::File f(filePath);
    if (! f.existsAsFile())
    {
        r.status = Status::Unreadable;
        r.details = juce::String::fromUTF8("Fichier introuvable");
        store(filePath, r.status, r.details);
        return r;
    }

    const auto ffmpeg = juce::String::fromUTF8(Export::VideoExportService::findFfmpeg().c_str());
    if (ffmpeg.isEmpty())
    {
        r.status = Status::Unknown;
        r.details = juce::String::fromUTF8("ffmpeg indisponible");
        return r;
    }

    juce::StringArray args { ffmpeg, "-v", "error", "-nostdin", "-i", filePath, "-f", "null", "-" };
    juce::ChildProcess proc;
    if (! proc.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        r.status = Status::Unreadable;
        r.details = juce::String::fromUTF8("Impossible de lancer ffmpeg");
        return r;
    }

    const juce::String output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish(10000);
    const juce::uint32 exitCode = proc.getExitCode();

    if (exitCode != 0)
    {
        r.status = Status::Corrupt;
        r.details = truncatedDetails(output.isNotEmpty() ? output
                        : juce::String::fromUTF8("D\xc3\xa9""codage impossible (code ")
                          + juce::String((int) exitCode) + ")");
    }
    else if (output.trim().isNotEmpty())
    {
        r.status = Status::Warning;
        r.details = truncatedDetails(output);
    }
    else
    {
        r.status = Status::Ok;
    }

    r.checkedAt = (int64_t) (juce::Time::getCurrentTime().toMilliseconds() / 1000);
    store(filePath, r.status, r.details);
    return r;
}

AudioIntegrityChecker::Report AudioIntegrityChecker::repair(const juce::String& filePath, juce::String* errorOut)
{
    auto fail = [&](const juce::String& why) -> Report {
        if (errorOut) *errorOut = why;
        Report r;
        r.status = Status::Corrupt;
        r.details = why;
        return r;
    };

    juce::File src(filePath);
    if (! src.existsAsFile())
        return fail(juce::String::fromUTF8("Fichier introuvable"));

    const auto ffmpeg = juce::String::fromUTF8(Export::VideoExportService::findFfmpeg().c_str());
    if (ffmpeg.isEmpty())
        return fail(juce::String::fromUTF8("ffmpeg indisponible"));

    juce::File tmp = src.getSiblingFile(src.getFileNameWithoutExtension()
                                        + ".bmfix" + src.getFileExtension());
    tmp.deleteFile();
    juce::StringArray args { ffmpeg, "-y", "-v", "error", "-nostdin",
                             "-err_detect", "ignore_err",
                             "-i", filePath, "-c", "copy", tmp.getFullPathName() };
    juce::ChildProcess proc;
    if (! proc.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return fail(juce::String::fromUTF8("Impossible de lancer ffmpeg"));
    proc.readAllProcessOutput();
    proc.waitForProcessToFinish(10000);

    if (! tmp.existsAsFile() || tmp.getSize() < 1024)
    {
        tmp.deleteFile();
        return fail(juce::String::fromUTF8("La r\xc3\xa9paration n'a produit aucun fichier valide"));
    }

    auto backupDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("BeatMate").getChildFile("integrity_backups");
    backupDir.createDirectory();
    const juce::String stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    juce::File backup = backupDir.getChildFile(stamp + "_" + src.getFileName());
    if (! src.copyFileTo(backup))
    {
        tmp.deleteFile();
        return fail(juce::String::fromUTF8("Impossible de sauvegarder l'original"));
    }
    if (! tmp.moveFileTo(src))
    {
        tmp.deleteFile();
        return fail(juce::String::fromUTF8("Impossible de remplacer le fichier"));
    }
    spdlog::info("[Integrity] Repaired {} (backup: {})", filePath.toStdString(),
                 backup.getFullPathName().toStdString());

    Report after = check(filePath);
    if (after.status == Status::Ok)
    {
        after.status = Status::Repaired;
        store(filePath, Status::Repaired, juce::String::fromUTF8("Original sauvegard\xc3\xa9 : ")
                                          + backup.getFullPathName());
    }
    return after;
}

} // namespace BeatMate::Services::Analysis
