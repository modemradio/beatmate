#include "VSTPluginHost.h"

#include <cstdlib>

#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>

namespace BeatMate::Services::Plugins {

VSTPluginHost::VSTPluginHost() {
    formatManager_.addDefaultFormats();
}

const juce::KnownPluginList& VSTPluginHost::scan(bool force) {
    if (scanned_ && !force) return knownList_;

    juce::FileSearchPath searchPath;
#ifdef _WIN32
    // Prefer %COMMONPROGRAMFILES% (the localized canonical location) over
    if (auto* cpf = std::getenv("COMMONPROGRAMFILES"); cpf && *cpf) {
        searchPath.add(juce::File(juce::String::fromUTF8(cpf)).getChildFile("VST3"));
    }
    if (auto* cpf86 = std::getenv("CommonProgramFiles(x86)"); cpf86 && *cpf86) {
        searchPath.add(juce::File(juce::String::fromUTF8(cpf86)).getChildFile("VST3"));
    }
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("AppData\\Local\\Programs\\Common\\VST3"));
#else
    searchPath.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
    searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins/VST3"));
#endif

    for (int i = 0; i < formatManager_.getNumFormats(); ++i) {
        auto* fmt = formatManager_.getFormat(i);
        if (!fmt) continue;
        juce::PluginDirectoryScanner scanner(knownList_, *fmt, searchPath,
                                             /*recursive=*/true, /*deadMansPedalFile=*/{});
        juce::String nextFile;
        while (scanner.scanNextFile(true, nextFile)) {
            // Progress silently.
        }
    }
    scanned_ = true;
    spdlog::info("[VST] Scan done: {} plugin(s) found", knownList_.getNumTypes());
    return knownList_;
}

std::vector<juce::PluginDescription> VSTPluginHost::getDescriptions() const {
    std::vector<juce::PluginDescription> out;
    const auto list = knownList_.getTypes();
    out.reserve((size_t)list.size());
    for (const auto& t : list) out.push_back(t);
    return out;
}

std::unique_ptr<juce::AudioPluginInstance>
VSTPluginHost::loadPlugin(const juce::PluginDescription& desc,
                          double sampleRate, int blockSize,
                          juce::String& error)
{
    auto inst = formatManager_.createPluginInstance(desc, sampleRate, blockSize, error);
    if (!inst) {
        spdlog::error("[VST] createPluginInstance failed: {}", error.toStdString());
        return nullptr;
    }
    // setPlayConfigDetails() must be called BEFORE prepareToPlay(): JUCE
    inst->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    inst->prepareToPlay(sampleRate, blockSize);
    return inst;
}

} // namespace BeatMate::Services::Plugins
