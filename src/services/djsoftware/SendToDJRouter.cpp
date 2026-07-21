#include "SendToDJRouter.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <spdlog/spdlog.h>

#include "virtualdj/VirtualDJRemote.h"
#include "traktor/TraktorNmlExporter.h"
#include "serato/SeratoTagWriter.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 // Ensure WIN32_LEAN_AND_MEAN (defined by spdlog/JUCE transitively) doesn't
 #ifdef WIN32_LEAN_AND_MEAN
  #undef WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shellapi.h>
 #include <shlobj.h>
 #include <tlhelp32.h>
 #include <ole2.h>
 #include <oleidl.h>
 #include <thread>
 #pragma comment(lib, "ole32.lib")
 #pragma comment(lib, "shell32.lib")
#endif

// Some Windows SDK headers (pulled by spdlog on Win32) define CreateDirectory
#ifdef CreateDirectory
#undef CreateDirectory
#endif
#ifdef CopyFile
#undef CopyFile
#endif


namespace BeatMate::Services::DJSoftware {

namespace {

std::string envVar(const char* name) {
    if (!name) return {};
#if defined(_WIN32)
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name) == 0 && buf) {
        std::string v(buf);
        free(buf);
        return v;
    }
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
#endif
}

// Build a juce::File without falling into the most-vexing-parse trap
inline juce::File makeFile(const std::string& s) {
    juce::String js;
    js = juce::String(s);
    return juce::File(js);
}

bool ensureDir(const std::string& p) {
    if (p.empty()) return false;
    juce::File f = makeFile(p);
    if (f.isDirectory()) return true;
    juce::Result res = f.createDirectory();
    return res.wasOk();
}

bool dirExists(const std::string& p) {
    if (p.empty()) return false;
    return makeFile(p).isDirectory();
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    juce::File base = makeFile(a);
    return base.getChildFile(juce::String(b)).getFullPathName().toStdString();
}

bool copyFileTo(const std::string& src, const std::string& dstDir) {
    if (src.empty() || dstDir.empty()) return false;
    juce::File s = makeFile(src);
    if (!s.existsAsFile()) return false;
    juce::File d = makeFile(dstDir);
    if (!d.isDirectory() && !d.createDirectory().wasOk()) return false;
    juce::File target = d.getChildFile(s.getFileName());
    return s.copyFileTo(target);
}


void writeBE32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void writeTag(std::vector<uint8_t>& buf, const char tag[5]) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(tag[i]));
}

std::vector<uint8_t> utf16BE(const std::string& utf8) {
    std::vector<uint8_t> out;
    juce::String s = juce::String::fromUTF8(utf8.c_str());
    auto it = s.getCharPointer();
    while (!it.isEmpty()) {
        juce::juce_wchar c = it.getAndAdvance();
        if (c == 0) break;
        if (c > 0xFFFF) c = '?'; // surrogate pairs not handled; rare in paths
        uint16_t u = static_cast<uint16_t>(c);
        out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(u & 0xFF));
    }
    return out;
}

void appendChunk(std::vector<uint8_t>& buf, const char tag[5], const std::vector<uint8_t>& payload) {
    writeTag(buf, tag);
    writeBE32(buf, static_cast<uint32_t>(payload.size()));
    buf.insert(buf.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> buildSeratoCrate(const std::vector<std::string>& relativePaths) {
    std::vector<uint8_t> buf;

    auto vrsnPayload = utf16BE("1.0/Serato ScratchLive Crate");
    appendChunk(buf, "vrsn", vrsnPayload);

    for (const auto& rel : relativePaths) {
        std::vector<uint8_t> otrk;
        auto ptrkPayload = utf16BE(rel);
        appendChunk(otrk, "ptrk", ptrkPayload);
        appendChunk(buf, "otrk", otrk);
    }

    return buf;
}

bool writeBinary(const std::string& path, const std::vector<uint8_t>& bytes) {
    juce::File f = makeFile(path);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    const void* ptr = bytes.empty() ? nullptr : static_cast<const void*>(bytes.data());
    return f.replaceWithData(ptr, bytes.size());
}

#if JUCE_WINDOWS
bool isProcessRunningWin(const std::wstring& exeNameLower) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring n = pe.szExeFile;
            for (auto& c : n) c = (wchar_t)towlower(c);
            if (n == exeNameLower) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

void bringAppToFrontWin(const std::wstring& exeNameLower) {
    struct EnumCtx { std::wstring target; HWND found = nullptr; };
    EnumCtx ctx{ exeNameLower, nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return TRUE;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc) return TRUE;
        wchar_t path[MAX_PATH] = {0};
        DWORD sz = MAX_PATH;
        BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &sz);
        CloseHandle(proc);
        if (!ok) return TRUE;
        std::wstring p = path;
        auto pos = p.find_last_of(L"\\/");
        std::wstring exe = (pos == std::wstring::npos) ? p : p.substr(pos+1);
        for (auto& ch : exe) ch = (wchar_t)towlower(ch);
        if (exe == c->target) { c->found = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.found) {
        if (IsIconic(ctx.found)) ShowWindow(ctx.found, SW_RESTORE);
        SetForegroundWindow(ctx.found);
    }
}

bool shellLaunchWin(const std::wstring& exeName, const std::wstring& args = L"") {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exeName.c_str(),
                                args.empty() ? nullptr : args.c_str(),
                                nullptr, SW_RESTORE);
    return reinterpret_cast<INT_PTR>(r) > 32;
}

// Simulate a real mouse-driven drag-drop onto the DJ app's window. Rekordbox,
bool setClipboardDropList(const std::vector<std::wstring>& filePaths,
                          const std::wstring& searchText = L"") {
    if (filePaths.empty()) return false;
    size_t totalChars = 1; // final \0
    for (const auto& p : filePaths) totalChars += p.size() + 1;
    const size_t bytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND, bytes);
    if (!hGlobal) return false;
    auto* df = reinterpret_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!df) { GlobalFree(hGlobal); return false; }
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(
        reinterpret_cast<char*>(df) + sizeof(DROPFILES));
    for (const auto& p : filePaths) {
        std::memcpy(dst, p.c_str(), p.size() * sizeof(wchar_t));
        dst += p.size();
        *dst++ = L'\0';
    }
    *dst = L'\0';
    GlobalUnlock(hGlobal);

    // CF_UNICODETEXT: prefer the caller-supplied search string (typically a
    HGLOBAL hText = nullptr;
    {
        const std::wstring& src = searchText.empty() ? filePaths.front()
                                                      : searchText;
        const size_t tbytes = (src.size() + 1) * sizeof(wchar_t);
        hText = GlobalAlloc(GHND, tbytes);
        if (hText) {
            auto* p = reinterpret_cast<wchar_t*>(GlobalLock(hText));
            if (p) { std::memcpy(p, src.c_str(), tbytes); GlobalUnlock(hText); }
        }
    }

    if (!OpenClipboard(nullptr)) {
        GlobalFree(hGlobal);
        if (hText) GlobalFree(hText);
        return false;
    }
    EmptyClipboard();
    bool dropOk = (SetClipboardData(CF_HDROP, hGlobal) != nullptr);
    if (!dropOk) GlobalFree(hGlobal);
    if (hText) {
        if (!SetClipboardData(CF_UNICODETEXT, hText)) GlobalFree(hText);
    }
    // Also set a preferred drop effect = COPY so Rekordbox interprets the
    HGLOBAL hEffect = GlobalAlloc(GHND, sizeof(DWORD));
    if (hEffect) {
        auto* p = reinterpret_cast<DWORD*>(GlobalLock(hEffect));
        if (p) { *p = DROPEFFECT_COPY; GlobalUnlock(hEffect); }
        UINT cfEffect = RegisterClipboardFormatW(L"Preferred DropEffect");
        if (cfEffect) {
            if (!SetClipboardData(cfEffect, hEffect)) GlobalFree(hEffect);
        } else {
            GlobalFree(hEffect);
        }
    }
    CloseClipboard();
    return dropOk;
}

