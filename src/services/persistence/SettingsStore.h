#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace BeatMate::Services::Persistence {

// journal_mode=DELETE volontaire (aligne sur TrackDatabase, evite les corruptions WAL constatees)
class SettingsStore {
public:
    struct NamedBlob {
        std::string name;
        std::string jsonPayload;
        int64_t     updatedAt = 0;
    };

    SettingsStore();
    ~SettingsStore();

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    bool initialize(const std::string& dbPath);
    bool isOpen() const { return db_ != nullptr; }
    void close();

    bool               setKV(const std::string& key, const std::string& value);
    std::optional<std::string> getKV(const std::string& key) const;
    bool               removeKV(const std::string& key);

    bool                     upsertSetlist(const std::string& name, const std::string& jsonPayload);
    std::optional<NamedBlob> getSetlist  (const std::string& name) const;
    bool                     deleteSetlist(const std::string& name);
    std::vector<NamedBlob>   listSetlists() const;

    bool                     upsertFilterPreset(const std::string& name, const std::string& jsonPayload);
    std::optional<NamedBlob> getFilterPreset  (const std::string& name) const;
    bool                     deleteFilterPreset(const std::string& name);
    std::vector<NamedBlob>   listFilterPresets() const;

    bool                     upsertEventPlan(const std::string& name, const std::string& jsonPayload);
    std::optional<NamedBlob> getEventPlan  (const std::string& name) const;
    bool                     deleteEventPlan(const std::string& name);
    std::vector<NamedBlob>   listEventPlans() const;

    bool isMigrationDone(const std::string& tag) const;
    bool markMigrationDone(const std::string& tag);

    int  currentSchemaVersion() const;

private:
    bool ensureSchema();
    bool execSQL(const std::string& sql);

    bool                     upsertNamed(const char* table, const std::string& name, const std::string& payload);
    std::optional<NamedBlob> getNamed(const char* table, const std::string& name) const;
    bool                     deleteNamed(const char* table, const std::string& name);
    std::vector<NamedBlob>   listNamed(const char* table) const;

    sqlite3*    db_ = nullptr;
    std::string dbPath_;
    mutable std::mutex mutex_;

    static constexpr int CURRENT_SCHEMA_VERSION = 1;
};

} // namespace BeatMate::Services::Persistence
