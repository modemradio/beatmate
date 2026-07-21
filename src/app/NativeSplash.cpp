#include "NativeSplash.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <array>
#include <mutex>
#include <cwchar>

#pragma comment(lib, "msimg32.lib")  // GradientFill, AlphaBlend
#include <objidl.h>   // IStream (requis par gdiplus.h sous WIN32_LEAN_AND_MEAN)
#include <gdiplus.h>
#include <shlwapi.h>  // SHCreateMemStream
#include "BinaryData.h"
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// BEATMATE_VERSION vient de CMake.
#define BM_WIDEN2(x) L##x
#define BM_WIDEN(x) BM_WIDEN2(x)

namespace BeatMate {

namespace {

// Dimensionne au demarrage selon l'ecran de l'utilisateur.
int  g_winW = 1440;
int  g_winH = 860;
constexpr UINT  kAnimTimerId    = 1;
constexpr UINT  kAnimIntervalMs = 16; // ~60 FPS
const wchar_t   kClassName[]    = L"BeatMateNativeSplashClass";

void computeSplashSize(int screenW, int screenH, int& outW, int& outH) {
    // ~65 % de la largeur ecran, ratio 1.68:1.
    float target = screenW * 0.65f;
    if (target < 900.f)  target = 900.f;
    if (target > 1900.f) target = 1900.f;
    outW = (int)target;
    outH = (int)(target / 1.68f);
    if (outW > screenW - 40) outW = screenW - 40;
    if (outH > screenH - 40) outH = screenH - 40;
}

constexpr int kNumParticles = 72;
struct Particle { float x, y, vx, vy, life, size; BYTE r, g, b; };
std::array<Particle, kNumParticles> g_particles{};

constexpr int kNumOrbit = 12;
struct OrbitDot { float angOffset; float radiusMul; BYTE r, g, b; };
std::array<OrbitDot, kNumOrbit> g_orbit{};

constexpr int kWaveBars = 64;
constexpr int kSpectrumBars = 24;

bool   g_particlesInit = false;
bool   g_orbitInit     = false;
float  g_phase         = 0.0f;
ULONGLONG g_startMs    = 0;

std::atomic<float> g_targetPct { 0.0f };
float              g_displayPct = 0.0f;

std::mutex   g_labelMutex;
std::wstring g_label = L"Initialisation…";

// Helpers
inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline BYTE  lerpByte(int a, int b, float t) { t = clamp01(t); return (BYTE)(a + (b - a) * t); }
inline float frand(float a, float b) { return a + (b - a) * ((float)rand() / (float)RAND_MAX); }

void fillAlphaRect(HDC dst, int x, int y, int w, int h, BYTE r, BYTE g, BYTE b, BYTE a) {
    if (w <= 0 || h <= 0 || a == 0) return;
    HDC memDC = CreateCompatibleDC(dst);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        auto* px = (DWORD*)bits;
        BYTE pr = (BYTE)(r * a / 255), pg = (BYTE)(g * a / 255), pb = (BYTE)(b * a / 255);
        DWORD v = ((DWORD)a << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
        for (int i = 0; i < w * h; ++i) px[i] = v;
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, a, AC_SRC_ALPHA };
        AlphaBlend(dst, x, y, w, h, memDC, 0, 0, w, h, bf);
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
    }
    DeleteDC(memDC);
}

void fillAlphaEllipse(HDC dst, int cx, int cy, int r, BYTE cr, BYTE cg, BYTE cb, BYTE a) {
    if (r <= 0 || a == 0) return;
    int d = r * 2;
    HDC memDC = CreateCompatibleDC(dst);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = d; bmi.bmiHeader.biHeight = -d;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        auto* px = (DWORD*)bits;
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x) {
                float dx = (float)(x - r), dy = (float)(y - r);
                float dist = std::sqrt(dx * dx + dy * dy);
                float n = dist / (float)r;
                if (n > 1.f) { px[y * d + x] = 0; continue; }
                float f = (1.f - n); f = f * f;
                BYTE aa = (BYTE)(a * f);
                BYTE pr = (BYTE)(cr * aa / 255), pg = (BYTE)(cg * aa / 255), pb = (BYTE)(cb * aa / 255);
                px[y * d + x] = ((DWORD)aa << 24) | ((DWORD)pr << 16) | ((DWORD)pg << 8) | (DWORD)pb;
            }
        HGDIOBJ oldBmp = SelectObject(memDC, bmp);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(dst, cx - r, cy - r, d, d, memDC, 0, 0, d, d, bf);
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
    }
    DeleteDC(memDC);
}