bool sendFileDropToWindow(HWND target, const std::wstring& filePath) {
    if (!target || !IsWindow(target) || filePath.empty()) return false;

    // Strategy 1: put CF_HDROP on the clipboard so Ctrl+V works ("load" in
    setClipboardDropList({ filePath });

    // Strategy 2: PostMessage WM_DROPFILES. Rekordbox 6/7 ignores this
    const size_t pathBytes = (filePath.size() + 2) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(DROPFILES) + pathBytes);
    if (!hGlobal) return true; // clipboard path already succeeded above
    auto* df = reinterpret_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!df) { GlobalFree(hGlobal); return true; }
    df->pFiles = sizeof(DROPFILES);
    // Place the drop point at the window's centre — some apps route the drop
    RECT rc{};
    if (GetWindowRect(target, &rc)) {
        df->pt.x = (rc.left + rc.right) / 2;
        df->pt.y = (rc.top  + rc.bottom) / 2;
    } else {
        df->pt.x = 200;
        df->pt.y = 200;
    }
    df->fNC   = FALSE;
    df->fWide = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(
        reinterpret_cast<char*>(df) + sizeof(DROPFILES));
    std::memcpy(dst, filePath.c_str(), filePath.size() * sizeof(wchar_t));
    dst[filePath.size()]     = L'\0';
    dst[filePath.size() + 1] = L'\0';
    GlobalUnlock(hGlobal);

    if (!PostMessageW(target, WM_DROPFILES, reinterpret_cast<WPARAM>(hGlobal), 0)) {
        GlobalFree(hGlobal);
    }
    return true;
}

// Programmatic OLE drag & drop. Modern DJ apps (Serato, Traktor, Engine DJ,

static HGLOBAL buildHDropGlobal(const std::vector<std::wstring>& files) {
    size_t charCount = 0;
    for (const auto& f : files) charCount += f.size() + 1;
    charCount += 1;
    HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(DROPFILES) + charCount * sizeof(wchar_t));
    if (!hGlobal) return nullptr;
    auto* df = reinterpret_cast<DROPFILES*>(GlobalLock(hGlobal));
    if (!df) { GlobalFree(hGlobal); return nullptr; }
    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(df) + sizeof(DROPFILES));
    for (const auto& f : files) {
        std::memcpy(dst, f.c_str(), f.size() * sizeof(wchar_t));
        dst += f.size();
        *dst++ = L'\0';
    }
    *dst = L'\0';
    GlobalUnlock(hGlobal);
    return hGlobal;
}

class SyntheticDropSource : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = (ULONG) InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    LONG ref_ = 1;
};

class HDropDataObject : public IDataObject {
public:
    explicit HDropDataObject(HGLOBAL hDrop) : hDrop_(hDrop) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG) InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = (ULONG) InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fe, STGMEDIUM* sm) override {
        if (!fe || !sm) return E_INVALIDARG;
        if (fe->cfFormat != CF_HDROP || !(fe->tymed & TYMED_HGLOBAL)) return DV_E_FORMATETC;
        const SIZE_T sz = GlobalSize(hDrop_);
        HGLOBAL dup = GlobalAlloc(GHND, sz);
        if (!dup) return E_OUTOFMEMORY;
        void* src = GlobalLock(hDrop_);
        void* dst = GlobalLock(dup);
        if (src && dst) std::memcpy(dst, src, sz);
        if (dst) GlobalUnlock(dup);
        if (src) GlobalUnlock(hDrop_);
        sm->tymed = TYMED_HGLOBAL;
        sm->hGlobal = dup;
        sm->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fe) override {
        if (!fe) return E_INVALIDARG;
        return (fe->cfFormat == CF_HDROP && (fe->tymed & TYMED_HGLOBAL)) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppenum) override {
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fmt{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return SHCreateStdEnumFmtEtc(1, &fmt, ppenum);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    LONG ref_ = 1;
    HGLOBAL hDrop_;
};

bool oleDragDropToWindow(HWND target, const std::vector<std::wstring>& files) {
    if (!target || !IsWindow(target) || files.empty()) return false;

    RECT rc{};
    if (!GetWindowRect(target, &rc)) return false;
    const POINT dropPt{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };

    HGLOBAL hDrop = buildHDropGlobal(files);
    if (!hDrop) return false;

    const HRESULT oleInit = OleInitialize(nullptr);

    auto* dataObj = new HDropDataObject(hDrop);
    auto* dropSrc = new SyntheticDropSource();

    POINT oldPos{};
    GetCursorPos(&oldPos);
    SetCursorPos(dropPt.x, dropPt.y);

    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &down, sizeof(INPUT));

    std::thread releaser([dropPt]() {
        Sleep(150);
        SetCursorPos(dropPt.x + 2, dropPt.y + 2);
        INPUT mv{};
        mv.type = INPUT_MOUSE;
        mv.mi.dwFlags = MOUSEEVENTF_MOVE;
        mv.mi.dx = 1;
        mv.mi.dy = 1;
        SendInput(1, &mv, sizeof(INPUT));
        Sleep(150);
        INPUT up{};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &up, sizeof(INPUT));
    });

    DWORD effect = 0;
    const HRESULT hr = DoDragDrop(dataObj, dropSrc, DROPEFFECT_COPY | DROPEFFECT_LINK | DROPEFFECT_MOVE, &effect);

    if (releaser.joinable()) releaser.join();

    dropSrc->Release();
    dataObj->Release();
    SetCursorPos(oldPos.x, oldPos.y);
    if (oleInit == S_OK) OleUninitialize();

    const bool dropped = (hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE);
    spdlog::info("[SendToDJRouter] OLE DoDragDrop -> hr=0x{:08x} effect={} {}",
                 (unsigned) hr, (unsigned) effect, dropped ? "DROPPED" : "not accepted");
    return dropped;
}

