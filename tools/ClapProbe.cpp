#include "services/ai/ClapEncoder.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using BeatMate::Services::AI::ClapEncoder;

namespace {

std::vector<juce::String> commandLine() {
    std::vector<juce::String> args;
#ifdef _WIN32
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        LocalFree(argv);
    }
#endif
    return args;
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto args = commandLine();

    juce::String refPath;
    bool tagMode = false;
    std::vector<juce::String> files;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--model" && i + 1 < args.size())
            ClapEncoder::setModelDirectoryOverride(juce::File(args[++i]));
        else if (args[i] == "--ref" && i + 1 < args.size())
            refPath = args[++i];
        else if (args[i] == "--tag")
            tagMode = true;
        else
            files.push_back(args[i]);
    }
    if (files.empty()) {
        std::printf("usage: BeatMateClapProbe [--model <dir>] [--ref <ref.vec>] [--tag] <audio> [audio...]\n");
        return 2;
    }
    ClapEncoder enc;
    if (! enc.isAvailable()) {
        std::printf("ERREUR: modele CLAP introuvable dans %s\n",
                    enc.modelDirectory().getFullPathName().toRawUTF8());
        return 3;
    }
    std::vector<std::vector<float>> vecs;
    for (const auto& f : files) {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto v = enc.encodeFile(juce::File(f));
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        if (v.empty()) {
            std::printf("ECHEC encode: %s\n", f.toRawUTF8());
            return 4;
        }
        double n = 0.0;
        for (float x : v) n += (double) x * (double) x;
        std::printf("OK  dim=%d norme=%.6f  %.0f ms  %s\n",
                    (int) v.size(), std::sqrt(n), ms, juce::File(f).getFileName().toRawUTF8());
        if (tagMode) {
            auto top = enc.topGenreLabels(v, 3);
            if (top.empty()) std::printf("    (labels zero-shot indisponibles)\n");
            for (const auto& t : top)
                std::printf("    tag %-22s %.3f\n", t.label.c_str(), t.score);
            for (const auto& t : enc.topMoodLabels(v, 2))
                std::printf("    mood %-21s %.3f\n", t.label.c_str(), t.score);
        }
        vecs.push_back(std::move(v));
    }

    if (refPath.isNotEmpty()) {
        auto ref = ClapEncoder::readVecFile(juce::File(refPath));
        if (ref.empty()) {
            std::printf("ECHEC lecture ref: %s\n", refPath.toRawUTF8());
            return 5;
        }
        const float c = ClapEncoder::cosine(vecs[0], ref);
        std::printf("PARITE cos=%.6f %s\n", c, c > 0.999f ? "PASS" : "FAIL");
        return c > 0.999f ? 0 : 1;
    }

    for (size_t i = 0; i < vecs.size(); ++i)
        for (size_t j = i + 1; j < vecs.size(); ++j)
            std::printf("cos[%d,%d]=%.4f  %s | %s\n", (int) i, (int) j,
                        ClapEncoder::cosine(vecs[i], vecs[j]),
                        juce::File(files[i]).getFileName().toRawUTF8(),
                        juce::File(files[j]).getFileName().toRawUTF8());
    return 0;
}
