#include "HardwareId.h"
#include <spdlog/spdlog.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <wincrypt.h>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "crypt32.lib")
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <cstdio>
#endif
namespace BeatMate::Services::Security {

namespace {

static std::string toUpperCopy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

#ifdef __APPLE__
static std::string ioPlatformString(CFStringRef key)
{
    std::string result;
    io_service_t service = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
    if (service) {
        if (CFTypeRef prop = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0)) {
            if (CFGetTypeID(prop) == CFStringGetTypeID()) {
                char buf[256] = {};
                if (CFStringGetCString(static_cast<CFStringRef>(prop), buf, sizeof(buf), kCFStringEncodingUTF8))
                    result = buf;
            }
            CFRelease(prop);
        }
        IOObjectRelease(service);
    }
    return result;
}

static std::string primaryMacAddress()
{
    std::string result;
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs* p = ifap; p != nullptr; p = p->ifa_next) {
            if (p->ifa_addr && p->ifa_addr->sa_family == AF_LINK
                && p->ifa_name && std::string(p->ifa_name) == "en0") {
                auto* sdl = reinterpret_cast<struct sockaddr_dl*>(p->ifa_addr);
                if (sdl->sdl_alen == 6) {
                    const unsigned char* m = reinterpret_cast<const unsigned char*>(LLADDR(sdl));
                    char buf[18];
                    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                                  m[0], m[1], m[2], m[3], m[4], m[5]);
                    result = buf;
                }
                break;
            }
        }
        freeifaddrs(ifap);
    }
    return result;
}

static std::filesystem::path macDataDir()
{
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home) : std::filesystem::path(".");
    auto dir = base / "Library" / "Application Support" / "BeatMate";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string readHwidCacheMac()
{
    auto path = macDataDir() / "hwid.txt";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::string id((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' '))
        id.pop_back();
    return id;
}

static void writeHwidCacheMac(const std::string& id)
{
    auto path = macDataDir() / "hwid.txt";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f.is_open())
        f.write(id.data(), static_cast<std::streamsize>(id.size()));
}
#endif

static bool containsCI(const std::string& haystack, const char* needle)
{
    auto H = toUpperCopy(haystack);
    std::string N = toUpperCopy(needle);
    return H.find(N) != std::string::npos;
}

#ifdef _WIN32
static std::string bstrToStd(BSTR b)
{
    if (!b) return {};
    return std::string(_bstr_t(b));
}

struct WmiSession {
    IWbemLocator*  loc = nullptr;
    IWbemServices* svc = nullptr;
    bool           comInit = false;
    bool           ok = false;

    WmiSession()
    {
        HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
        comInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                                    IID_IWbemLocator, (LPVOID*)&loc))) return;
        if (FAILED(loc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr,
                                      0, 0, 0, 0, &svc))) return;
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                          nullptr, EOAC_NONE);
        ok = true;
    }
    ~WmiSession()
    {
        if (svc) svc->Release();
        if (loc) loc->Release();
        if (comInit) CoUninitialize();
    }
};

static std::string querySingle(WmiSession& s, const std::string& cls, const std::string& prop)
{
    if (!s.ok) return "unknown";
    IEnumWbemClassObject* en = nullptr;
    std::string q = "SELECT " + prop + " FROM " + cls;
    if (FAILED(s.svc->ExecQuery(bstr_t("WQL"), bstr_t(q.c_str()),
                                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                nullptr, &en)) || !en)
        return "unknown";

    std::string result = "unknown";
    IWbemClassObject* obj = nullptr; ULONG ret = 0;
    if (en->Next(WBEM_INFINITE, 1, &obj, &ret) == S_OK && obj) {
        VARIANT v; VariantInit(&v);
        if (SUCCEEDED(obj->Get(_bstr_t(prop.c_str()), 0, &v, 0, 0)) && v.vt == VT_BSTR)
            result = bstrToStd(v.bstrVal);
        VariantClear(&v);
        obj->Release();
    }
    en->Release();
    return result;
}

struct Adapter {
    std::string name;
    std::string mac;
    int         index = 999999;
    bool        physical = false;
    bool        netEnabled = false;
};