HWND findMainWindowForExe(const std::wstring& exeNameLower) {
    struct EnumCtx { std::wstring target; HWND found = nullptr; };
    EnumCtx ctx{ exeNameLower, nullptr };
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return TRUE;
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc) return TRUE;
        wchar_t path[MAX_PATH] = {0};
        DWORD sz = MAX_PATH;
        BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &sz);
        CloseHandle(proc);
        if (!ok) return TRUE;
        std::wstring p = path;
        auto pos = p.find_last_of(L"\\/");
        std::wstring exe = (pos == std::wstring::npos) ? p : p.substr(pos + 1);
        for (auto& ch : exe) ch = (wchar_t) towlower(ch);
        if (exe == c->target) { c->found = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

std::wstring findRunningExePath(const std::wstring& exeNameLower) {
    std::wstring result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = pe.szExeFile;
            for (auto& c : name) c = (wchar_t) towlower(c);
            if (name != exeNameLower) continue;
            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                      FALSE, pe.th32ProcessID);
            if (!proc) continue;
            wchar_t path[MAX_PATH] = {0};
            DWORD sz = MAX_PATH;
            if (QueryFullProcessImageNameW(proc, 0, path, &sz)) {
                result.assign(path);
            }
            CloseHandle(proc);
            if (!result.empty()) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

void sendKeyComboWin(const std::vector<WORD>& mods, WORD vk) {
    std::vector<INPUT> inputs;
    inputs.reserve(mods.size() * 2 + 2);
    for (WORD m : mods) {
        INPUT ip{}; ip.type = INPUT_KEYBOARD; ip.ki.wVk = m; ip.ki.dwFlags = 0;
        inputs.push_back(ip);
    }
    {
        INPUT ip{}; ip.type = INPUT_KEYBOARD; ip.ki.wVk = vk; ip.ki.dwFlags = 0;
        inputs.push_back(ip);
    }
    {
        INPUT ip{}; ip.type = INPUT_KEYBOARD; ip.ki.wVk = vk; ip.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(ip);
    }
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        INPUT ip{}; ip.type = INPUT_KEYBOARD; ip.ki.wVk = *it; ip.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(ip);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void autoPasteIntoForegroundAfter(int delayMs, bool openSearchFirst) {
    std::thread([delayMs, openSearchFirst]() {
        Sleep(delayMs);
        if (openSearchFirst) {
            // Ctrl+L is Rekordbox's bound shortcut for Library > Search focus
            sendKeyComboWin({ VK_CONTROL }, 'L');
            Sleep(120);
        }
        sendKeyComboWin({ VK_CONTROL }, 'V');
        // Retry once: some apps absorb the first event when they're busy
        Sleep(200);
        sendKeyComboWin({ VK_CONTROL }, 'V');
    }).detach();
}

// Scancode-based keystroke: survives non-QWERTY layouts (AZERTY, QWERTZ…)
void sendKeyComboScan(const std::vector<WORD>& mods, WORD vk) {
    auto mk = [](WORD v, bool keyUp) {
        INPUT ip{};
        ip.type = INPUT_KEYBOARD;
        UINT sc = MapVirtualKeyW(v, MAPVK_VK_TO_VSC);
        ip.ki.wVk   = v;
        ip.ki.wScan = (WORD) sc;
        ip.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        // Extended-key set for navigation keys (Left/Right/Up/Down/Home/End/...)
        switch (v) {
            case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
            case VK_HOME: case VK_END:  case VK_PRIOR: case VK_NEXT:
            case VK_INSERT: case VK_DELETE:
                ip.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
                break;
            default: break;
        }
        return ip;
    };
    std::vector<INPUT> inputs;
    inputs.reserve(mods.size() * 2 + 2);
    for (WORD m : mods) inputs.push_back(mk(m, false));
    inputs.push_back(mk(vk, false));
    inputs.push_back(mk(vk, true));
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) inputs.push_back(mk(*it, true));
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

HWND waitForFileOpenDialog(int timeoutMs) {
    const int tick = 50;
    for (int waited = 0; waited < timeoutMs; waited += tick) {
        HWND h = FindWindowW(L"#32770", nullptr);
        if (h && IsWindowVisible(h)) {
            // Only accept dialogs with the "Edit" child that hosts the filename
            if (FindWindowExW(h, nullptr, L"Edit", nullptr)) return h;
        }
        Sleep(tick);
    }
    return nullptr;
}

// Rekordbox-specific: trigger File > Import Track (Ctrl+O), which opens the
// reference (https://cdn.rekordbox.com/files/20241203185020/...).
void rekordboxImportTrackAfter(int delayMs, const std::wstring& fullPath) {
    std::thread([delayMs, fullPath]() {
        Sleep(delayMs);
        sendKeyComboScan({ VK_CONTROL }, 'O');
        HWND dlg = waitForFileOpenDialog(1500);
        if (!dlg) {
            spdlog::warn("[Rekordbox::ImportTrack] FileOpenDialog did not appear "
                         "within 1.5 s — Ctrl+O may be bound to something else "
                         "or focus was stolen. Clipboard still contains the path.");
            return;
        }
        // 3. The FileOpenDialog filename edit needs the FULL FILE PATH, not the
        bool typed = false;
        if (!fullPath.empty()) {
            HWND edit = FindWindowExW(dlg, nullptr, L"Edit", nullptr);
            if (!edit) {
                HWND combo = FindWindowExW(dlg, nullptr, L"ComboBoxEx32", nullptr);
                if (combo) {
                    HWND inner = FindWindowExW(combo, nullptr, L"ComboBox", nullptr);
                    if (inner) edit = FindWindowExW(inner, nullptr, L"Edit", nullptr);
                }
            }
            if (edit) {
                SetWindowTextW(edit, fullPath.c_str());
                typed = true;
            }
        }
        if (!typed) {
            // Fallback: drop the full path into CF_UNICODETEXT then Ctrl+V.
            if (!fullPath.empty() && OpenClipboard(nullptr)) {
                EmptyClipboard();
                const size_t tbytes = (fullPath.size() + 1) * sizeof(wchar_t);
                HGLOBAL h = GlobalAlloc(GHND, tbytes);
                if (h) {
                    auto* p = reinterpret_cast<wchar_t*>(GlobalLock(h));
                    if (p) { std::memcpy(p, fullPath.c_str(), tbytes); GlobalUnlock(h); }
                    if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
                }
                CloseClipboard();
            }
            sendKeyComboScan({ VK_CONTROL }, 'V');
        }
        Sleep(180);
        sendKeyComboScan({}, VK_RETURN);
        // 5. Opportunistic deck load 400 ms later.
        Sleep(400);
        sendKeyComboScan({ VK_SHIFT }, VK_LEFT);
        spdlog::info("[Rekordbox::ImportTrack] Ctrl+O sequence completed "
                     "(dialog was detected, file pasted, Enter sent, Shift+Left sent).");
    }).detach();
}
#endif // JUCE_WINDOWS

bool launchAndFront(const char* exeName, const std::string& argPath = {}) {
#if JUCE_WINDOWS
    std::wstring wexe; wexe.assign(exeName, exeName + std::strlen(exeName));
    std::wstring lower = wexe;
    for (auto& c : lower) c = (wchar_t)towlower(c);

    if (isProcessRunningWin(lower)) {
        bringAppToFrontWin(lower);
        return true;
    }
    std::wstring wargs;
    if (!argPath.empty()) {
        juce::String s = juce::String("\"") + juce::String(argPath) + juce::String("\"");
        auto utf16 = s.toUTF16();
        wargs.assign(utf16, utf16 + s.length());
    }
    return shellLaunchWin(wexe, wargs);
#else
    (void)exeName; (void)argPath;
    return false;
#endif
}

std::string findTraktorProfile(const std::string& documents) {
    std::string empty = "";
    if (documents.empty()) return empty;
    juce::File ni = makeFile(documents).getChildFile("Native Instruments");
    if (!ni.isDirectory()) return empty;
    auto children = ni.findChildFiles((int)juce::File::findDirectories, false, "Traktor*");
    if (children.isEmpty()) return empty;
    juce::File best = children[0];
    for (int i = 1; i < children.size(); ++i) {
        if (children[i].getLastModificationTime() > best.getLastModificationTime())
            best = children[i];
    }
    return best.getFullPathName().toStdString();
}

} // namespace


SendToDJRouter::SendToDJRouter()
    : manager_(std::make_unique<DJSoftwareManager>()),
      preference_{ DJTarget::Rekordbox, DJTarget::Serato, DJTarget::Traktor,
                   DJTarget::VirtualDJ, DJTarget::EngineDJ } {}

SendToDJRouter::~SendToDJRouter() = default;

const char* SendToDJRouter::targetLabel(DJTarget t) {
    switch (t) {
        case DJTarget::VirtualDJ: return "VirtualDJ";
        case DJTarget::Rekordbox: return "Rekordbox";
        case DJTarget::Serato:    return "Serato DJ";
        case DJTarget::Traktor:   return "Traktor";
        case DJTarget::EngineDJ:  return "Engine DJ";
        case DJTarget::Auto:      return "Auto";
    }
    return "Unknown";
}

void SendToDJRouter::setPreferenceOrder(std::vector<DJTarget> order) {
    if (order.empty()) return;
    preference_ = std::move(order);
}

static DJSoftwareType toDJSoftwareType(DJTarget t);

namespace {
std::vector<std::wstring> processNamesFor(DJTarget t) {
    switch (t) {
        case DJTarget::VirtualDJ: return { L"virtualdj.exe", L"virtualdj_pro.exe",
                                           L"virtualdj pro.exe" };
        case DJTarget::Rekordbox: return { L"rekordbox.exe" };
        case DJTarget::Serato:    return { L"serato dj pro.exe", L"seratodj.exe",
                                           L"serato_dj.exe", L"serato dj.exe" };
        case DJTarget::Traktor:   return { L"traktor.exe" };
        case DJTarget::EngineDJ:  return { L"engine dj.exe", L"enginedj.exe" };
        default:                  return {};
    }
}
} // namespace

bool SendToDJRouter::isInstalled(DJTarget t) const {
    if (!manager_) return false;
    const auto dst = toDJSoftwareType(t);
    const auto infos = manager_->getDetectedSoftware();
    for (const auto& i : infos) {
        if (i.type == dst && i.isInstalled) return true;
    }
    // Fallback: directory probe (same logic as availableTargets())
    auto userProf = envVar("USERPROFILE");
    auto appdata  = envVar("APPDATA");
    auto docs     = userProf.empty() ? std::string{} : joinPath(userProf, "Documents");
    auto music    = userProf.empty() ? std::string{} : joinPath(userProf, "Music");
    switch (t) {
        case DJTarget::Rekordbox: return !appdata.empty() && dirExists(joinPath(appdata, "Pioneer\\rekordbox"));
        case DJTarget::Serato:    return !music.empty()   && dirExists(joinPath(music, "_Serato_"));
        case DJTarget::Traktor:   return !docs.empty()    && !findTraktorProfile(docs).empty();
        case DJTarget::VirtualDJ: return !docs.empty()    && dirExists(joinPath(docs, "VirtualDJ"));
        case DJTarget::EngineDJ:  return !music.empty()   && dirExists(joinPath(music, "Engine Library"));
        default: return false;
    }
}

bool SendToDJRouter::isRunning(DJTarget t) const {
#ifdef _WIN32
    for (const auto& name : processNamesFor(t)) {
        if (isProcessRunningWin(name)) return true;
    }
#else
    (void)t;
#endif
    // Last resort: DJSoftwareManager may have a richer probe (e.g. VDJ remote)
    if (manager_) {
        const auto dst = toDJSoftwareType(t);
        for (const auto& i : manager_->getDetectedSoftware()) {
            if (i.type == dst && i.isRunning) return true;
        }
    }
    return false;
}

std::map<DJTarget, std::string> SendToDJRouter::pingTargets() const {
    std::map<DJTarget, std::string> out;
    for (auto t : { DJTarget::Rekordbox, DJTarget::Serato, DJTarget::Traktor,
                    DJTarget::VirtualDJ, DJTarget::EngineDJ }) {
        if (isRunning(t))        out[t] = "OK (running)";
        else if (isInstalled(t)) out[t] = "installed but not running";
        else                     out[t] = "not installed";
    }
    return out;
}

std::string SendToDJRouter::userProfilePath()    { return envVar("USERPROFILE"); }
std::string SendToDJRouter::appDataRoamingPath() { return envVar("APPDATA"); }

std::string SendToDJRouter::documentsPath() {
    auto p = userProfilePath();
    if (p.empty()) return {};
    return joinPath(p, "Documents");
}

std::string SendToDJRouter::musicPath() {
    auto p = userProfilePath();
    if (p.empty()) return {};
    return joinPath(p, "Music");
}


static DJSoftwareType toDJSoftwareType(DJTarget t) {
    switch (t) {
        case DJTarget::VirtualDJ: return DJSoftwareType::VirtualDJ;
        case DJTarget::Rekordbox: return DJSoftwareType::Rekordbox;
        case DJTarget::Serato:    return DJSoftwareType::Serato;
        case DJTarget::Traktor:   return DJSoftwareType::Traktor;
        case DJTarget::EngineDJ:  return DJSoftwareType::EngineDJ;
        default:                  return DJSoftwareType::VirtualDJ;
    }
}

static DJTarget toDJTarget(DJSoftwareType t) {
    switch (t) {
        case DJSoftwareType::VirtualDJ: return DJTarget::VirtualDJ;
        case DJSoftwareType::Rekordbox: return DJTarget::Rekordbox;
        case DJSoftwareType::Serato:    return DJTarget::Serato;
        case DJSoftwareType::Traktor:   return DJTarget::Traktor;
        case DJSoftwareType::EngineDJ:  return DJTarget::EngineDJ;
        default:                        return DJTarget::Auto;
    }
}

std::vector<DJTarget> SendToDJRouter::availableTargets() const {
    std::vector<DJTarget> out;
    if (!manager_) {
        // Always return all known targets so the menu is never empty.
        return { DJTarget::Rekordbox, DJTarget::Serato, DJTarget::Traktor,
                 DJTarget::VirtualDJ, DJTarget::EngineDJ };
    }

    const auto infos = manager_->getDetectedSoftware();
    auto has = [&](DJTarget t) {
        auto dst = toDJSoftwareType(t);
        for (const auto& i : infos) {
            if (i.type == dst && (i.isInstalled || i.isRunning)) return true;
        }
        return false;
    };

    auto installDirExists = [](DJTarget t) {
        auto userProf = envVar("USERPROFILE");
        auto appdata  = envVar("APPDATA");
        auto docs     = userProf.empty() ? std::string{} : joinPath(userProf, "Documents");
        auto music    = userProf.empty() ? std::string{} : joinPath(userProf, "Music");
        switch (t) {
            case DJTarget::Rekordbox: return !appdata.empty() && dirExists(joinPath(appdata, "Pioneer\\rekordbox"));
            case DJTarget::Serato:    return !music.empty()   && dirExists(joinPath(music, "_Serato_"));
            case DJTarget::Traktor:   return !docs.empty()    && !findTraktorProfile(docs).empty();
            case DJTarget::VirtualDJ: return !docs.empty()    && dirExists(joinPath(docs, "VirtualDJ"));
            case DJTarget::EngineDJ:  return !music.empty()   && dirExists(joinPath(music, "Engine Library"));
            default: return false;
        }
    };

    for (auto t : { DJTarget::Rekordbox, DJTarget::Serato, DJTarget::Traktor,
                    DJTarget::VirtualDJ, DJTarget::EngineDJ }) {
        if (has(t) || installDirExists(t)) out.push_back(t);
    }

    // Still empty? fall back to full list so the menu stays usable.
    if (out.empty()) {
        out = { DJTarget::Rekordbox, DJTarget::Serato, DJTarget::Traktor,
                DJTarget::VirtualDJ, DJTarget::EngineDJ };
    }
    return out;
}

DJTarget SendToDJRouter::resolveAuto() const {
    if (!manager_) return DJTarget::VirtualDJ;
    const auto infos = manager_->getDetectedSoftware();

    for (auto pref : preference_) {
        auto dst = toDJSoftwareType(pref);
        for (const auto& i : infos) {
            if (i.type == dst && i.isRunning) return pref;
        }
    }
    for (auto pref : preference_) {
        auto dst = toDJSoftwareType(pref);
        for (const auto& i : infos) {
            if (i.type == dst && i.isInstalled) return pref;
        }
    }
    for (const auto& i : infos) {
        if (i.isInstalled) return toDJTarget(i.type);
    }
    return DJTarget::VirtualDJ;
}


namespace {
std::string stampClipboard(const Models::Track& track) {
    // User request: the CF_UNICODETEXT clipboard must carry a search-friendly
    std::string payload;
    const std::string& artist = track.artist;
    const std::string& title  = track.title;
    const bool artistIsGarbage =
        artist.empty()
        || artist == "UnknownArtist" || artist == "Unknown Artist"
        || artist == "Unknown artist";
    const bool titleIsGarbage  = title.empty();

    if (!titleIsGarbage && !artistIsGarbage) {
        payload = title;
    } else if (!titleIsGarbage) {
        payload = title;
    } else if (!artistIsGarbage) {
        payload = artist;
    } else if (!track.filePath.empty()) {
        juce::File f{juce::String::fromUTF8(track.filePath.c_str())};
        payload = f.getFileNameWithoutExtension().toStdString();
    }

    if (!payload.empty()) {
        juce::SystemClipboard::copyTextToClipboard(juce::String::fromUTF8(payload.c_str()));
        spdlog::info("[SendToDJRouter] Clipboard TEXT set: '{}'", payload);
    }
    return payload;
}

void setClipboard(const std::string& s) {
    if (s.empty()) return;
    juce::SystemClipboard::copyTextToClipboard(juce::String(s));
    spdlog::info("[SendToDJRouter] Clipboard set: '{}'", s);
}
} // namespace

SendResult SendToDJRouter::sendTrackAutoDeck(const Models::Track& track) {
    DJTarget tgt = resolveAuto();

    // Pick the free deck (A/B). For VirtualDJ we can query the HTTP remote to
    DeckSlot deck = DeckSlot::DeckA;
    if (tgt == DJTarget::VirtualDJ) {
        try {
            VirtualDJ::VirtualDJRemote rem;
            if (rem.connectAuto("127.0.0.1")) {
                const auto a = rem.getCurrentTrack(1);
                const auto b = rem.getCurrentTrack(2);
                if (a.isPlaying && !b.isPlaying)      deck = DeckSlot::DeckB;
                else if (!a.isPlaying && b.isPlaying) deck = DeckSlot::DeckA;
            }
        } catch (...) {}
    }
    return sendTrack(track, tgt, deck);
}

SendResult SendToDJRouter::sendTrack(const Models::Track& track, DJTarget target, DeckSlot deck) {
    spdlog::info("[SendToDJRouter::sendTrack] ENTRY target={} deck={} path='{}' artist='{}' title='{}'",
                 targetLabel(target), static_cast<int>(deck),
                 track.filePath, track.artist, track.title);

    if (track.filePath.empty() && track.title.empty() && track.artist.empty()) {
        spdlog::warn("[SendToDJRouter::sendTrack] empty track - aborting");
        return { false, target, "Piste vide" };
    }

    const bool hasLocal = !track.filePath.empty() &&
                          juce::File(juce::String(track.filePath)).existsAsFile();

    const DJTarget originalTarget = target;
    if (target == DJTarget::Auto) {
        target = resolveAuto();
        spdlog::info("[SendToDJRouter::sendTrack] Auto resolved -> {}", targetLabel(target));
    }

    const std::string clipboardPayload = stampClipboard(track);

    SendResult r;
    if (!hasLocal) {
        spdlog::info("[SendToDJRouter::sendTrack] no local file -> clipboard hint path");
        // Streaming/HOT 100 path : no file to copy. We copy "Artist - Title"
        r.target = target;
        auto hint = clipboardPayload; // already in clipboard (see stampClipboard above)
#if JUCE_WINDOWS
        const wchar_t* exe = nullptr;
        switch (target) {
            case DJTarget::Rekordbox: exe = L"rekordbox.exe";      break;
            case DJTarget::VirtualDJ: exe = L"virtualdj.exe";      break;
            case DJTarget::Serato:    exe = L"serato dj pro.exe";  break;
            case DJTarget::Traktor:   exe = L"traktor.exe";        break;
            case DJTarget::EngineDJ:  exe = L"engine dj.exe";      break;
            default:                  exe = nullptr;               break;
        }
        bool broughtToFront = false;
        if (exe && isProcessRunningWin(exe)) {
            bringAppToFrontWin(exe);
            broughtToFront = true;
        }
#else
        bool broughtToFront = false;
#endif
        if (hint.empty()) {
            r.ok = false;
            r.message = "Piste streaming sans metadonnees exploitables.";
        } else {
            // Flag as success since clipboard+focus is a genuine action for
            r.ok = true;
            r.message = std::string("Piste streaming (pas de fichier local). '") +
                        hint + "' copie dans le presse-papier. Colle (Ctrl+V) dans la barre de recherche ";
            r.message += targetLabel(target);
            if (broughtToFront) r.message += " (fenetre mise au premier plan)";
            else                r.message += " (l'appli n'est pas lancee)";
            r.message += ".";
        }
        return r;
    }

    spdlog::info("[SendToDJRouter::sendTrack] dispatching to {}", targetLabel(target));
    switch (target) {
        case DJTarget::VirtualDJ: r = sendToVirtualDJ(track, deck); break;
        case DJTarget::Rekordbox: r = sendToRekordbox({ track });   break;
        case DJTarget::Serato:    r = sendToSerato({ track });      break;
        case DJTarget::Traktor:   r = sendToTraktor({ track });     break;
        case DJTarget::EngineDJ:  r = sendToEngineDJ({ track });    break;
        case DJTarget::Auto:      r = { false, target, "Aucune appli DJ detectee" }; break;
    }

    // Clipboard already holds the file path (see stampClipboard at entry). We
    if (r.ok && !clipboardPayload.empty()) {
        r.message += "  (chemin dans presse-papier : Ctrl+V sur le deck pour charger)";
    }
    spdlog::info("[SendToDJRouter::sendTrack] DONE originalTarget={} resolvedTarget={} ok={} msg='{}'",
                 targetLabel(originalTarget), targetLabel(r.target), r.ok, r.message);
    return r;
}

SendResult SendToDJRouter::sendTracks(const std::vector<Models::Track>& tracks, DJTarget target) {
    if (tracks.empty()) return { false, target, "Aucun morceau" };

    if (target == DJTarget::Auto) target = resolveAuto();

    SendResult r;
    switch (target) {
        case DJTarget::VirtualDJ: {
            // VirtualDJ remote can only drop one track per deck; load first on Deck A
            r = sendToVirtualDJ(tracks.front(), DeckSlot::DeckA);
            if (r.ok && tracks.size() > 1) {
                r.message += " (+ " + std::to_string(tracks.size() - 1) + " en file d'attente Sideview)";
                auto sideview = joinPath(documentsPath(), "VirtualDJ/Sideview");
                ensureDir(sideview);
                for (size_t i = 1; i < tracks.size(); ++i) {
                    copyFileTo(tracks[i].filePath, sideview);
                }
            }
            break;
        }
        case DJTarget::Rekordbox: r = sendToRekordbox(tracks); break;
        case DJTarget::Serato:    r = sendToSerato(tracks);    break;
        case DJTarget::Traktor:   r = sendToTraktor(tracks);   break;
        case DJTarget::EngineDJ:  r = sendToEngineDJ(tracks);  break;
        case DJTarget::Auto:      r = { false, target, "Aucune appli DJ detectee" }; break;
    }

    auto hint = stampClipboard(tracks.front());
    if (!hint.empty()) {
        r.message += " (chemin dans le presse-papier)";
    }
    return r;
}

SendResult SendToDJRouter::sendToVirtualDJ(const Models::Track& track, DeckSlot deck) {
    VirtualDJ::VirtualDJRemote remote;
    if (remote.connectAuto("127.0.0.1")) {
        int deckNum = static_cast<int>(deck) + 1;
        if (remote.loadTrack(deckNum, track.filePath)) {
            return { true, DJTarget::VirtualDJ,
                     std::string("Charge dans VirtualDJ deck ") +
                     static_cast<char>('A' + static_cast<int>(deck)) };
        }
    }

    std::vector<std::string> roots;
    if (auto la = envVar("LOCALAPPDATA"); !la.empty()) {
        roots.push_back(joinPath(la, "VirtualDJ"));
    }
    if (auto ad = envVar("APPDATA"); !ad.empty()) {
        roots.push_back(joinPath(ad, "VirtualDJ"));
    }
    auto docs = documentsPath();
    if (!docs.empty()) roots.push_back(joinPath(docs, "VirtualDJ"));

    for (const auto& root : roots) {
        if (!dirExists(root)) continue;
        auto playlistsDir = joinPath(root, "Playlists");
        ensureDir(playlistsDir);
        auto m3u = joinPath(playlistsDir, "BeatMate.m3u8");
        std::ofstream os(m3u, std::ios::binary | std::ios::trunc);
        if (os) {
            os << "#EXTM3U\n";
            os << "#EXTINF:" << static_cast<int>(track.duration) << ","
               << track.artist << " - " << track.title << "\n";
            os << track.filePath << "\n";
            os.close();
        }
        auto sideview = joinPath(root, "Sideview");
        ensureDir(sideview);
        copyFileTo(track.filePath, sideview);
        return { true, DJTarget::VirtualDJ,
                 "VirtualDJ non joignable en HTTP - ajoute dans Playlists > BeatMate.m3u8 + Sideview" };
    }
    return { false, DJTarget::VirtualDJ,
             "VirtualDJ non detecte : rien n'a ete envoye. Le chemin est dans le presse-papier." };
}

SendResult SendToDJRouter::sendToRekordbox(const std::vector<Models::Track>& tracks) {
    spdlog::info("[SendToDJRouter::sendToRekordbox] BEGIN ({} tracks)", tracks.size());

    auto appdata = appDataRoamingPath();
    std::string rbDir;
    if (!appdata.empty()) {
        for (const char* sub : { "Pioneer\\rekordbox7",
                                  "Pioneer\\rekordbox",
                                  "Pioneer\\rekordbox6" }) {
            auto candidate = joinPath(appdata, sub);
            if (dirExists(candidate)) { rbDir = candidate; break; }
        }
    }
    if (rbDir.empty()) {
        auto la = envVar("LOCALAPPDATA");
        if (!la.empty()) {
            for (const char* sub : { "Pioneer\\rekordbox7", "Pioneer\\rekordbox" }) {
                auto candidate = joinPath(la, sub);
                if (dirExists(candidate)) { rbDir = candidate; break; }
            }
        }
    }

    // ALWAYS also create a visible inbox in Documents/BeatMate so the user can
    std::string docsInbox;
    std::string docsBeatDir;
    if (auto docs = documentsPath(); !docs.empty()) {
        docsBeatDir = joinPath(docs, "BeatMate");
        ensureDir(docsBeatDir);
        docsInbox = joinPath(docsBeatDir, "rekordbox_inbox.xml");
    }

    spdlog::info("[SendToDJRouter::sendToRekordbox] rbDir='{}' docsInbox='{}'",
                 rbDir, docsInbox);

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<DJ_PLAYLISTS Version=\"1.0.0\">\n";
    xml << "  <PRODUCT Name=\"BeatMate\" Version=\"11\" Company=\"BeatMate\"/>\n";
    xml << "  <COLLECTION Entries=\"" << tracks.size() << "\">\n";

    auto xmlEscape = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&': r += "&amp;"; break;
                case '<': r += "&lt;"; break;
                case '>': r += "&gt;"; break;
                case '"': r += "&quot;"; break;
                case '\'': r += "&apos;"; break;
                default: r += c;
            }
        }
        return r;
    };

    int id = 1;
    for (const auto& t : tracks) {
        // Rekordbox expects file:// URL paths
        juce::String loc = juce::URL(juce::File(juce::String(t.filePath))).toString(false);
        xml << "    <TRACK TrackID=\"" << id++
            << "\" Name=\""    << xmlEscape(t.title)  << "\""
            << " Artist=\""    << xmlEscape(t.artist) << "\""
            << " Album=\""     << xmlEscape(t.album)  << "\""
            << " Genre=\""     << xmlEscape(t.genre)  << "\""
            << " AverageBpm=\""<< t.bpm << "\""
            << " Tonality=\""  << xmlEscape(t.key)    << "\""
            << " Location=\""  << xmlEscape(loc.toStdString()) << "\"/>\n";
    }
    xml << "  </COLLECTION>\n";
    xml << "  <PLAYLISTS>\n";
    xml << "    <NODE Type=\"0\" Name=\"ROOT\" Count=\"1\">\n";
    xml << "      <NODE Name=\"BeatMate\" Type=\"1\" KeyType=\"0\" Entries=\"" << tracks.size() << "\">\n";
    for (int i = 1; i <= static_cast<int>(tracks.size()); ++i) {
        xml << "        <TRACK Key=\"" << i << "\"/>\n";
    }
    xml << "      </NODE>\n";
    xml << "    </NODE>\n";
    xml << "  </PLAYLISTS>\n";
    xml << "</DJ_PLAYLISTS>\n";

    const std::string xmlContent = xml.str();
    std::vector<std::string> writtenXmls;

    // Primary : rekordbox %APPDATA% folder (if detected).
    std::string rbXmlPath;
    if (!rbDir.empty()) {
        rbXmlPath = joinPath(rbDir, "beatmate_queue.xml");
        std::ofstream os(rbXmlPath, std::ios::binary | std::ios::trunc);
        if (os) {
            os << xmlContent;
            os.close();
            writtenXmls.push_back(rbXmlPath);
            spdlog::info("[SendToDJRouter::sendToRekordbox] wrote {}", rbXmlPath);
        } else {
            spdlog::warn("[SendToDJRouter::sendToRekordbox] cannot write {}", rbXmlPath);
        }

        // m3u8 companion for Explorer-drag-friendly importing.
        auto m3uPath = joinPath(rbDir, "beatmate_pending.m3u8");
        std::ofstream mos(m3uPath, std::ios::binary | std::ios::trunc);
        if (mos) {
            mos << "#EXTM3U\n";
            for (const auto& t : tracks) {
                if (t.filePath.empty()) continue;
                mos << "#EXTINF:" << static_cast<int>(t.duration) << ","
                    << t.artist << " - " << t.title << "\n";
                mos << t.filePath << "\n";
            }
        }
    }

    // Secondary (ALWAYS) : visible copy in Documents/BeatMate.
    if (!docsInbox.empty()) {
        std::ofstream os2(docsInbox, std::ios::binary | std::ios::trunc);
        if (os2) {
            os2 << xmlContent;
            os2.close();
            writtenXmls.push_back(docsInbox);
            spdlog::info("[SendToDJRouter::sendToRekordbox] wrote {}", docsInbox);
        }
        // Also write the m3u8 companion into Documents/BeatMate.
        auto m3uDocs = joinPath(docsBeatDir, "rekordbox_inbox.m3u8");
        std::ofstream mos(m3uDocs, std::ios::binary | std::ios::trunc);
        if (mos) {
            mos << "#EXTM3U\n";
            for (const auto& t : tracks) {
                if (t.filePath.empty()) continue;
                mos << "#EXTINF:" << static_cast<int>(t.duration) << ","
                    << t.artist << " - " << t.title << "\n";
                mos << t.filePath << "\n";
            }
        }
    }

    if (writtenXmls.empty()) {
        spdlog::error("[SendToDJRouter::sendToRekordbox] No XML could be written");
        return { false, DJTarget::Rekordbox,
                 "Rekordbox : impossible d'ecrire le XML d'import. Le chemin est dans le presse-papier : Ctrl+V sur le deck.",
                 SendMethod::NotSent };
    }

    // CRITICAL: put the file on the clipboard as a REAL CF_HDROP shell drop
