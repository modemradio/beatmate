#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace BeatMate::Core::Stems {

class StemSepSotaService {
public:
    enum class Model { VocalsHQ = 0, FourStems = 1 };

    struct Result {
        bool ok = false;
        juce::String message;
        juce::File vocals;
        juce::File instrumental;
        juce::File drums;
        juce::File bass;
        juce::File other;
        bool fourStems = false;
    };

    using ProgressCallback = std::function<void(float pct, const juce::String& phase)>;

    static juce::String modelName(Model m) {
        return m == Model::FourStems ? "htdemucs_ft" : "vocft";
    }

    static juce::File findExecutable() {
        const auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                .getParentDirectory();

#if JUCE_WINDOWS
        const juce::StringArray binNames { "stemsep.exe" };
#else
        const juce::StringArray binNames { "stemsep" };
#endif

        juce::Array<juce::File> roots;
        roots.add(exeDir.getChildFile("tools").getChildFile("external").getChildFile("stemsep"));
        roots.add(exeDir.getChildFile("stemsep"));
        roots.add(exeDir.getParentDirectory().getChildFile("tools").getChildFile("external").getChildFile("stemsep"));

        juce::File dir = exeDir;
        for (int up = 0; up < 8 && dir.getParentDirectory() != dir; ++up) {
            roots.add(dir.getChildFile("tools").getChildFile("external").getChildFile("stemsep"));
            dir = dir.getParentDirectory();
        }

        for (auto& root : roots)
            for (auto& name : binNames) {
                auto c = root.getChildFile(name);
                if (c.existsAsFile())
                    return c;
            }

        return {};
    }

    static bool isAvailable() {
        return findExecutable().existsAsFile();
    }

    static bool isGpuWarmDone() { return gpuWarmDone().load(); }

    static juce::File rootCacheDir() {
        auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("BeatMate")
                        .getChildFile("stemsep");
        if (!base.isDirectory())
            base.createDirectory();
        return base;
    }

    static juce::File cacheDirFor(const juce::File& input, Model m) {
        const auto hash = juce::MD5(input.getFullPathName().toUTF8()).toHexString();
        return rootCacheDir().getChildFile(hash + "_" + modelName(m));
    }

    static Result loadCachedResult(const juce::File& input, Model m) {
        const auto outDir = cacheDirFor(input, m);
        if (!outDir.isDirectory()) {
            Result r;
            r.message = "Aucun cache stemsep";
            return r;
        }
        juce::Array<juce::File> produced;
        outDir.findChildFiles(produced, juce::File::findFiles, false, "*.wav");
        return collectStems(outDir, m, produced);
    }

    static bool cachedResultExists(const juce::File& input, Model m) {
        return loadCachedResult(input, m).ok;
    }

    Result separate(const juce::File& input, Model m, ProgressCallback onProgress = nullptr) {
        Result r;

        if (!input.existsAsFile()) {
            r.message = "Fichier source introuvable : " + input.getFullPathName();
            return r;
        }

        if (cachedResultExists(input, m))
            return loadCachedResult(input, m);

        std::lock_guard<std::mutex> serialise(runMutex());
        if (cachedResultExists(input, m))
            return loadCachedResult(input, m);

        return runSeparation(input, m, onProgress);
    }

private:
    static std::atomic<bool>& gpuWarmDone() {
        static std::atomic<bool> v{false};
        return v;
    }

    static std::mutex& runMutex() {
        static std::mutex m;
        return m;
    }

    static Result collectStems(const juce::File& outDir, Model m,
                               const juce::Array<juce::File>& produced) {
        Result r;

        auto matchOne = [&produced](const juce::String& tag) -> juce::File {
            for (auto& f : produced) {
                const auto n = f.getFileNameWithoutExtension().toLowerCase();
                if (n.contains("(" + tag.toLowerCase() + ")") || n.contains(tag.toLowerCase()))
                    return f;
            }
            return {};
        };

        if (m == Model::VocalsHQ) {
            juce::File vocals = matchOne("vocals");
            if (!vocals.existsAsFile()) vocals = matchOne("vocal");
            juce::File instr = matchOne("instrumental");
            if (!instr.existsAsFile()) instr = matchOne("instrum");
            if (!instr.existsAsFile()) instr = matchOne("no_vocals");
            if (!instr.existsAsFile()) instr = matchOne("inst");

            if (!vocals.existsAsFile() || !instr.existsAsFile()) {
                for (auto& f : produced) {
                    const auto n = f.getFileNameWithoutExtension().toLowerCase();
                    if (!vocals.existsAsFile() && n.contains("voc")) vocals = f;
                    else if (!instr.existsAsFile()) instr = f;
                }
            }

            r.fourStems = false;
            r.vocals = vocals;
            r.instrumental = instr;
            r.drums = instr;
            r.bass = instr;
            r.other = instr;
            r.ok = vocals.existsAsFile() && instr.existsAsFile();
            if (!r.ok)
                r.message = "Stems voix/instrumental introuvables dans " + outDir.getFullPathName();
            return r;
        }

        r.fourStems = true;
        r.drums = matchOne("drums");
        r.bass = matchOne("bass");
        r.other = matchOne("other");
        r.vocals = matchOne("vocals");
        if (!r.vocals.existsAsFile()) r.vocals = matchOne("vocal");
        r.instrumental = r.other;
        r.ok = r.drums.existsAsFile() && r.bass.existsAsFile()
            && r.other.existsAsFile() && r.vocals.existsAsFile();
        if (!r.ok)
            r.message = "Stems 4-pistes incomplets dans " + outDir.getFullPathName();
        return r;
    }