static std::string queryStableMAC(WmiSession& s)
{
    if (!s.ok) return "unknown";

    IEnumWbemClassObject* en = nullptr;
    if (FAILED(s.svc->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT Name,MACAddress,Index,PhysicalAdapter,NetEnabled,PNPDeviceID "
                   "FROM Win32_NetworkAdapter WHERE MACAddress IS NOT NULL"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &en)) || !en)
        return "unknown";

    std::vector<Adapter> rows;
    rows.reserve(16);

    IWbemClassObject* obj = nullptr; ULONG ret = 0;
    while (en->Next(WBEM_INFINITE, 1, &obj, &ret) == S_OK && obj) {
        Adapter a;
        VARIANT v; VariantInit(&v);

        if (SUCCEEDED(obj->Get(L"Name", 0, &v, 0, 0)) && v.vt == VT_BSTR)
            a.name = bstrToStd(v.bstrVal);
        VariantClear(&v);

        if (SUCCEEDED(obj->Get(L"MACAddress", 0, &v, 0, 0)) && v.vt == VT_BSTR)
            a.mac = bstrToStd(v.bstrVal);
        VariantClear(&v);

        if (SUCCEEDED(obj->Get(L"Index", 0, &v, 0, 0))) {
            if (v.vt == VT_I4)        a.index = v.lVal;
            else if (v.vt == VT_UI4)  a.index = (int)v.ulVal;
            else if (v.vt == VT_BSTR) a.index = std::atoi(_bstr_t(v.bstrVal));
        }
        VariantClear(&v);

        if (SUCCEEDED(obj->Get(L"PhysicalAdapter", 0, &v, 0, 0)) && v.vt == VT_BOOL)
            a.physical = (v.boolVal == VARIANT_TRUE);
        VariantClear(&v);

        if (SUCCEEDED(obj->Get(L"NetEnabled", 0, &v, 0, 0)) && v.vt == VT_BOOL)
            a.netEnabled = (v.boolVal == VARIANT_TRUE);
        VariantClear(&v);

        std::string pnp;
        if (SUCCEEDED(obj->Get(L"PNPDeviceID", 0, &v, 0, 0)) && v.vt == VT_BSTR)
            pnp = bstrToStd(v.bstrVal);
        VariantClear(&v);

        obj->Release();
        obj = nullptr;

        if (a.mac.empty()) continue;

        static const char* kBadNeedles[] = {
            "VPN", "Bluetooth", "Virtual", "Loopback",
            "Microsoft Wi-Fi Direct", "Wi-Fi Direct",
            "VMware", "VirtualBox", "Hyper-V", "TAP-Windows",
            "WAN Miniport", "Pseudo", "Tunnel", "Tailscale",
            "ZeroTier", "Hamachi", "Npcap", "WireGuard"
        };
        bool bad = false;
        for (const char* n : kBadNeedles) {
            if (containsCI(a.name, n)) { bad = true; break; }
        }
        if (bad) continue;

        if (pnp.rfind("ROOT\\", 0) == 0) continue;

        rows.push_back(std::move(a));
    }
    en->Release();

    if (rows.empty()) return "unknown";

    std::sort(rows.begin(), rows.end(), [](const Adapter& a, const Adapter& b) {
        if (a.physical != b.physical) return a.physical;
        if (a.netEnabled != b.netEnabled) return a.netEnabled;
        return a.index < b.index;
    });

    return rows.front().mac;
}

static std::filesystem::path hwidCachePath()
{
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) appdata = ".";
    auto dir = std::filesystem::path(appdata) / "BeatMate";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / "hwid.bin";
}

static bool dpapiSeal(const std::string& plain, std::vector<uint8_t>& out)
{
    DATA_BLOB in{}, blob{};
    in.pbData = (BYTE*)plain.data();
    in.cbData = (DWORD)plain.size();
    if (!CryptProtectData(&in, L"BeatMateHWID", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &blob))
        return false;
    out.assign(blob.pbData, blob.pbData + blob.cbData);
    LocalFree(blob.pbData);
    return true;
}

static bool dpapiOpen(const std::vector<uint8_t>& sealed, std::string& out)
{
    DATA_BLOB in{}, blob{};
    in.pbData = (BYTE*)sealed.data();
    in.cbData = (DWORD)sealed.size();
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &blob))
        return false;
    out.assign((char*)blob.pbData, (char*)blob.pbData + blob.cbData);
    LocalFree(blob.pbData);
    return true;
}

struct CachedHwid {
    std::string id;       // 32-char SHA-256 prefix (the public HWID)
    std::string cpu, mb, disk, mac;
};