#if JUCE_WINDOWS
    if (!tracks.empty() && !tracks.front().filePath.empty()) {
        const auto& t0 = tracks.front();
        juce::File f{juce::String::fromUTF8(t0.filePath.c_str())};
        if (f.existsAsFile()) {
            // Path must survive non-ASCII accents: go through juce::String
            const std::wstring wpath =
                juce::String::fromUTF8(t0.filePath.c_str())
                    .toUTF16().getAddress();
            const std::string searchUtf8 =
                  !t0.title.empty()  ? t0.title
                : !t0.artist.empty() ? t0.artist
                :                      f.getFileNameWithoutExtension().toStdString();
            const std::wstring wsearch =
                juce::String::fromUTF8(searchUtf8.c_str()).toUTF16().getAddress();
            bool ok = setClipboardDropList({ wpath }, wsearch);
            spdlog::info("[SendToDJRouter::sendToRekordbox] CF_HDROP clipboard -> {} "
                         "(search='{}')",
                         ok ? "OK" : "FAIL", searchUtf8);
        }
    }
#endif

    bool rbRunning = false;
    bool importSequenceFired = false;
#if JUCE_WINDOWS
    if (isProcessRunningWin(L"rekordbox.exe")) {
        rbRunning = true;
        bringAppToFrontWin(L"rekordbox.exe");
        spdlog::info("[SendToDJRouter::sendToRekordbox] brought rekordbox.exe to front");

        if (!tracks.empty() && !tracks.front().filePath.empty()) {
            const std::wstring wpathImport =
                juce::String::fromUTF8(tracks.front().filePath.c_str())
                    .toUTF16().getAddress();
            rekordboxImportTrackAfter(350, wpathImport);
            importSequenceFired = true;
            spdlog::info("[SendToDJRouter::sendToRekordbox] Import-Track sequence scheduled");
        }
    } else {
        spdlog::info("[SendToDJRouter::sendToRekordbox] rekordbox.exe not running");
    }