void fillRoundRect(HDC dc, int x, int y, int w, int h, int rad,
                    COLORREF col, COLORREF border, int bw) {
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = bw > 0 ? CreatePen(PS_SOLID, bw, border) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ oldBr = SelectObject(dc, br), oldPen = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w, y + h, rad, rad);
    SelectObject(dc, oldBr); SelectObject(dc, oldPen);
    DeleteObject(br); if (bw > 0) DeleteObject(pen);
}

HFONT makeFont(int px, int weight, bool italic = false, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(px, 0, 0, 0, weight, italic, FALSE, FALSE,
                       ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, face);
}

void drawText(HDC dc, const wchar_t* s, RECT rc, HFONT f, COLORREF col,
               UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER) {
    HGDIOBJ oldF = SelectObject(dc, f);
    SetTextColor(dc, col); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &rc, flags);
    SelectObject(dc, oldF);
}

void initParticles() {
    for (auto& p : g_particles) {
        p.x = frand(0.f, (float)g_winW);
        p.y = frand(0.f, (float)g_winH);
        p.vx = frand(-0.18f, 0.18f);
        p.vy = frand(-0.75f, -0.20f);
        p.size = frand(1.5f, 4.0f);
        p.life = frand(0.3f, 1.0f);
        int tint = rand() % 5;
        switch (tint) {
            case 0: p.r = 0x63; p.g = 0x66; p.b = 0xF1; break;
            case 1: p.r = 0x3B; p.g = 0x82; p.b = 0xF6; break;
            case 2: p.r = 0x8B; p.g = 0x5C; p.b = 0xF6; break;
            case 3: p.r = 0x22; p.g = 0xD3; p.b = 0xEE; break;
            default:p.r = 0xEC; p.g = 0x4D; p.b = 0x9E; break;
        }
    }
    g_particlesInit = true;
}

void initOrbit() {
    for (int i = 0; i < kNumOrbit; ++i) {
        g_orbit[i].angOffset = i * (6.28318f / kNumOrbit);
        g_orbit[i].radiusMul = 1.0f + ((i % 3) - 1) * 0.12f;
        int tint = i % 3;
        switch (tint) {
            case 0: g_orbit[i].r = 0x3B; g_orbit[i].g = 0x82; g_orbit[i].b = 0xF6; break;
            case 1: g_orbit[i].r = 0x8B; g_orbit[i].g = 0x5C; g_orbit[i].b = 0xF6; break;
            default:g_orbit[i].r = 0x22; g_orbit[i].g = 0xD3; g_orbit[i].b = 0xEE; break;
        }
    }
    g_orbitInit = true;
}

void stepAndDrawParticles(HDC dc) {
    if (!g_particlesInit) initParticles();
    for (auto& p : g_particles) {
        p.x += p.vx; p.y += p.vy;
        p.life -= 0.0025f;
        if (p.life <= 0.f || p.y < -12.f || p.x < -12.f || p.x > g_winW + 12.f) {
            p.x = frand(0.f, (float)g_winW);
            p.y = frand((float)g_winH, (float)g_winH + 40.f);
            p.life = 1.0f;
        }
        BYTE a = (BYTE)(clamp01(p.life) * 200.f);
        fillAlphaEllipse(dc, (int)p.x, (int)p.y, (int)(p.size * 3.2f), p.r, p.g, p.b, (BYTE)(a / 7));
        fillAlphaEllipse(dc, (int)p.x, (int)p.y, (int)p.size, p.r, p.g, p.b, a);
    }
}

void drawHexagon(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF border, int bw) {
    POINT pts[6];
    for (int i = 0; i < 6; ++i) {
        float a = (i * 60.f - 90.f) * 3.14159265f / 180.f;
        pts[i].x = cx + (LONG)(std::cos(a) * r);
        pts[i].y = cy + (LONG)(std::sin(a) * r);
    }
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = bw > 0 ? CreatePen(PS_SOLID, bw, border) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ oldBr = SelectObject(dc, br), oldPen = SelectObject(dc, pen);
    Polygon(dc, pts, 6);
    SelectObject(dc, oldBr); SelectObject(dc, oldPen);
    DeleteObject(br); if (bw > 0) DeleteObject(pen);
}