static bool parseCache(const std::string& blob, CachedHwid& c)
{
    if (blob.rfind("v1\n", 0) != 0) return false;
    std::vector<std::string> fields; fields.reserve(6);
    size_t p = 0;
    while (p <= blob.size()) {
        auto nl = blob.find('\n', p);
        if (nl == std::string::npos) { fields.push_back(blob.substr(p)); break; }
        fields.push_back(blob.substr(p, nl - p));
        p = nl + 1;
    }
    if (fields.size() < 6) return false;
    c.id   = fields[1];
    c.cpu  = fields[2];
    c.mb   = fields[3];
    c.disk = fields[4];
    c.mac  = fields[5];
    return !c.id.empty();
}

static std::string serializeCache(const CachedHwid& c)
{
    return "v1\n" + c.id + "\n" + c.cpu + "\n" + c.mb + "\n" + c.disk + "\n" + c.mac + "\n";
}

static bool readCache(CachedHwid& out)
{
    auto path = hwidCachePath();
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8_t> sealed((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
    if (sealed.empty()) return false;
    std::string plain;
    if (!dpapiOpen(sealed, plain)) return false;
    return parseCache(plain, out);
}

static void writeCache(const CachedHwid& c)
{
    std::vector<uint8_t> sealed;
    if (!dpapiSeal(serializeCache(c), sealed)) return;
    auto path = hwidCachePath();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f.is_open())
        f.write((const char*)sealed.data(), (std::streamsize)sealed.size());
}
#endif // _WIN32

static std::string sha256Hex32(const std::string& s)
{
    juce::SHA256 sha(s.data(), s.size());
    return sha.toHexString().toStdString().substr(0, 32);
}

} // namespace

std::string HardwareId::getHardwareId() {
#ifdef _WIN32
    WmiSession s;
    std::string cpu  = querySingle(s, "Win32_Processor",  "ProcessorId");
    std::string mb   = querySingle(s, "Win32_BaseBoard",  "SerialNumber");
    std::string disk = querySingle(s, "Win32_DiskDrive",  "SerialNumber");
    std::string mac  = queryStableMAC(s);

    std::string raw = cpu + mb + disk + mac;
    std::string fresh = sha256Hex32(raw);

    CachedHwid cache;
    if (readCache(cache) && !cache.id.empty()) {
        int match = 0;
        if (!cpu.empty()  && cpu  != "unknown" && cpu  == cache.cpu)  ++match;
        if (!mb.empty()   && mb   != "unknown" && mb   == cache.mb)   ++match;
        if (!disk.empty() && disk != "unknown" && disk == cache.disk) ++match;
        if (!mac.empty()  && mac  != "unknown" && mac  == cache.mac)  ++match;

        if (match >= 3) {
            spdlog::debug("HardwareId: cache hit ({}/4 sources match)", match);
            return cache.id;
        }
        spdlog::warn("HardwareId: cache invalidated ({}/4 sources match) — rotating", match);
    }

    CachedHwid out;
    out.id = fresh; out.cpu = cpu; out.mb = mb; out.disk = disk; out.mac = mac;
    writeCache(out);
    spdlog::debug("HardwareId: generated and cached new HWID");
    return fresh;
#elif defined(__APPLE__)
    std::string cached = readHwidCacheMac();
    if (!cached.empty()) {
        spdlog::debug("HardwareId: cache hit");
        return cached;
    }
    const std::string uuid   = ioPlatformString(CFSTR("IOPlatformUUID"));
    const std::string serial = ioPlatformString(CFSTR("IOPlatformSerialNumber"));
    const std::string mac    = primaryMacAddress();
    std::string raw = uuid + serial + mac;
    if (raw.empty()) raw = "unknown";
    std::string fresh = sha256Hex32(raw);
    writeHwidCacheMac(fresh);
    spdlog::debug("HardwareId: generated and cached new HWID");
    return fresh;
#else
    return sha256Hex32("unknown");
#endif
}

std::string HardwareId::getCPUID()        { return "unknown"; } // legacy stubs (kept for ABI)
std::string HardwareId::getMotherboardId(){ return "unknown"; }
std::string HardwareId::getDiskSerial()   { return "unknown"; }
std::string HardwareId::getMACAddress() {
#ifdef _WIN32
    WmiSession s;
    return queryStableMAC(s);
#elif defined(__APPLE__)
    std::string m = primaryMacAddress();
    return m.empty() ? "unknown" : m;
#else
    return "unknown";
#endif
}
std::string HardwareId::queryWMI(const std::string&, const std::string&) { return "unknown"; }

} // namespace BeatMate::Services::Security