#endif

    const std::string xmlPath = writtenXmls.front();
    std::string msg;
    SendMethod method = SendMethod::FileInbox;
    if (importSequenceFired) {
        msg = "Rekordbox : Ctrl+O -> collage -> Entree envoyes. Verifiez la Collection : "
              "la piste doit apparaitre et se charger sur le Deck 1. Si rien ne bouge, "
              "la dialog Import n'a pas pris le focus — utilisez Fichier > Importer piste puis Ctrl+V.";
        method = SendMethod::Keyboard;
    } else if (rbRunning) {
        msg = std::string("Rekordbox actif : fichier sur le presse-papier. Fichier > Importer piste puis Ctrl+V, "
                          "ou Fichier > Importer playlist -> ") + xmlPath;
        method = SendMethod::Clipboard;
    } else {
        msg = std::string("Rekordbox non lance. XML ecrit : ") + xmlPath +
              " - ouvre Rekordbox puis Fichier > Importer playlist. Le chemin du fichier est dans le presse-papier (Ctrl+V).";
        method = SendMethod::FileInbox;
    }
    spdlog::info("[SendToDJRouter::sendToRekordbox] DONE running={} importSeq={} method={} xml='{}'",
                 rbRunning, importSequenceFired, static_cast<int>(method), xmlPath);
    return { true, DJTarget::Rekordbox, msg, method };
}

