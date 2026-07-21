#include "UpdateService.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>
#include <sstream>

namespace BeatMate::Services::Update {

// La version fait foi : a numero identique, le binaire distant est celui deja installe,
// quelle que soit la date du fichier sur le serveur. Trancher par les dates rendait la
// mise a jour perpetuelle, l'archive etant toujours deposee des heures apres la
// compilation du binaire qu'elle contient.
bool UpdateService::decideAvailable (const std::string& currentVersion,
                                     const std::string& latestVersion,
                                     int64_t latestEpoch,
                                     int64_t buildEpoch)
{
    const int64_t kMarginSeconds = 1800;

    auto segCount = [] (const std::string& s) {
        int n = 1; for (char c : s) if (c == '.') ++n; return n;
    };
    auto firstSeg = [] (const std::string& s) {
        try { return std::stoi (s); } catch (...) { return 100000; }
    };
    const bool comparable = ! currentVersion.empty() && ! latestVersion.empty()
                         && segCount (currentVersion) == segCount (latestVersion)
                         && firstSeg (currentVersion) < 100 && firstSeg (latestVersion) < 100;

    if (comparable)
        return compareVersions (currentVersion, latestVersion) < 0;

    if (latestEpoch > 0 && buildEpoch > 0)
        return latestEpoch > buildEpoch + kMarginSeconds;

    return false;
}

int UpdateService::compareVersions (const std::string& a, const std::string& b)
{
    auto split = [] (const std::string& s) {
        std::vector<int> out;
        std::stringstream ss (s);
        std::string seg;
        while (std::getline (ss, seg, '.')) {
            try { out.push_back (std::stoi (seg)); } catch (...) { out.push_back (0); }
        }
        return out;
    };
    auto va = split (a), vb = split (b);
    const size_t n = std::max (va.size(), vb.size());
    for (size_t i = 0; i < n; ++i) {
        const int x = i < va.size() ? va[i] : 0;
        const int y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

UpdateInfo UpdateService::checkRemote() const
{
    UpdateInfo info;
    const juce::String root = juce::String (baseUrl_).trimCharactersAtEnd ("/");

    juce::String text;
    for (const char* leaf : { "/latest.php", "/latest.json" })
    {
        juce::URL url (root + leaf);
        const juce::String t = url.readEntireTextStream (false);
        if (t.trim().startsWithChar ('{')) { text = t; break; }
    }
    if (text.isEmpty()) {
        info.error = "Manifest introuvable ou hote injoignable (" + root.toStdString() + ")";
        return info;
    }

    juce::var parsed;
    const auto pr = juce::JSON::parse (text, parsed);
    if (pr.failed() || ! parsed.isObject()) {
        info.error = "Manifest illisible (JSON invalide)";
        return info;
    }

    if (parsed.getProperty ("error", juce::var()).toString().isNotEmpty()) {
        info.error = "Aucune mise a jour publiee sur le serveur.";
        return info;
    }

    info.latestVersion = parsed.getProperty ("version", juce::var()).toString().toStdString();
    std::string u      = parsed.getProperty ("url", juce::var()).toString().toStdString();
    info.notes         = parsed.getProperty ("notes", juce::var()).toString().toStdString();
    info.mandatory     = (bool) parsed.getProperty ("mandatory", juce::var (false));
    info.latestEpoch   = (int64_t) parsed.getProperty ("epoch", juce::var ((juce::int64) 0));
    info.date          = parsed.getProperty ("date", juce::var()).toString().toStdString();

    if (info.latestVersion.empty() && info.latestEpoch <= 0) {
        info.error = "Manifest sans 'version' ni 'epoch'"; return info;
    }

    if (u.rfind ("http://", 0) != 0 && u.rfind ("https://", 0) != 0) {
        if (u.empty()) u = "BeatMate-Suite-Setup.exe";
        u = root.toStdString() + "/" + u;
    }
    info.downloadUrl = u;

    info.available = decideAvailable (currentVersion_, info.latestVersion,
                                      info.latestEpoch, buildEpoch_);

    const int64_t updatesUntil = readUpdatesUntilEpoch();
    if (info.available && updatesUntil > 0 && info.latestEpoch > 0
        && info.latestEpoch > updatesUntil) {
        info.available = false;
        info.mandatory = false;
        info.error = "Votre periode de mises a jour (6 mois, licence a vie) est terminee. "
                     "Votre logiciel reste pleinement utilisable. "
                     "Prolongez vos mises a jour sur beatmate.fr.";
    }
    return info;
}

int64_t UpdateService::readUpdatesUntilEpoch()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("BeatMate").getChildFile ("updates_until.txt");
    if (! f.existsAsFile())
        return 0;
    return (int64_t) f.loadFileAsString().trim().getLargeIntValue();
}

void UpdateService::checkRemoteAsync (std::function<void (UpdateInfo)> cb) const
{
    const std::string ver = currentVersion_;
    const std::string base = baseUrl_;
    const int64_t epoch = buildEpoch_;
    juce::Thread::launch ([ver, base, epoch, cb]
    {
        UpdateService tmp (ver);
        tmp.setBaseUrl (base);
        tmp.setBuildEpoch (epoch);
        auto info = tmp.checkRemote();
        juce::MessageManager::callAsync ([cb, info] { if (cb) cb (info); });
    });
}

void UpdateService::downloadAndInstallAsync (std::string msiUrl,
                                             std::function<void (bool, std::string)> done,
                                             std::function<void (double)> progress) const
{
    const std::string self_base = baseUrl_;
    juce::Thread::launch ([msiUrl, done, progress, self_base]
    {
        auto report = [done] (bool ok, std::string msg) {
            juce::MessageManager::callAsync ([done, ok, msg] { if (done) done (ok, msg); });
        };

        const juce::String urlStr (msiUrl);
        juce::URL url (urlStr);
        std::unique_ptr<juce::InputStream> in (
            url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                       .withConnectionTimeoutMs (15000)));
        if (in == nullptr) { report (false, "Telechargement impossible (hote injoignable)."); return; }

        const bool isExe = urlStr.upToFirstOccurrenceOf ("?", false, false)
                               .endsWithIgnoreCase (".exe");
        auto dest = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("BeatMate-Update")
                        .getChildFile (isExe ? "BeatMate-Suite-Setup.exe"
                                             : "BeatMate-Suite-Setup.msi");
        dest.getParentDirectory().createDirectory();
        dest.deleteFile();

        std::unique_ptr<juce::FileOutputStream> out (dest.createOutputStream());
        if (out == nullptr || ! out->openedOk()) { report (false, "Ecriture du fichier temporaire impossible."); return; }

        const juce::int64 total = in->getTotalLength();
        juce::int64 readBytes = 0;
        juce::HeapBlock<char> buf (1 << 16);
        while (! in->isExhausted()) {
            const int n = in->read (buf, 1 << 16);
            if (n <= 0) break;
            out->write (buf, (size_t) n);
            readBytes += n;
            if (progress && total > 0) {
                const double pct = juce::jlimit (0.0, 1.0, (double) readBytes / (double) total);
                juce::MessageManager::callAsync ([progress, pct] { if (progress) progress (pct); });
            }
        }
        out->flush();
        out.reset();
        in.reset();

        if (readBytes <= 0) { report (false, "Telechargement vide."); return; }

        UpdateService tmp ("0");
        std::string err;
        if (tmp.installFromFile (dest, err))
            report (true, "Installeur lance. L'application va se fermer pour terminer la mise a jour.");
        else
            report (false, "Echec du lancement de l'installeur: " + err);
    });
}

bool UpdateService::installFromFile (const juce::File& msi, std::string& err) const
{
    if (! msi.existsAsFile()) { err = "Fichier MSI introuvable: " + msi.getFullPathName().toStdString(); return false; }
    return launchInstaller (msi, err);
}

bool UpdateService::launchInstaller (const juce::File& msi, std::string& err) const
{
#if JUCE_WINDOWS
    if (msi.hasFileExtension ("exe"))
    {
        if (msi.startAsProcess()) return true;
        err = "L'installeur exe n'a pas pu demarrer.";
        return false;
    }
    juce::ChildProcess proc;
    juce::StringArray cmd { "msiexec.exe", "/i", msi.getFullPathName() };
    if (proc.start (cmd, 0)) return true;
    if (msi.startAsProcess()) return true;
    err = "msiexec n'a pas pu demarrer.";
    return false;
#else
    if (msi.startAsProcess()) return true;
    err = "Lancement non supporte sur cette plateforme.";
    return false;
#endif
}

}