void drawSplashContent(HDC hdc, int width, int height) {
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

    {
        TRIVERTEX v[2] = {};
        v[0].x = 0; v[0].y = 0;
        v[0].Red = 0x00 << 8; v[0].Green = 0x0F << 8; v[0].Blue = 0x2D << 8;
        v[1].x = width; v[1].y = height;
        v[1].Red = 0x02 << 8; v[1].Green = 0x06 << 8; v[1].Blue = 0x16 << 8;
        GRADIENT_RECT gr = { 0, 1 };
        GradientFill(memDC, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    }
    fillAlphaEllipse(memDC, width / 2, (int)(height * 0.30f), 280, 0x3B, 0x82, 0xF6, 28);

    const int cardX = 0, cardY = 0, cardW = width, cardH = height;
    const int rad = (int)(height * 0.012f);
    (void)rad;

    float target = clamp01(g_targetPct.load());
    if (target > g_displayPct) {
        float step = (target - g_displayPct) * 0.18f;
        float minStep = 0.0035f;
        if (step < minStep) step = minStep;
        g_displayPct += step;
        if (g_displayPct > target) g_displayPct = target;
    }
    float pct = g_displayPct;
    int pctInt = (int)(pct * 100.f);

    bool logoDrawn = false;
    {
        const int areaW = (int)(width * 0.52f);
        const int areaH = (int)(height * 0.44f);
        const int areaX = (width - areaW) / 2;
        const int areaY = (int)(height * 0.16f);
        (void)areaX;
        {
            float pulse = 0.5f + 0.5f * std::sin(g_phase * 1.2f);
            fillAlphaEllipse(memDC, width / 2, areaY + areaH / 2,
                             (int)(areaW * 0.40f + pulse * 12), 0x6C, 0x4C, 0xFF,
                             (BYTE)(26 + pulse * 18));
        }
        IStream* st = SHCreateMemStream(
            (const BYTE*)BinaryData::beatmate_logo_full_png,
            (UINT)BinaryData::beatmate_logo_full_pngSize);
        if (st) {
            Gdiplus::Bitmap bmp(st);
            if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0) {
                float ar = (float)bmp.GetWidth() / (float)bmp.GetHeight();
                int tw = areaW, th = (int)(areaW / ar);
                if (th > areaH) { th = areaH; tw = (int)(areaH * ar); }
                int ox = (width - tw) / 2;
                int oy = areaY + (areaH - th) / 2;
                Gdiplus::Graphics gfx(memDC);
                gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                gfx.DrawImage(&bmp, Gdiplus::Rect(ox, oy, tw, th));
                logoDrawn = true;
            }
            st->Release();
        }
        if (!logoDrawn) {
            HFONT tf = makeFont((int)(height * 0.09f), FW_LIGHT);
            RECT rc = { 0, (int)(height * 0.34f), width, (int)(height * 0.46f) };
            drawText(memDC, L"BeatMate", rc, tf, RGB(0xF8, 0xFA, 0xFC));
            DeleteObject(tf);
        }
    }

    {
        int subTop = (int)(height * 0.63f);
        HFONT sf = makeFont((int)(height * 0.024f), FW_NORMAL);
        RECT rc = { 0, subTop, width, subTop + (int)(height * 0.04f) };
        drawText(memDC, L"PROFESSIONAL DJ SUITE   \x00B7   V" BM_WIDEN(BEATMATE_VERSION), rc, sf,
                 RGB(0x8B, 0x9A, 0xB8),
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(sf);
    }

    {
        int barW = (int)(width * 0.55f);
        int barH = 3;
        int barX = (width - barW) / 2;
        int barY = (int)(height * 0.78f);
        fillAlphaRect(memDC, barX, barY, barW, barH, 0x1E, 0x29, 0x3B, 200);
        int fillW = (int)(barW * pct);
        if (fillW > 1) {
            TRIVERTEX v[2] = {};
            v[0].x = barX; v[0].y = barY;
            v[0].Red = 0x3B << 8; v[0].Green = 0x82 << 8; v[0].Blue = 0xF6 << 8;
            v[1].x = barX + fillW; v[1].y = barY + barH;
            v[1].Red = 0x8B << 8; v[1].Green = 0x5C << 8; v[1].Blue = 0xF6 << 8;
            GRADIENT_RECT gr = { 0, 1 };
            GradientFill(memDC, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
        }
    }

    {
        std::wstring label;
        {
            std::lock_guard<std::mutex> lk(g_labelMutex);
            label = g_label;
        }
        wchar_t buf[160];
        swprintf_s(buf, 160, L"%ls   %d%%", label.c_str(), pctInt);
        int txtY = (int)(height * 0.81f);
        HFONT tf = makeFont((int)(height * 0.022f), FW_NORMAL);
        RECT rc = { 0, txtY, width, txtY + (int)(height * 0.04f) };
        drawText(memDC, buf, rc, tf, RGB(0x94, 0xA3, 0xB8),
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(tf);
    }

    {
        int y = height - (int)(height * 0.045f);
        HFONT f = makeFont((int)(height * 0.018f), FW_NORMAL);
        RECT rc = { 0, y, width, y + (int)(height * 0.03f) };
        drawText(memDC, L"\x00A9 2026 BeatMate   \x00B7   V" BM_WIDEN(BEATMATE_VERSION),
                  rc, f, RGB(0x40, 0x4D, 0x60),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(f);
    }

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

LRESULT CALLBACK splashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_startMs = GetTickCount64();
            g_displayPct = 0.0f;
            g_targetPct.store(0.0f);
            if (!g_particlesInit) initParticles();
            if (!g_orbitInit)     initOrbit();
            SetTimer(hwnd, kAnimTimerId, kAnimIntervalMs, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == kAnimTimerId) {
                g_phase += 0.075f;
                if (g_phase > 1000.f) g_phase -= 1000.f;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            drawSplashContent(hdc, rc.right - rc.left, rc.bottom - rc.top);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kAnimTimerId);
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // anonymous namespace

NativeSplash::NativeSplash() = default;
NativeSplash::~NativeSplash() { close(); }

void NativeSplash::show() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread([this]() {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken = 0;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        WNDCLASSW wc = {};
        wc.lpfnWndProc = splashWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);
        int sW = GetSystemMetrics(SM_CXSCREEN);
        int sH = GetSystemMetrics(SM_CYSCREEN);
        computeSplashSize(sW, sH, g_winW, g_winH);
        g_particlesInit = false; // re-seed particles for new canvas size
        int x = (sW - g_winW) / 2;
        int y = (sH - g_winH) / 2;
        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName,
            L"BeatMate V12 - Chargement", WS_POPUP,
            x, y, g_winW, g_winH, nullptr, nullptr, hInst, nullptr);
        if (!hwnd) { m_running.store(false); return; }
        m_hwnd = hwnd;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        MSG msg;
        while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
        UnregisterClassW(kClassName, hInst);
        m_hwnd = nullptr;
        if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    });
}

void NativeSplash::close() {
    if (!m_running.load()) {
        if (m_thread.joinable()) m_thread.join();
        return;
    }
    m_running.store(false);
    if (m_hwnd)
        PostMessageW(reinterpret_cast<HWND>(m_hwnd), WM_CLOSE, 0, 0);
    if (m_thread.joinable()) m_thread.join();
}

void NativeSplash::setProgress(float pct, const wchar_t* label) {
    if (pct < 0.f) pct = 0.f;
    if (pct > 1.f) pct = 1.f;
    if (pct > g_targetPct.load())
        g_targetPct.store(pct);
    if (label != nullptr) {
        std::lock_guard<std::mutex> lk(g_labelMutex);
        g_label = label;
    }
}

void NativeSplash::finishAndClose(int stepMs) {
    if (stepMs < 1) stepMs = 1;
    {
        std::lock_guard<std::mutex> lk(g_labelMutex);
        g_label = L"Prêt";
    }
    g_targetPct.store(1.0f);
    for (int i = 0; i < 22 && g_displayPct < 0.999f; ++i)
        Sleep(stepMs);
    Sleep(20);
    close();
}

} // namespace BeatMate

#else // !_WIN32

namespace BeatMate {
NativeSplash::NativeSplash() = default;
NativeSplash::~NativeSplash() = default;
void NativeSplash::show()  {}
void NativeSplash::close() {}
void NativeSplash::setProgress(float, const wchar_t*) {}
void NativeSplash::finishAndClose(int) {}
} // namespace BeatMate

#endif
