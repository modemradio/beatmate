#include "StemSidecar.h"

#include <spdlog/spdlog.h>

#include <array>
#include <memory>

namespace BeatMate::Core {

namespace {

constexpr std::array<const char*, 4> kStemNames { "vocals", "drums", "bass", "other" };
constexpr int kBitDepth = 16;
constexpr int kChunkSize = 65536;

static juce::File stemFile(const juce::File& root,
                           const juce::String& clipId,
                           const char* stemName)
{
    return root.getChildFile(clipId + "_" + stemName + ".wav");
}

static juce::AudioBuffer<float>* bufferAt(StemSidecar::StemSet& s, int idx)
{
    switch (idx) {
        case 0: return &s.vocals;
        case 1: return &s.drums;
        case 2: return &s.bass;
        case 3: return &s.other;
        default: return nullptr;
    }
}

static const juce::AudioBuffer<float>* bufferAt(const StemSidecar::StemSet& s, int idx)
{
    switch (idx) {
        case 0: return &s.vocals;
        case 1: return &s.drums;
        case 2: return &s.bass;
        case 3: return &s.other;
        default: return nullptr;
    }
}

static bool writeWav(const juce::File& dest,
                     const juce::AudioBuffer<float>& buffer,
                     double sampleRate)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0) {
        spdlog::warn("StemSidecar: refus d'ecrire un buffer vide -> {}",
                     dest.getFullPathName().toStdString());
        return false;
    }

    if (dest.exists()) {
        dest.deleteFile();
    }

    auto outputStream = std::make_unique<juce::FileOutputStream>(dest);
    if (outputStream->failedToOpen()) {
        spdlog::error("StemSidecar: impossible d'ouvrir {} en ecriture",
                      dest.getFullPathName().toStdString());
        return false;
    }

    juce::WavAudioFormat wavFormat;
    juce::StringPairArray metadata;

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(outputStream.get(),
                                  sampleRate,
                                  static_cast<unsigned int>(buffer.getNumChannels()),
                                  kBitDepth,
                                  metadata,
                                  /*qualityOptionIndex=*/0));

    if (writer == nullptr) {
        spdlog::error("StemSidecar: createWriterFor a echoue pour {}",
                      dest.getFullPathName().toStdString());
        return false;
    }

    outputStream.release();

    const int total = buffer.getNumSamples();
    int written = 0;
    while (written < total) {
        const int n = juce::jmin(kChunkSize, total - written);
        if (! writer->writeFromAudioSampleBuffer(buffer, written, n)) {
            spdlog::error("StemSidecar: writeFromAudioSampleBuffer a echoue (frame {})",
                          written);
            writer.reset();
            dest.deleteFile();
            return false;
        }
        written += n;
    }

    writer.reset();
    return true;
}

static bool readWav(const juce::File& src,
                    juce::AudioBuffer<float>& outBuffer,
                    double& outSampleRate)
{
    if (! src.existsAsFile()) {
        return false;
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(new juce::FileInputStream(src),
                                  /*deleteStreamIfOpeningFails=*/true));

    if (reader == nullptr) {
        spdlog::error("StemSidecar: lecture WAV impossible -> {}",
                      src.getFullPathName().toStdString());
        return false;
    }

    const int numChannels = static_cast<int>(reader->numChannels);
    const int numSamples  = static_cast<int>(reader->lengthInSamples);

    if (numChannels <= 0 || numSamples <= 0) {
        spdlog::warn("StemSidecar: WAV vide -> {}",
                     src.getFullPathName().toStdString());
        return false;
    }

    juce::AudioBuffer<float> tmp(numChannels, numSamples);
    if (! reader->read(&tmp, 0, numSamples, 0, true, true)) {
        spdlog::error("StemSidecar: read() a echoue -> {}",
                      src.getFullPathName().toStdString());
        return false;
    }

    outBuffer = std::move(tmp);
    outSampleRate = reader->sampleRate;
    return true;
}

} // namespace

