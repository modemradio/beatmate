#include "RgbPeaksGenerator.h"
#include <algorithm>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <spdlog/spdlog.h>

namespace BeatMate::Core {

juce::File RgbPeaksGenerator::cacheDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BeatMate").getChildFile("waveform_cache");
    dir.createDirectory();
    return dir;
}

juce::File RgbPeaksGenerator::cacheFileFor(const std::string& audioPath)
{
    juce::String key = juce::String::toHexString(juce::String(audioPath).hashCode64());
    return cacheDirectory().getChildFile(key + ".rgbpeaks3");
}

bool RgbPeaksGenerator::isCacheValid(const std::string& audioPath)
{
    juce::File cache = cacheFileFor(audioPath);
    if (!cache.existsAsFile())
        return false;
    juce::FileInputStream fis(cache);
    if (!fis.openedOk())
        return false;
    double dur = 0.0;
    int32_t np = 0;
    if (fis.read(&dur, sizeof(double)) != sizeof(double))
        return false;
    if (fis.read(&np, sizeof(int32_t)) != sizeof(int32_t))
        return false;
    if (np <= 0 || np >= 500000 || dur <= 0.0)
        return false;
    const int64_t expected = static_cast<int64_t>(sizeof(double) + sizeof(int32_t))
                             + 4LL * np * static_cast<int64_t>(sizeof(float));
    if (cache.getSize() != expected)
        return false;
    juce::File source{ juce::String(audioPath) };
    if (source.existsAsFile()
        && source.getLastModificationTime() > cache.getLastModificationTime())
        return false;
    return true;
}

bool RgbPeaksGenerator::read(const juce::File& cacheFile, RgbPeaksData& out)
{
    juce::FileInputStream fis(cacheFile);
    if (!fis.openedOk())
        return false;
    double dur = 0.0;
    if (fis.read(&dur, sizeof(double)) != sizeof(double))
        return false;
    int32_t np = 0;
    if (fis.read(&np, sizeof(int32_t)) != sizeof(int32_t))
        return false;
    if (np <= 0 || np >= 500000)
        return false;
    size_t n = static_cast<size_t>(np);
    out.duration = dur;
    out.peaks.resize(n);
    out.bass.resize(n);
    out.mid.resize(n);
    out.treble.resize(n);
    const int bytes = np * static_cast<int>(sizeof(float));
    if (fis.read(out.peaks.data(), bytes) != bytes) return false;
    if (fis.read(out.bass.data(), bytes) != bytes) return false;
    if (fis.read(out.mid.data(), bytes) != bytes) return false;
    if (fis.read(out.treble.data(), bytes) != bytes) return false;
    return true;
}

bool RgbPeaksGenerator::write(const juce::File& cacheFile, const RgbPeaksData& data)
{
    if (!data.valid())
        return false;
    cacheFile.deleteFile();
    juce::FileOutputStream fos(cacheFile);
    if (!fos.openedOk())
        return false;
    fos.write(&data.duration, sizeof(double));
    int32_t np = static_cast<int32_t>(data.peaks.size());
    fos.write(&np, sizeof(int32_t));
    fos.write(data.peaks.data(), data.peaks.size() * sizeof(float));
    fos.write(data.bass.data(), data.bass.size() * sizeof(float));
    fos.write(data.mid.data(), data.mid.size() * sizeof(float));
    fos.write(data.treble.data(), data.treble.size() * sizeof(float));
    fos.flush();
    return true;
}

