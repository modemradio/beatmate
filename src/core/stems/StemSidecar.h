#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace BeatMate::Core {

// Sidecar WAV des 4 stems dans <projet>.stems/ ; methodes statiques SYNCHRONES bloquantes — jamais depuis le thread audio ; serialiser les acces a un meme clipId.
class StemSidecar {
public:
    struct StemSet {
        juce::AudioBuffer<float> vocals;
        juce::AudioBuffer<float> drums;
        juce::AudioBuffer<float> bass;
        juce::AudioBuffer<float> other;
        double sampleRate { 44100.0 };
        bool ready { false };
    };

    static juce::String makeClipId(const juce::String& filePath,
                                    double audioInSec,
                                    double audioOutSec) noexcept;

    static bool save(const juce::File& sidecarRoot,
                     const juce::String& clipId,
                     const StemSet& stems);

    static bool load(const juce::File& sidecarRoot,
                     const juce::String& clipId,
                     StemSet& outStems);

    static bool exists(const juce::File& sidecarRoot,
                       const juce::String& clipId);

    static void erase(const juce::File& sidecarRoot,
                      const juce::String& clipId);
};

} // namespace BeatMate::Core