SendResult SendToDJRouter::sendToSerato(const std::vector<Models::Track>& tracks) {
    std::vector<std::string> roots;
    if (auto m = musicPath();    !m.empty()) roots.push_back(joinPath(m, "_Serato_"));
    if (auto d = documentsPath();!d.empty()) roots.push_back(joinPath(d, "_Serato_"));
#if JUCE_WINDOWS
    DWORD drives = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        char letter = static_cast<char>('A' + i);
        std::string p = std::string(1, letter) + ":\\_Serato_";
        if (dirExists(p)) roots.push_back(p);
    }
#endif

    std::string seratoDir;
    for (const auto& r : roots) { if (dirExists(r)) { seratoDir = r; break; } }
    if (seratoDir.empty()) {
        return { false, DJTarget::Serato,
                 "Serato non detecte : rien n'a ete envoye. Le chemin est dans le presse-papier." };
    }
    auto subcrates = joinPath(seratoDir, "Subcrates");
    ensureDir(subcrates);

    // Optional: stamp cues (best-effort, ignore errors)
    Serato::SeratoTagWriter tagWriter;
    for (const auto& t : tracks) {
        if (t.filePath.empty()) continue;
        // Empty cue vector is a safe no-op stamp (updates BPM frame)
        (void)tagWriter.writeToFile(t.filePath, {}, {}, static_cast<float>(t.bpm));
    }

    // Build crate containing absolute paths (Serato stores them relative to
    std::vector<std::string> paths;
    paths.reserve(tracks.size());
    for (const auto& t : tracks) {
        if (!t.filePath.empty()) paths.push_back(t.filePath);
    }
    if (paths.empty()) return { false, DJTarget::Serato, "Aucun chemin valide" };

    auto cratePath = joinPath(subcrates, "BeatMate.crate");
    auto bytes = buildSeratoCrate(paths);
    if (!writeBinary(cratePath, bytes)) {
        return { false, DJTarget::Serato, "Ecriture BeatMate.crate impossible" };
    }

    auto dbV2 = joinPath(seratoDir, "database V2");
    if (makeFile(dbV2).existsAsFile()) {
        makeFile(dbV2).setLastModificationTime(juce::Time::getCurrentTime());
    }

    bool directDrop = false;
