#include "SettingsStore.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>

namespace BeatMate::Services::Persistence {

namespace {
// Allowlist compile-time des tables : évite l'injection SQL via noms de table (identifiants non paramétrables)
bool isKnownTable(const char* t)
{
    if (!t) return false;
    return std::strcmp(t, "setlists")        == 0
        || std::strcmp(t, "filter_presets")  == 0
        || std::strcmp(t, "event_plans")     == 0;
}

int64_t nowEpoch()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

SettingsStore::SettingsStore() = default;

SettingsStore::~SettingsStore()
{
    close();
}

bool SettingsStore::initialize(const std::string& dbPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    dbPath_ = dbPath;

    int rc = sqlite3_open(dbPath_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsStore] sqlite3_open failed ({}): {}",
                      dbPath_, sqlite3_errmsg(db_));
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    // Match TrackDatabase PRAGMAs so we do not flip journal modes mid-run.
    sqlite3_exec(db_, "PRAGMA journal_mode=DELETE", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=FULL",    nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON",     nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA busy_timeout=5000",   nullptr, nullptr, nullptr);

    if (!ensureSchema()) {
        spdlog::error("[SettingsStore] ensureSchema failed at {}", dbPath_);
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    spdlog::info("[SettingsStore] Initialized at {} (schema v{})",
                 dbPath_, CURRENT_SCHEMA_VERSION);
    return true;
}

void SettingsStore::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SettingsStore::execSQL(const std::string& sql)
{
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsStore] SQL error: {} -- in: {}", err ? err : "?", sql);
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

bool SettingsStore::ensureSchema()
{
    if (!execSQL(
        "CREATE TABLE IF NOT EXISTS schema_store_version ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  version INTEGER NOT NULL"
        ")")) return false;

    if (!execSQL(
        "CREATE TABLE IF NOT EXISTS kv_store ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL DEFAULT 0"
        ")")) return false;

    auto makeNamedTable = [this](const char* name) {
        std::string sql =
            std::string("CREATE TABLE IF NOT EXISTS ") + name + " ("
            "  name TEXT PRIMARY KEY,"
            "  payload TEXT NOT NULL,"
            "  updated_at INTEGER NOT NULL DEFAULT 0"
            ")";
        return execSQL(sql);
    };
    if (!makeNamedTable("setlists"))        return false;
    if (!makeNamedTable("filter_presets"))  return false;
    if (!makeNamedTable("event_plans"))     return false;

    if (!execSQL(
        "CREATE TABLE IF NOT EXISTS json_migrations ("
        "  tag TEXT PRIMARY KEY,"
        "  imported_at INTEGER NOT NULL"
        ")")) return false;

    int cur = currentSchemaVersion();
    if (cur < CURRENT_SCHEMA_VERSION) {
        std::string upsert =
            "INSERT INTO schema_store_version(id, version) VALUES (1, "
            + std::to_string(CURRENT_SCHEMA_VERSION)
            + ") ON CONFLICT(id) DO UPDATE SET version = excluded.version";
        if (!execSQL(upsert)) return false;
        spdlog::info("[SettingsStore] Upgraded schema {} -> {}", cur, CURRENT_SCHEMA_VERSION);
    }
    return true;
}

int SettingsStore::currentSchemaVersion() const
{
    // No lock here: callable under initialize() (already locked); non-atomic read tolerated, schema_version is monotonic
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT version FROM schema_store_version WHERE id = 1",
                           -1, &stmt, nullptr) != SQLITE_OK) return 0;
    int v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) v = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return v;
}

