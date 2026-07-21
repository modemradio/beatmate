#include "DemucsStemService.h"

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace BeatMate::Core::Stems {

std::string DemucsStemService::findDemucsLauncher() {
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                      .getParentDirectory();
    for (const auto& c : {
        exeDir.getChildFile("demucs.exe"),
        exeDir.getChildFile("demucs"),
        exeDir.getChildFile("tools").getChildFile("demucs.exe"),
        exeDir.getChildFile("tools").getChildFile("demucs"),
        exeDir.getChildFile("tools").getChildFile("external").getChildFile("demucs.exe"),
        exeDir.getChildFile("tools").getChildFile("external").getChildFile("demucs") })
    {
        if (c.existsAsFile()) return c.getFullPathName().toStdString();
    }

#ifdef _WIN32
    juce::ChildProcess cp;
    if (cp.start("where demucs")) {
        auto out = cp.readAllProcessOutput().trim();
        if (out.isNotEmpty())
            return out.upToFirstOccurrenceOf("\n", false, false).trim().toStdString();
    }
    juce::ChildProcess cpPy;
    if (cpPy.start("python -m demucs --help")) {
        cpPy.waitForProcessToFinish(5000);
        if (cpPy.getExitCode() == 0) return "python -m demucs";
    }
#else
    juce::ChildProcess cp;
    if (cp.start("which demucs")) {
        auto out = cp.readAllProcessOutput().trim();
        if (out.isNotEmpty())
            return out.upToFirstOccurrenceOf("\n", false, false).trim().toStdString();
    }
    juce::ChildProcess cpPy;
    if (cpPy.start("python3 -m demucs --help")) {
        cpPy.waitForProcessToFinish(5000);
        if (cpPy.getExitCode() == 0) return "python3 -m demucs";
    }
#endif
    return {};
}

DemucsStemService::Result
DemucsStemService::separate(const std::string& inputWav,
                            const std::string& outputDir,
                            ProgressCallback progress)
{
    Result r;
    auto launcher = findDemucsLauncher();
    if (launcher.empty()) {
        r.message = "Demucs non trouve. Installez-le avec 'pip install demucs' "
                    "puis redemarrez BeatMate.";
        if (progress) progress(0.0f, "error: demucs not found");
        return r;
    }
    if (!fs::exists(inputWav)) {
        r.message = "Fichier source introuvable : " + inputWav;
        return r;
    }
    fs::create_directories(outputDir);

    juce::StringArray args;
    if (launcher == "python -m demucs") {
        args.add("python"); args.add("-m"); args.add("demucs");
    } else {
        args.add(juce::String(launcher));
    }
    args.add("--out");          args.add(juce::String(outputDir));
    // Do NOT pass --filename: we rely on Demucs' default output layout
    args.add("-n"); args.add("htdemucs");
    args.add(juce::String(inputWav));

    if (progress) progress(0.05f, "starting demucs (this can take 30 s to 3 min)");

    juce::ChildProcess proc;
    if (!proc.start(args)) {
        r.message = "Impossible de lancer Demucs.";
        if (progress) progress(0.0f, "error: failed to start demucs");
        return r;
    }

    juce::String accum;
    while (proc.isRunning()) {
        juce::Thread::sleep(200);
        accum += proc.readAllProcessOutput();
        // Keep only the last line to avoid unbounded growth
        accum = accum.fromLastOccurrenceOf("\n", true, false);
        // htdemucs prints " 42%|" lines
        const int pct = accum.fromLastOccurrenceOf("\r", false, false)
                             .upToFirstOccurrenceOf("%", false, false)
                             .trim().getIntValue();
        if (pct > 0 && progress) progress(0.05f + 0.9f * (pct / 100.0f),
                                          "demucs: " + std::to_string(pct) + "%");
    }
    const int rc = proc.getExitCode();
    if (rc != 0) {
        r.message = "Demucs a echoue (code " + std::to_string(rc) + ").";
        if (progress) progress(0.0f, "error: demucs rc=" + std::to_string(rc));
        return r;
    }

    // Demucs 4.x writes to <out>/htdemucs/<base>/
    const fs::path inPath(inputWav);
    const std::string base = inPath.stem().string();
    const fs::path stemDir = fs::path(outputDir) / "htdemucs" / base;
    const char* names[4] = { "drums.wav", "bass.wav", "other.wav", "vocals.wav" };
    for (int i = 0; i < 4; ++i) {
        const auto p = stemDir / names[i];
        if (!fs::exists(p)) {
            r.message = "Sortie Demucs manquante: " + p.string();
            return r;
        }
        r.stemPaths[(size_t)i] = p.string();
    }
    r.ok = true;
    r.message = "Separation OK (drums / bass / other / vocals)";
    if (progress) progress(1.0f, "done");
    return r;
}

} // namespace BeatMate::Core::Stems