#if JUCE_WINDOWS
    // Bring Serato to front and perform a REAL OLE drag&drop of the files on
    for (const wchar_t* exe : { L"serato dj pro.exe", L"seratodjpro.exe", L"serato.exe" }) {
        if (isProcessRunningWin(exe)) {
            bringAppToFrontWin(exe);
            if (HWND h = findMainWindowForExe(exe); h && !paths.empty()) {
                std::vector<std::wstring> wfiles;
                for (const auto& p : paths)
                    wfiles.emplace_back(juce::String::fromUTF8(p.c_str()).toWideCharPointer());
                directDrop = oleDragDropToWindow(h, wfiles);
                if (!directDrop) {
                    bool ok = sendFileDropToWindow(h, wfiles.front());
                    spdlog::info("[SendToDJRouter::sendToSerato] WM_DROPFILES fallback -> {}", ok ? "OK" : "FAIL");
                    autoPasteIntoForegroundAfter(700, /*openSearchFirst=*/true);
                }
            }
            break;
        }
    }
#endif
    if (directDrop) {
        return { true, DJTarget::Serato,
                 "Depose directement dans Serato (" + std::to_string(paths.size()) +
                 " titre(s)) + crate BeatMate mise a jour." };
    }
    return { true, DJTarget::Serato,
             "Ajoute dans Serato > Crates > BeatMate (" +
             std::to_string(paths.size()) + " titre(s)). Chemin colle dans la recherche si Serato tourne." };
}