bool SettingsStore::setKV(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql =
        "INSERT INTO kv_store(key, value, updated_at) VALUES(?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SettingsStore] setKV prepare failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, nowEpoch());
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) spdlog::error("[SettingsStore] setKV step failed: {}", sqlite3_errmsg(db_));
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<std::string> SettingsStore::getKV(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM kv_store WHERE key = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) out = reinterpret_cast<const char*>(txt);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool SettingsStore::removeKV(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM kv_store WHERE key = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool SettingsStore::upsertNamed(const char* table, const std::string& name, const std::string& payload)
{
    if (!isKnownTable(table)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    std::string sql =
        std::string("INSERT INTO ") + table
        + "(name, payload, updated_at) VALUES(?, ?, ?) "
          "ON CONFLICT(name) DO UPDATE SET payload=excluded.payload, updated_at=excluded.updated_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SettingsStore] upsertNamed({}) prepare failed: {}", table, sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, nowEpoch());
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<SettingsStore::NamedBlob>
SettingsStore::getNamed(const char* table, const std::string& name) const
{
    if (!isKnownTable(table)) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return std::nullopt;

    std::string sql = std::string("SELECT name, payload, updated_at FROM ") + table + " WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<NamedBlob> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        NamedBlob b;
        const unsigned char* n = sqlite3_column_text(stmt, 0);
        const unsigned char* p = sqlite3_column_text(stmt, 1);
        if (n) b.name        = reinterpret_cast<const char*>(n);
        if (p) b.jsonPayload = reinterpret_cast<const char*>(p);
        b.updatedAt = sqlite3_column_int64(stmt, 2);
        out = std::move(b);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool SettingsStore::deleteNamed(const char* table, const std::string& name)
{
    if (!isKnownTable(table)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    std::string sql = std::string("DELETE FROM ") + table + " WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<SettingsStore::NamedBlob> SettingsStore::listNamed(const char* table) const
{
    std::vector<NamedBlob> out;
    if (!isKnownTable(table)) return out;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return out;
    std::string sql = std::string("SELECT name, payload, updated_at FROM ") + table + " ORDER BY updated_at DESC, name ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NamedBlob b;
        const unsigned char* n = sqlite3_column_text(stmt, 0);
        const unsigned char* p = sqlite3_column_text(stmt, 1);
        if (n) b.name        = reinterpret_cast<const char*>(n);
        if (p) b.jsonPayload = reinterpret_cast<const char*>(p);
        b.updatedAt = sqlite3_column_int64(stmt, 2);
        out.push_back(std::move(b));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool SettingsStore::upsertSetlist(const std::string& name, const std::string& j)   { return upsertNamed("setlists",       name, j); }
std::optional<SettingsStore::NamedBlob> SettingsStore::getSetlist(const std::string& name) const { return getNamed("setlists", name); }
bool SettingsStore::deleteSetlist(const std::string& name)                          { return deleteNamed("setlists",       name); }
std::vector<SettingsStore::NamedBlob> SettingsStore::listSetlists() const          { return listNamed("setlists"); }

bool SettingsStore::upsertFilterPreset(const std::string& name, const std::string& j)                 { return upsertNamed("filter_presets", name, j); }
std::optional<SettingsStore::NamedBlob> SettingsStore::getFilterPreset(const std::string& name) const { return getNamed("filter_presets", name); }
bool SettingsStore::deleteFilterPreset(const std::string& name)                                        { return deleteNamed("filter_presets", name); }
std::vector<SettingsStore::NamedBlob> SettingsStore::listFilterPresets() const                        { return listNamed("filter_presets"); }

bool SettingsStore::upsertEventPlan(const std::string& name, const std::string& j)                 { return upsertNamed("event_plans", name, j); }
std::optional<SettingsStore::NamedBlob> SettingsStore::getEventPlan(const std::string& name) const { return getNamed("event_plans", name); }
bool SettingsStore::deleteEventPlan(const std::string& name)                                        { return deleteNamed("event_plans", name); }
std::vector<SettingsStore::NamedBlob> SettingsStore::listEventPlans() const                        { return listNamed("event_plans"); }

bool SettingsStore::isMigrationDone(const std::string& tag) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM json_migrations WHERE tag = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    bool found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

bool SettingsStore::markMigrationDone(const std::string& tag)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO json_migrations(tag, imported_at) VALUES(?, ?)",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, nowEpoch());
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace BeatMate::Services::Persistence