    Result runSeparation(const juce::File& input, Model m, const ProgressCallback& onProgress) {
        Result r;

        const auto exe = findExecutable();
        if (!exe.existsAsFile()) {
            r.message = "Moteur stemsep introuvable (tools/external/stemsep/stemsep.exe)";
            return r;
        }

        const auto outDir = cacheDirFor(input, m);
        if (!outDir.isDirectory())
            outDir.createDirectory();

        const auto modelDir = exe.getParentDirectory().getChildFile("models");
        if (modelDir.isDirectory()) {
           #if JUCE_WINDOWS
            SetEnvironmentVariableW(L"STEMSEP_MODEL_DIR", modelDir.getFullPathName().toWideCharPointer());
           #else
            setenv("STEMSEP_MODEL_DIR", modelDir.getFullPathName().toRawUTF8(), 1);
           #endif
        }

        const bool firstRun = !gpuWarmDone().load();
        if (onProgress) {
            const juce::String phase = firstRun
                ? juce::String::fromUTF8("Pr\xC3\xA9paration GPU (1er lancement, ~1-2 min)...")
                : juce::String::fromUTF8("Lancement de la s\xC3\xA9paration SOTA...");
            juce::MessageManager::callAsync([onProgress, phase] { onProgress(0.03f, phase); });
        }

        juce::StringArray args;
        args.add(exe.getFullPathName());
        args.add("--input");  args.add(input.getFullPathName());
        args.add("--out");    args.add(outDir.getFullPathName());
        args.add("--model");  args.add(modelName(m));
        args.add("--device"); args.add("auto");

        juce::ChildProcess proc;
        if (!proc.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            r.message = "Impossible de lancer stemsep.exe";
            return r;
        }

        {
            std::lock_guard<std::mutex> lk(procMutex);
            activeProc = &proc;
        }

        juce::Array<juce::File> produced;
        juce::String tail;
        char buffer[4096];
        for (;;) {
            const auto n = proc.readProcessOutput(buffer, (int) sizeof(buffer));
            if (n > 0) {
                tail += juce::String(juce::CharPointer_UTF8(buffer), (size_t) n);

                int searchFrom = 0;
                for (;;) {
                    const int idx = tail.indexOf(searchFrom, ">>> stem:");
                    if (idx < 0) break;
                    const int lineEnd = tail.indexOfChar(idx, '\n');
                    if (lineEnd < 0) break;
                    const juce::String path = tail.substring(idx + 9, lineEnd).trim();
                    if (path.isNotEmpty()) {
                        const juce::File f(path);
                        if (!produced.contains(f))
                            produced.add(f);
                    }
                    searchFrom = lineEnd + 1;
                }
                if (tail.length() > 16384)
                    tail = tail.getLastCharacters(16384);

                if (firstRun && !gpuWarmDone().load() && tail.containsIgnoreCase("stem:"))
                    gpuWarmDone().store(true);

                if (onProgress) {
                    const float mapped = produced.isEmpty() ? 0.20f : 0.85f;
                    const juce::String phase = produced.isEmpty()
                        ? juce::String::fromUTF8("S\xC3\xA9paration en cours...")
                        : juce::String::fromUTF8("\xC3\x89""criture des stems...");
                    juce::MessageManager::callAsync([onProgress, mapped, phase] { onProgress(mapped, phase); });
                }
            } else if (!proc.isRunning()) {
                break;
            } else {
                juce::Thread::sleep(80);
            }
        }

        proc.waitForProcessToFinish(900000);
        const auto exitCode = proc.getExitCode();

        {
            std::lock_guard<std::mutex> lk(procMutex);
            activeProc = nullptr;
        }

        gpuWarmDone().store(true);

        if (produced.isEmpty())
            outDir.findChildFiles(produced, juce::File::findFiles, false, "*.wav");

        if (onProgress)
            juce::MessageManager::callAsync([onProgress] { onProgress(0.97f, "Finalisation..."); });

        r = collectStems(outDir, m, produced);
        if (!r.ok)
            r.message = "stemsep exit=" + juce::String(exitCode)
                      + (tail.isNotEmpty() ? (" | " + tail.getLastCharacters(200)) : juce::String());
        return r;
    }

    std::mutex procMutex;
    juce::ChildProcess* activeProc = nullptr;
};

}