SendResult SendToDJRouter::sendToTraktor(const std::vector<Models::Track>& tracks) {
    auto docs = documentsPath();
    if (docs.empty()) return { false, DJTarget::Traktor, "Dossier Documents introuvable" };

    auto profile = findTraktorProfile(docs);
    if (profile.empty()) {
        return { false, DJTarget::Traktor,
                 "Traktor non detecte : rien n'a ete envoye. Le chemin est dans le presse-papier." };
    }

    Traktor::TraktorNmlExporter exporter;
    for (const auto& t : tracks) {
        Traktor::TraktorNmlExporter::ExportTrack et;
        et.filePath = t.filePath;
        et.title    = t.title;
        et.artist   = t.artist;
        et.album    = t.album;
        et.genre    = t.genre;
        et.comment  = t.comment;
        et.key      = t.camelotKey.empty() ? t.key : t.camelotKey;
        et.bpm      = static_cast<float>(t.bpm);
        et.duration = t.duration;
        exporter.addTrack(et);
    }

    auto nmlPath = joinPath(profile, "beatmate.nml");
    if (!exporter.exportToFile(nmlPath)) {
        return { false, DJTarget::Traktor, "Ecriture beatmate.nml impossible" };
    }

    bool directDrop = false;
#if JUCE_WINDOWS
    for (const wchar_t* exe : { L"traktor.exe", L"traktor pro 3.exe" }) {
        if (isProcessRunningWin(exe)) {
            bringAppToFrontWin(exe);
            if (HWND h = findMainWindowForExe(exe); h && !tracks.empty()
                && !tracks.front().filePath.empty()) {
                std::vector<std::wstring> wfiles;
                for (const auto& t : tracks)
                    if (!t.filePath.empty())
                        wfiles.emplace_back(juce::String::fromUTF8(t.filePath.c_str()).toWideCharPointer());
                directDrop = !wfiles.empty() && oleDragDropToWindow(h, wfiles);
                if (!directDrop && !wfiles.empty()) {
                    bool ok = sendFileDropToWindow(h, wfiles.front());
                    spdlog::info("[SendToDJRouter::sendToTraktor] WM_DROPFILES fallback -> {}", ok ? "OK" : "FAIL");
                    autoPasteIntoForegroundAfter(700, /*openSearchFirst=*/true);
                }
            }
            break;
        }
    }
#endif
    if (directDrop) {
        return { true, DJTarget::Traktor,
                 "Depose directement dans Traktor + NML ecrit : " + nmlPath };
    }
    return { true, DJTarget::Traktor,
             std::string("NML ecrit : ") + nmlPath +
             ". Dans Traktor : Preferences > File Management > Import. Chemin colle dans la recherche si Traktor tourne." };
}

SendResult SendToDJRouter::sendToEngineDJ(const std::vector<Models::Track>& tracks) {
    std::vector<std::string> roots;
    if (auto m = musicPath(); !m.empty()) roots.push_back(joinPath(m, "Engine Library"));
    if (auto d = documentsPath(); !d.empty()) roots.push_back(joinPath(d, "Engine Library"));
    if (auto ad = appDataRoamingPath(); !ad.empty()) {
        roots.push_back(joinPath(ad, "Engine DJ"));
        roots.push_back(joinPath(ad, "Denon DJ\\Engine Library"));
    }
#if JUCE_WINDOWS
    DWORD drives = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        char letter = static_cast<char>('A' + i);
        std::string p = std::string(1, letter) + ":\\Engine Library";
        if (dirExists(p)) roots.push_back(p);
    }
#endif

    std::string engineLib;
    for (const auto& r : roots) { if (dirExists(r)) { engineLib = r; break; } }
    if (engineLib.empty()) {
        return { false, DJTarget::EngineDJ,
                 "Engine DJ non detecte : rien n'a ete envoye. Le chemin est dans le presse-papier." };
    }

    auto prepare = joinPath(engineLib, "Prepare");
    ensureDir(prepare);
    int copied = 0;
    for (const auto& t : tracks) {
        if (t.filePath.empty()) continue;
        if (copyFileTo(t.filePath, prepare)) ++copied;
    }
    if (copied == 0) {
        return { false, DJTarget::EngineDJ, "Aucun fichier copie vers Engine Library/Prepare" };
    }

    bool directDrop = false;
#if JUCE_WINDOWS
    for (const wchar_t* exe : { L"engine dj.exe", L"enginedj.exe", L"engineprime.exe" }) {
        if (isProcessRunningWin(exe)) {
            bringAppToFrontWin(exe);
            if (HWND h = findMainWindowForExe(exe); h && !tracks.empty()
                && !tracks.front().filePath.empty()) {
                std::vector<std::wstring> wfiles;
                for (const auto& t : tracks)
                    if (!t.filePath.empty())
                        wfiles.emplace_back(juce::String::fromUTF8(t.filePath.c_str()).toWideCharPointer());
                directDrop = !wfiles.empty() && oleDragDropToWindow(h, wfiles);
                if (!directDrop && !wfiles.empty()) {
                    bool ok = sendFileDropToWindow(h, wfiles.front());
                    spdlog::info("[SendToDJRouter::sendToEngineDJ] WM_DROPFILES fallback -> {}", ok ? "OK" : "FAIL");
                    autoPasteIntoForegroundAfter(700, /*openSearchFirst=*/true);
                }
            }
            break;
        }
    }
#endif
    if (directDrop) {
        return { true, DJTarget::EngineDJ,
                 "Depose directement dans Engine DJ + copie vers Prepare (" +
                 std::to_string(copied) + " fichier(s))." };
    }
    return { true, DJTarget::EngineDJ,
             "Copie vers Engine Library/Prepare (" + std::to_string(copied) +
             " fichier(s)). Dans Engine DJ : onglet Prepare -> Rescan." };
}

} // namespace BeatMate::Services::DJSoftware
