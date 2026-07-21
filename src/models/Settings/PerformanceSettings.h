#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace BeatMate::Models::Settings {

struct PerformanceSettings {
    int64_t maxMemoryMB = 2048;
    int64_t waveformCacheMB = 512;
    int64_t albumArtCacheMB = 256;

    int threadCount = 0;                // 0 = auto-detect
    int analysisThreads = 2;
    int ioThreads = 2;
    int decodingThreads = 2;

    bool gpuAcceleration = false;
    std::string gpuDevice = "auto";     // "auto", "cuda:0", "opencl:0", etc.
    bool gpuWaveformRendering = true;
    bool gpuStemSeparation = true;

    int64_t cacheSizeMB = 1024;
    std::string cacheDirectory;
    bool preloadWaveforms = true;
    bool cacheDecodedAudio = false;
    int maxCachedTracks = 100;

    int dbConnectionPoolSize = 5;
    bool dbWriteAheadLog = true;
    int dbCacheSizeKB = 8192;

    int audioBufferCount = 3;
    bool useMemoryMappedFiles = true;
    bool prefetchAudio = true;
    int prefetchSeconds = 30;

    bool buildSearchIndex = true;
    bool inMemorySearchIndex = true;

    int targetFPS = 60;
    bool vsync = true;
    bool hardwareRendering = true;
    int maxWaveformPoints = 100000;

    int networkTimeoutMs = 30000;
    int maxConcurrentDownloads = 3;
    bool useConnectionPooling = true;

    bool reduceCPUOnBattery = true;
    bool disableAnimationsOnBattery = false;

    PerformanceSettings() = default;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PerformanceSettings,
        maxMemoryMB, waveformCacheMB, albumArtCacheMB,
        threadCount, analysisThreads, ioThreads, decodingThreads,
        gpuAcceleration, gpuDevice, gpuWaveformRendering, gpuStemSeparation,
        cacheSizeMB, cacheDirectory, preloadWaveforms, cacheDecodedAudio, maxCachedTracks,
        dbConnectionPoolSize, dbWriteAheadLog, dbCacheSizeKB,
        audioBufferCount, useMemoryMappedFiles, prefetchAudio, prefetchSeconds,
        buildSearchIndex, inMemorySearchIndex,
        targetFPS, vsync, hardwareRendering, maxWaveformPoints,
        networkTimeoutMs, maxConcurrentDownloads, useConnectionPooling,
        reduceCPUOnBattery, disableAnimationsOnBattery
    )
};

} // namespace BeatMate::Models::Settings