RgbPeaksData RgbPeaksGenerator::generate(const std::string& audioPath,
                                         const std::function<bool()>& shouldAbort,
                                         const std::function<void(const RgbPeaksData&)>& onPartial)
{
    RgbPeaksData data;
    auto aborted = [&shouldAbort]() { return shouldAbort && shouldAbort(); };

    juce::AudioFormatManager fmtMgr;
    fmtMgr.registerBasicFormats();
    juce::File audioFile{ juce::String(audioPath) };
    std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(audioFile));
    if (!reader || aborted())
        return data;

    const int64_t totalFrames = static_cast<int64_t>(reader->lengthInSamples);
    const int sr = static_cast<int>(reader->sampleRate);
    if (totalFrames <= 0 || sr <= 0)
        return data;
    data.duration = static_cast<double>(totalFrames) / sr;

    const int numPeaks = std::max(1000, static_cast<int>(data.duration * 400.0));
    const int64_t framesPerPeak = std::max<int64_t>(1, totalFrames / numPeaks);
    data.peaks.assign(static_cast<size_t>(numPeaks), 0.0f);
    data.bass.assign(static_cast<size_t>(numPeaks), 0.0f);
    data.mid.assign(static_cast<size_t>(numPeaks), 0.0f);
    data.treble.assign(static_cast<size_t>(numPeaks), 0.0f);

    const int blockSize = 8192;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), blockSize);

    const float aLow  = juce::jlimit(0.0f, 1.0f, 2.0f * juce::MathConstants<float>::pi * 200.0f / (float) sr);
    const float aHigh = juce::jlimit(0.0f, 1.0f, 2.0f * juce::MathConstants<float>::pi * 4000.0f / (float) sr);
    float lpB1 = 0.0f, lpB2 = 0.0f;
    float lpH1 = 0.0f, lpH2 = 0.0f;

    const int progressStep = std::max(1, numPeaks / 6);
    int nextProgress = std::max(1, numPeaks / 64);

    for (int p = 0; p < numPeaks; ++p)
    {
        if (aborted())
        {
            data = RgbPeaksData{};
            return data;
        }

        int64_t startFrame = p * framesPerPeak;
        int64_t endFrame = std::min(startFrame + framesPerPeak, totalFrames);
        float maxAll = 0.0f;
        double sumBass = 0.0, sumMid = 0.0, sumTreble = 0.0;
        int64_t nSamples = 0;

        for (int64_t pos = startFrame; pos < endFrame; pos += blockSize)
        {
            int samplesToRead = static_cast<int>(std::min<int64_t>(blockSize, endFrame - pos));
            reader->read(&buffer, 0, samplesToRead, pos, true, true);

            const int chans = static_cast<int>(reader->numChannels);
            for (int s = 0; s < samplesToRead; ++s)
            {
                float v = buffer.getSample(0, s);
                if (chans > 1) v = 0.5f * (v + buffer.getSample(1, s));
                const float absV = std::abs(v);

                lpB1 += aLow  * (v - lpB1);
                lpB2 += aLow  * (lpB1 - lpB2);
                lpH1 += aHigh * (v - lpH1);
                const float hp1 = v - lpH1;
                lpH2 += aHigh * (hp1 - lpH2);
                const float bassV   = lpB2;
                const float trebleV = hp1 - lpH2;
                const float midV    = v - bassV - trebleV;

                if (absV > maxAll) maxAll = absV;
                sumBass   += static_cast<double>(bassV)   * bassV;
                sumMid    += static_cast<double>(midV)    * midV;
                sumTreble += static_cast<double>(trebleV) * trebleV;
            }
            nSamples += samplesToRead;
        }

        const size_t sp = static_cast<size_t>(p);
        const double invN = nSamples > 0 ? 1.0 / static_cast<double>(nSamples) : 0.0;
        data.peaks[sp]  = maxAll;
        data.bass[sp]   = static_cast<float>(std::sqrt(sumBass * invN));
        data.mid[sp]    = static_cast<float>(std::sqrt(sumMid * invN));
        data.treble[sp] = static_cast<float>(std::sqrt(sumTreble * invN));

        if (p >= nextProgress && onPartial && !aborted())
        {
            nextProgress = std::min(nextProgress * 2, nextProgress + progressStep);
            onPartial(data);
        }
    }

    if (data.valid())
    {
        juce::File cache = cacheFileFor(audioPath);
        juce::File tmp = cache.getSiblingFile(cache.getFileName() + ".tmp");
        if (write(tmp, data) && tmp.moveFileTo(cache))
        {
            const juce::String key = juce::String::toHexString(juce::String(audioPath).hashCode64());
            cacheDirectory().getChildFile(key + ".peaks").deleteFile();
            cacheDirectory().getChildFile(key + ".rgbpeaks").deleteFile();
        }
        else
        {
            tmp.deleteFile();
        }
    }

    return data;
}

}