juce::String StemSidecar::makeClipId(const juce::String& filePath,
                                      double audioInSec,
                                      double audioOutSec) noexcept
{
   #if JUCE_WINDOWS
    const juce::String normPath = filePath.replaceCharacter('\\', '/').toLowerCase();
   #else
    const juce::String normPath = filePath.replaceCharacter('\\', '/');
   #endif

    juce::String key;
    key << normPath
        << "|in=" << juce::String(audioInSec, 6)
        << "|out=" << juce::String(audioOutSec, 6);

    const juce::int64 h = key.hashCode64();
    return juce::String::toHexString(h).paddedLeft('0', 16);
}

bool StemSidecar::save(const juce::File& sidecarRoot,
                       const juce::String& clipId,
                       const StemSet& stems)
{
    if (clipId.isEmpty()) {
        spdlog::error("StemSidecar::save: clipId vide");
        return false;
    }

    if (! stems.ready) {
        spdlog::warn("StemSidecar::save: StemSet.ready=false, save quand meme demande");
    }

    if (! sidecarRoot.exists()) {
        const auto res = sidecarRoot.createDirectory();
        if (res.failed()) {
            spdlog::error("StemSidecar::save: createDirectory a echoue ({}) -> {}",
                          res.getErrorMessage().toStdString(),
                          sidecarRoot.getFullPathName().toStdString());
            return false;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const auto* buf = bufferAt(stems, i);
        const auto dest = stemFile(sidecarRoot, clipId, kStemNames[i]);
        if (buf == nullptr || ! writeWav(dest, *buf, stems.sampleRate)) {
            spdlog::error("StemSidecar::save: echec stem '{}' pour clip {}",
                          kStemNames[i], clipId.toStdString());
            return false;
        }
    }

    spdlog::info("StemSidecar::save: 4 stems ecrits pour clip {} dans {}",
                 clipId.toStdString(),
                 sidecarRoot.getFullPathName().toStdString());
    return true;
}

bool StemSidecar::load(const juce::File& sidecarRoot,
                       const juce::String& clipId,
                       StemSet& outStems)
{
    if (clipId.isEmpty()) {
        spdlog::error("StemSidecar::load: clipId vide");
        outStems.ready = false;
        return false;
    }

    if (! exists(sidecarRoot, clipId)) {
        outStems.ready = false;
        return false;
    }

    juce::AudioBuffer<float> tmpBuffers[4];
    double tmpSampleRate = 0.0;

    for (int i = 0; i < 4; ++i) {
        const auto src = stemFile(sidecarRoot, clipId, kStemNames[i]);
        double sr = 0.0;
        if (! readWav(src, tmpBuffers[i], sr)) {
            spdlog::warn("StemSidecar::load: lecture stem '{}' a echoue pour clip {}",
                         kStemNames[i], clipId.toStdString());
            outStems.ready = false;
            return false;
        }
        if (i == 0) {
            tmpSampleRate = sr;
        } else if (sr != tmpSampleRate) {
            spdlog::warn("StemSidecar::load: sampleRate inconsistant entre stems "
                         "(stem {} : {} Hz, ref {} Hz) pour clip {}",
                         kStemNames[i], sr, tmpSampleRate, clipId.toStdString());
        }
    }

    outStems.vocals = std::move(tmpBuffers[0]);
    outStems.drums  = std::move(tmpBuffers[1]);
    outStems.bass   = std::move(tmpBuffers[2]);
    outStems.other  = std::move(tmpBuffers[3]);
    outStems.sampleRate = tmpSampleRate;
    outStems.ready = true;

    spdlog::info("StemSidecar::load: 4 stems charges pour clip {} ({} Hz)",
                 clipId.toStdString(), tmpSampleRate);
    return true;
}

bool StemSidecar::exists(const juce::File& sidecarRoot,
                          const juce::String& clipId)
{
    if (clipId.isEmpty() || ! sidecarRoot.isDirectory()) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (! stemFile(sidecarRoot, clipId, kStemNames[i]).existsAsFile()) {
            return false;
        }
    }
    return true;
}

void StemSidecar::erase(const juce::File& sidecarRoot,
                         const juce::String& clipId)
{
    if (clipId.isEmpty() || ! sidecarRoot.isDirectory()) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        const auto f = stemFile(sidecarRoot, clipId, kStemNames[i]);
        if (f.existsAsFile()) {
            f.deleteFile();
        }
    }
    spdlog::info("StemSidecar::erase: stems effaces pour clip {}",
                 clipId.toStdString());
}

} // namespace BeatMate::Core
