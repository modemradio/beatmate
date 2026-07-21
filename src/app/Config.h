#pragma once

#include <string>
#include <cstdint>

namespace BeatMate {

struct Config
{
    static constexpr const char* APP_NAME = "BeatMate";
    static constexpr const char* APP_VERSION = BEATMATE_VERSION;
    static constexpr int VERSION_MAJOR = BEATMATE_VERSION_MAJOR;
    static constexpr int VERSION_MINOR = BEATMATE_VERSION_MINOR;
    static constexpr int VERSION_PATCH = BEATMATE_VERSION_PATCH;
    static constexpr const char* ORGANIZATION = "BeatMate";
    static constexpr const char* APP_DOMAIN = "beatmate.fr";

    static constexpr int DEFAULT_SAMPLE_RATE = 44100;
    static constexpr int DEFAULT_BUFFER_SIZE = 512;
    static constexpr int DEFAULT_CHANNELS = 2;
    static constexpr int DEFAULT_BIT_DEPTH = 16;
    static constexpr float MAX_AUDIO_LATENCY_MS = 10.0f;

    static constexpr int FFT_SIZE = 4096;
    static constexpr int FFT_HOP_SIZE = 512;
    static constexpr float BPM_MIN = 60.0f;
    static constexpr float BPM_MAX = 200.0f;
    static constexpr float BPM_ACCURACY_TARGET = 0.85f;
    static constexpr float KEY_ACCURACY_TARGET = 0.75f;

    static constexpr int WAVEFORM_RESOLUTION_LOW = 256;
    static constexpr int WAVEFORM_RESOLUTION_MED = 1024;
    static constexpr int WAVEFORM_RESOLUTION_HIGH = 4096;

    static constexpr int TARGET_FPS = 60;
    static constexpr float FRAME_BUDGET_MS = 16.67f;
    static constexpr int MAX_HOT_CUES_PER_DECK = 8;
    static constexpr int SAMPLER_PAD_COUNT = 16;

    static constexpr const char* DB_FILENAME = "beatmate.db";
    static constexpr int DB_VERSION = 1;

    static constexpr int COLLECTION_SYNC_INTERVAL_SEC = 60;

    static constexpr size_t MAX_MEMORY_CACHE_MB = 512;
    static constexpr int STEM_CACHE_MAX_DAYS = 30;
    static constexpr int TEMP_FILE_EXPIRY_MIN = 10;

    static constexpr size_t MAX_MEMORY_MB = 2048;
    static constexpr int THREAD_POOL_SIZE = 4;

    static constexpr int TRIAL_DAYS = 30;
    static constexpr const char* LICENSE_FORMAT = "XXXXX-XXXXX-XXXXX-XXXXX";

    static constexpr int HTTP_TIMEOUT_SEC = 30;
    static constexpr int WEBSOCKET_RECONNECT_SEC = 5;
    static constexpr int MAX_RETRY_ATTEMPTS = 3;

    static constexpr const char* SETTINGS_FILE = "appsettings.json";
    static constexpr const char* STEMS_CACHE_DIR = "StemsCache";
    static constexpr const char* BACKUP_DIR = "Backups";
    static constexpr const char* PLUGINS_DIR = "Plugins";
    static constexpr const char* LOGS_DIR = "Logs";
    static constexpr const char* TEMP_AUDIO_DIR = "TempAudio";
};

} // namespace BeatMate
