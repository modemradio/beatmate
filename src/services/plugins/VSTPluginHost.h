#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <vector>

namespace BeatMate::Services::Plugins {

class VSTPluginHost {
public:
    VSTPluginHost();
    ~VSTPluginHost() = default;

    const juce::KnownPluginList& scan(bool force = false);

    std::vector<juce::PluginDescription> getDescriptions() const;

    std::unique_ptr<juce::AudioPluginInstance>
    loadPlugin(const juce::PluginDescription& desc,
               double sampleRate,
               int blockSize,
               juce::String& error);

private:
    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList          knownList_;
    bool                            scanned_ = false;
};

} // namespace BeatMate::Services::Plugins
