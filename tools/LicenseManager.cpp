/**
 * BeatMate V11 - License Manager Tool
 * Outil separé pour generer, valider et gerer les licences
 *
 * Fonctions :
 * - Generer des cles de licence (Personal, Professional, Family, Enterprise)
 * - Valider une cle
 * - Afficher les infos d'une cle
 * - Generer des cles en lot
 * - Blacklister une cle
 *
 * Usage :
 *   LicenseManager generate <type> [count]
 *   LicenseManager validate <key>
 *   LicenseManager info <key>
 *   LicenseManager batch <type> <count> <output_file>
 *   LicenseManager hwid
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>

// ============================================================================
// Luhn checksum (same as LicenseValidator)
// ============================================================================
static bool luhnCheck(const std::string& input)
{
    // Must match LicenseValidator::luhnCheck exactly
    int sum = 0;
    bool alternate = false;
    for (int i = static_cast<int>(input.size()) - 1; i >= 0; --i) {
        int n = 0;
        char c = input[static_cast<size_t>(i)];
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'A' && c <= 'Z') n = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') n = c - 'a' + 10;
        if (alternate) {
            n *= 2;
            if (n > 9) n -= 9;
        }
        sum += n;
        alternate = !alternate;
    }
    return sum % 10 == 0;
}

// ============================================================================
// Generate a single license segment (5 chars)
// ============================================================================
static std::string generateSegment(std::mt19937& rng, int length = 5)
{
    static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::uniform_int_distribution<> dist(0, static_cast<int>(sizeof(chars)) - 2);
    std::string result;
    for (int i = 0; i < length; ++i)
        result += chars[dist(rng)];
    return result;
}

// ============================================================================
// Generate a license key with type prefix
// Type encoding in first segment:
//   P = Personal, R = Professional, F = Family, E = Enterprise
// ============================================================================
static std::string generateLicenseKey(const std::string& type, std::mt19937& rng)
{
    char prefix = 'P'; // Personal
    if (type == "Professional" || type == "Pro") prefix = 'R';
    else if (type == "Premium" || type == "Enterprise" || type == "Family") prefix = 'E';

    // Generate key with type prefix
    std::string key;
    key += prefix;
    key += generateSegment(rng, 4); // First segment: prefix + 4 chars
    key += "-";
    key += generateSegment(rng);
    key += "-";
    key += generateSegment(rng);
    key += "-";

    // Last segment: generate 4 chars + Luhn check char
    std::string lastSeg = generateSegment(rng, 4);

    // Extract all alphanumeric chars for Luhn calculation
    std::string allAlnum;
    for (char c : key + lastSeg)
        if (c != '-') allAlnum += c;

    // Try each candidate char to find one that satisfies Luhn
    static const char candidates[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    for (char c : candidates) {
        std::string test = allAlnum + c;
        if (luhnCheck(test)) {
            lastSeg += c;
            break;
        }
    }

    return key + lastSeg;
}

// ============================================================================
// Validate a key
// ============================================================================
static bool validateKey(const std::string& key)
{
    // Format: XXXXX-XXXXX-XXXXX-XXXXX (23 chars)
    if (key.size() != 23) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        if (i == 5 || i == 11 || i == 17) { if (key[i] != '-') return false; }
        else { if (!std::isalnum(static_cast<unsigned char>(key[i]))) return false; }
    }

    // Luhn check on alphanumeric chars (same algo as LicenseValidator)
    std::string alnum;
    for (char c : key)
        if (c != '-') alnum += c;
    return luhnCheck(alnum);
}

// ============================================================================
// Get license type from key prefix
// ============================================================================
static std::string getTypeFromKey(const std::string& key)
{
    if (key.empty()) return "Unknown";
    switch (key[0]) {
        case 'P': return "Personal (1 machine)";
        case 'R': return "Professional (1 machine)";
        case 'E': return "Premium (tout debloque)";
        default:  return "Standard";
    }
}

// ============================================================================
// LICENSE DATABASE (SQLite)
// ============================================================================
#include <sqlite3.h>

static sqlite3* openLicenseDB()
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open("licenses.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Erreur DB: " << sqlite3_errmsg(db) << std::endl;
        return nullptr;
    }

    // Create tables if not exist
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS licenses (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            license_key TEXT UNIQUE NOT NULL,
            type TEXT NOT NULL,
            status TEXT DEFAULT 'available',
            created_at TEXT DEFAULT (datetime('now')),
            activated_at TEXT,
            expires_at TEXT,
            nom TEXT,
            prenom TEXT,
            email TEXT,
            mac_address TEXT,
            hwid TEXT,
            machine_name TEXT,
            notes TEXT
        );
        CREATE TABLE IF NOT EXISTS activation_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            license_key TEXT NOT NULL,
            action TEXT NOT NULL,
            timestamp TEXT DEFAULT (datetime('now')),
            hwid TEXT,
            mac_address TEXT,
            ip_address TEXT
        );
    )";

    char* err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) { std::cerr << "SQL Error: " << err << std::endl; sqlite3_free(err); }
    return db;
}

static void dbAddLicense(sqlite3* db, const std::string& key, const std::string& type)
{
    const char* sql = "INSERT OR IGNORE INTO licenses (license_key, type) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void dbActivateLicense(sqlite3* db, const std::string& key,
                               const std::string& nom, const std::string& prenom,
                               const std::string& email, const std::string& mac,
                               const std::string& hwid)
{
    const char* sql = R"(
        UPDATE licenses SET status='activated', activated_at=datetime('now'),
            nom=?, prenom=?, email=?, mac_address=?, hwid=?
        WHERE license_key=? AND status='available'
    )";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, nom.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, prenom.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, mac.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, hwid.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, key.c_str(), -1, SQLITE_STATIC);
    int changes = 0;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (changes > 0) {
        // Log activation
        const char* logSql = "INSERT INTO activation_log (license_key, action, hwid, mac_address) VALUES (?, 'activate', ?, ?)";
        sqlite3_prepare_v2(db, logSql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hwid.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, mac.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        std::cout << "Licence activee pour " << prenom << " " << nom << " (" << email << ")" << std::endl;
    } else {
        std::cerr << "Erreur: cle non trouvee ou deja activee" << std::endl;
    }
}

static void dbRevokeLicense(sqlite3* db, const std::string& key)
{
    const char* sql = "UPDATE licenses SET status='revoked' WHERE license_key=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    const char* logSql = "INSERT INTO activation_log (license_key, action) VALUES (?, 'revoke')";
    sqlite3_prepare_v2(db, logSql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    std::cout << "Licence revoquee: " << key << std::endl;
}

static void dbListLicenses(sqlite3* db, const std::string& filter)
{
    std::string sql = "SELECT license_key, type, status, nom, prenom, email, mac_address, activated_at FROM licenses";
    if (filter == "available") sql += " WHERE status='available'";
    else if (filter == "activated") sql += " WHERE status='activated'";
    else if (filter == "revoked") sql += " WHERE status='revoked'";
    sql += " ORDER BY id DESC";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);

    std::cout << std::left
              << std::setw(25) << "CLE"
              << std::setw(14) << "TYPE"
              << std::setw(12) << "STATUS"
              << std::setw(20) << "NOM"
              << std::setw(25) << "EMAIL"
              << std::setw(20) << "MAC"
              << std::setw(20) << "ACTIVE LE"
              << std::endl;
    std::cout << std::string(136, '-') << std::endl;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto col = [&](int i) -> std::string {
            auto* t = sqlite3_column_text(stmt, i);
            return t ? reinterpret_cast<const char*>(t) : "";
        };
        std::cout << std::left
                  << std::setw(25) << col(0)
                  << std::setw(14) << col(1)
                  << std::setw(12) << col(2)
                  << std::setw(20) << (col(4) + " " + col(3))
                  << std::setw(25) << col(5)
                  << std::setw(20) << col(6)
                  << std::setw(20) << col(7)
                  << std::endl;
    }
    sqlite3_finalize(stmt);
}

static void dbStats(sqlite3* db)
{
    auto countQuery = [&](const std::string& where) -> int {
        std::string sql = "SELECT COUNT(*) FROM licenses" + (where.empty() ? "" : " WHERE " + where);
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    };
    std::cout << "=== STATISTIQUES LICENCES ===" << std::endl;
    std::cout << "  Total         : " << countQuery("") << std::endl;
    std::cout << "  Disponibles   : " << countQuery("status='available'") << std::endl;
    std::cout << "  Activees      : " << countQuery("status='activated'") << std::endl;
    std::cout << "  Revoquees     : " << countQuery("status='revoked'") << std::endl;
    std::cout << "  Personal      : " << countQuery("type LIKE 'Personal%'") << std::endl;
    std::cout << "  Professional  : " << countQuery("type LIKE 'Professional%'") << std::endl;
    std::cout << "  Premium       : " << countQuery("type LIKE 'Premium%'") << std::endl;
}

// ============================================================================
// Get Hardware ID (simplified for tool)
// ============================================================================
#ifdef _WIN32
#include <windows.h>
static std::string getHardwareId()
{
    // Simplified: use computer name + volume serial
    char compName[256];
    DWORD size = sizeof(compName);
    GetComputerNameA(compName, &size);

    DWORD volSerial = 0;
    GetVolumeInformationA("C:\\", nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0);

    std::ostringstream oss;
    oss << compName << "-" << std::hex << volSerial;
    return oss.str();
}
#else
static std::string getHardwareId() { return "unknown-platform"; }
#endif

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  BeatMate V11 - License Manager" << std::endl;
    std::cout << "  (c) 2026 Sebastien Sainte-Foi" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    if (argc < 2) {
        std::cout << "Usage:" << std::endl;
        std::cout << std::endl;
        std::cout << "  === GENERATION ===" << std::endl;
        std::cout << "  LicenseManager generate <type>                            Generer 1 cle" << std::endl;
        std::cout << "  LicenseManager batch <type> <count> <file>                Generer en lot" << std::endl;
        std::cout << std::endl;
        std::cout << "  === VALIDATION ===" << std::endl;
        std::cout << "  LicenseManager validate <key>                             Valider une cle" << std::endl;
        std::cout << "  LicenseManager info <key>                                 Infos d'une cle" << std::endl;
        std::cout << "  LicenseManager hwid                                       ID de cette machine" << std::endl;
        std::cout << std::endl;
        std::cout << "  === BASE DE DONNEES ===" << std::endl;
        std::cout << "  LicenseManager db-generate <type> <count>                 Generer et stocker en DB" << std::endl;
        std::cout << "  LicenseManager db-activate <key> <nom> <prenom> <email> <mac>  Activer pour un user" << std::endl;
        std::cout << "  LicenseManager db-revoke <key>                            Revoquer une cle" << std::endl;
        std::cout << "  LicenseManager db-list [all|available|activated|revoked]   Lister les cles" << std::endl;
        std::cout << "  LicenseManager db-stats                                   Statistiques" << std::endl;
        std::cout << std::endl;
        std::cout << "Types: Personal, Professional, Premium" << std::endl;
        std::cout << "DB   : licenses.db (SQLite, dans le dossier courant)" << std::endl;
        return 1;
    }

    std::string cmd = argv[1];

    // Seed RNG
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(seed));

    if (cmd == "generate" && argc >= 3) {
        std::string type = argv[2];
        std::string key = generateLicenseKey(type, rng);
        std::cout << "Type     : " << type << std::endl;
        std::cout << "Cle      : " << key << std::endl;
        std::cout << "Valide   : " << (validateKey(key) ? "OUI" : "NON") << std::endl;
        std::cout << "Info     : " << getTypeFromKey(key) << std::endl;
    }
    else if (cmd == "validate" && argc >= 3) {
        std::string key = argv[2];
        bool valid = validateKey(key);
        std::cout << "Cle      : " << key << std::endl;
        std::cout << "Format   : " << (valid ? "VALIDE" : "INVALIDE") << std::endl;
        if (valid)
            std::cout << "Type     : " << getTypeFromKey(key) << std::endl;
    }
    else if (cmd == "info" && argc >= 3) {
        std::string key = argv[2];
        std::cout << "Cle      : " << key << std::endl;
        std::cout << "Valide   : " << (validateKey(key) ? "OUI" : "NON") << std::endl;
        std::cout << "Type     : " << getTypeFromKey(key) << std::endl;
        std::cout << "Prefix   : " << key[0] << std::endl;
        std::cout << "Segments : " << key.substr(0, 5) << " / "
                  << key.substr(6, 5) << " / "
                  << key.substr(12, 5) << " / "
                  << key.substr(18) << std::endl;
    }
    else if (cmd == "batch" && argc >= 5) {
        std::string type = argv[2];
        int count = std::atoi(argv[3]);
        std::string outFile = argv[4];

        std::ofstream file(outFile);
        if (!file.is_open()) {
            std::cerr << "Erreur: impossible d'ouvrir " << outFile << std::endl;
            return 1;
        }

        file << "# BeatMate V11 License Keys" << std::endl;
        file << "# Type: " << type << std::endl;
        file << "# Generated: " << __DATE__ << " " << __TIME__ << std::endl;
        file << "# Count: " << count << std::endl;
        file << "#" << std::endl;

        int generated = 0;
        for (int i = 0; i < count; ++i) {
            std::string key = generateLicenseKey(type, rng);
            if (validateKey(key)) {
                file << key << std::endl;
                generated++;
            } else {
                --i; // retry
            }
        }

        file.close();
        std::cout << generated << " cles generees dans " << outFile << std::endl;
    }
    else if (cmd == "hwid") {
        std::cout << "Machine ID : " << getHardwareId() << std::endl;
    }
    // === DATABASE COMMANDS ===
    else if (cmd == "db-generate" && argc >= 4) {
        std::string type = argv[2];
        int count = std::atoi(argv[3]);
        auto* db = openLicenseDB();
        if (!db) return 1;

        int generated = 0;
        for (int i = 0; i < count; ++i) {
            std::string key = generateLicenseKey(type, rng);
            if (validateKey(key)) {
                dbAddLicense(db, key, type);
                generated++;
                if (count <= 10) std::cout << "  " << key << std::endl;
            } else {
                --i;
            }
        }
        sqlite3_close(db);
        std::cout << generated << " cles " << type << " generees et stockees dans licenses.db" << std::endl;
    }
    else if (cmd == "db-activate" && argc >= 7) {
        std::string key = argv[2];
        std::string nom = argv[3];
        std::string prenom = argv[4];
        std::string email = argv[5];
        std::string mac = argv[6];
        std::string hwid = (argc >= 8) ? argv[7] : getHardwareId();

        auto* db = openLicenseDB();
        if (!db) return 1;
        dbActivateLicense(db, key, nom, prenom, email, mac, hwid);
        sqlite3_close(db);
    }
    else if (cmd == "db-revoke" && argc >= 3) {
        std::string key = argv[2];
        auto* db = openLicenseDB();
        if (!db) return 1;
        dbRevokeLicense(db, key);
        sqlite3_close(db);
    }
    else if (cmd == "db-list") {
        std::string filter = (argc >= 3) ? argv[2] : "all";
        auto* db = openLicenseDB();
        if (!db) return 1;
        dbListLicenses(db, filter);
        sqlite3_close(db);
    }
    else if (cmd == "db-stats") {
        auto* db = openLicenseDB();
        if (!db) return 1;
        dbStats(db);
        sqlite3_close(db);
    }
    else {
        std::cerr << "Commande inconnue: " << cmd << std::endl;
        return 1;
    }

    return 0;
}
