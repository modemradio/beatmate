#include "BeatMateLiveWindow.h"

#include "../../app/ServiceLocator.h"
#include "../../services/config/SettingsManager.h"
#include "../../services/config/I18n.h"

#include <spdlog/spdlog.h>
#include <string>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace BeatMate { class ServiceLocator; }
extern BeatMate::ServiceLocator* g_serviceLocator;

namespace BeatMate::UI {

class BeatMateLiveWindow::LiveTrayIcon : public juce::SystemTrayIconComponent
{
public:
    explicit LiveTrayIcon(BeatMateLiveWindow& owner) : owner_(owner) {
        juce::Image img(juce::Image::ARGB, 32, 32, true);
        {
            juce::Graphics g(img);
            g.fillAll(juce::Colour(0xFF1E293B));
            g.setColour(juce::Colour(0xFF60A5FA));
            g.fillRoundedRectangle(2.0f, 2.0f, 28.0f, 28.0f, 6.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold")));
            g.drawFittedText("BM", 0, 0, 32, 32, juce::Justification::centred, 1);
        }
        setIconImage(img, img);
        setIconTooltip(BM_TJ("live.tray.tooltip"));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu m;
            m.addItem(1, BM_TJ("live.tray.showLive"));
            m.addItem(2, BM_TJ("live.tray.fullscreen"));
            m.addSeparator();
            m.addItem(3, BM_TJ("live.tray.quit"));
            m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
                if (r == 1) owner_.expandToFull();
                else if (r == 2) { owner_.setFullScreen(true); owner_.expandToFull(); }
                else if (r == 3) owner_.closeButtonPressed();
            });
        } else {
            owner_.expandToFull();
        }
    }

    void mouseEnter(const juce::MouseEvent&) override {
        owner_.expandToFull();
    }

private:
    BeatMateLiveWindow& owner_;
};

class BeatMateLiveWindow::FloatingBadge : public juce::DocumentWindow,
                                          private juce::Timer
{
public:
    explicit FloatingBadge(BeatMateLiveWindow& owner)
        : juce::DocumentWindow("BeatMate", juce::Colours::transparentBlack, 0, true),
          owner_(owner)
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(true);
        setAlwaysOnTop(true);
        setResizable(false, false);
        setOpaque(false);

        content_ = std::make_unique<Content>(owner_, *this);
        setContentOwned(content_.release(), true);

        setSize(kBadgeW, kBadgeH);
        repositionToDJWindowOrScreen();
        setVisible(true);
        toFront(true);

        // TOOLWINDOW + NOACTIVATE so Windows never gives the badge focus
        applyToolWindowStyle();
        installForegroundHook();
        startTimer(150);
    }

    ~FloatingBadge() override {
        if (eventHook_) {
           #if JUCE_WINDOWS
            UnhookWinEvent(eventHook_);
           #endif
            eventHook_ = nullptr;
        }
        if (s_self == this) s_self = nullptr;
    }

    void closeButtonPressed() override { /* swallow: badge has no X */ }

    void breakDock() { dockBroken_ = true; }

    static constexpr int kBadgeSize = 40;                  // legacy (clamp helpers)
    static constexpr int kBadgeW    = 68;
    static constexpr int kBadgeH    = 68;
    static constexpr int kEdgeInset = 28;

private:
    void timerCallback() override
    {
        // Self-heal: Windows may demote our peer (hide / strip TOPMOST)
        if (!isVisible())     setVisible(true);
        if (!isAlwaysOnTop()) setAlwaysOnTop(true);
        if (!isOnDesktop())   addToDesktop(getDesktopWindowStyleFlags());
        promoteTopmostNoActivate();

        if (dockBroken_) return;
        repositionToDJWindowOrScreen();
    }

    void promoteTopmostNoActivate()
    {
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
        {
            if (HWND h = (HWND) peer->getNativeHandle())
            {
                SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
            }
        }
       #else
        toFront(false);
       #endif
    }

    void applyToolWindowStyle()
    {
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
        {
            if (HWND h = (HWND) peer->getNativeHandle())
            {
                LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
                ex |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
                SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
                SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            }
        }
       #endif
    }

    void installForegroundHook()
    {
       #if JUCE_WINDOWS
        s_self = this;
        eventHook_ = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, &FloatingBadge::foregroundProc,
            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
       #endif
    }

   #if JUCE_WINDOWS
    static void CALLBACK foregroundProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                        LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_SYSTEM_FOREGROUND) return;
        if (idObject != OBJID_WINDOW)         return;
        if (!hwnd || !isDJWindow((void*) hwnd)) return;
        // Marshal to the JUCE message thread before touching the peer
        juce::MessageManager::callAsync([] {
            if (!s_self) return;
            s_self->promoteTopmostNoActivate();
            if (!s_self->isVisible()) s_self->setVisible(true);
        });
    }
   #endif

    void repositionToDJWindowOrScreen()
    {
        auto area = juce::Desktop::getInstance().getDisplays()
                        .getPrimaryDisplay()->userArea;
        setTopLeftPosition(area.getRight() - kBadgeW - kEdgeInset,
                           area.getY() + (area.getHeight() - kBadgeH) / 2);
    }

    static bool isDJWindow(void* hwndRaw)
    {
       #if JUCE_WINDOWS
        HWND hwnd = (HWND) hwndRaw;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return false;
        std::string exeLower;
        if (HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t path[MAX_PATH] {};
            DWORD sz = MAX_PATH;
            if (::QueryFullProcessImageNameW(h, 0, path, &sz)) {
                std::wstring w(path);
                auto slash = w.find_last_of(L"\\/");
                if (slash != std::wstring::npos) w = w.substr(slash + 1);
                exeLower = juce::String(juce::CharPointer_UTF16(
                    reinterpret_cast<const juce::CharPointer_UTF16::CharType*>(w.c_str())))
                        .toLowerCase().toStdString();
            }
            ::CloseHandle(h);
        }
        auto contains = [&](const char* needle) {
            return exeLower.find(needle) != std::string::npos;
        };
        return contains("rekordbox") || contains("serato") || contains("traktor")
            || contains("virtualdj") || contains("engine") || contains("djay");
       #else
        (void) hwndRaw;
        return false;
       #endif
    }

private:
    class Content : public juce::Component,
                    private juce::Timer
    {
    public:
        Content(BeatMateLiveWindow& o, FloatingBadge& badge)
            : owner_(o), badge_(badge) {
            setRepaintsOnMouseActivity(true);
            startTimerHz(30);
        }
        ~Content() override { stopTimer(); }

    private:
        void timerCallback() override { repaint(); }

    public:

        void paint(juce::Graphics& g) override {
            const auto   b    = getLocalBounds().toFloat();
            const float  cx   = b.getCentreX();
            const float  cy   = b.getCentreY();
            const float  rBase= juce::jmin(b.getWidth(), b.getHeight()) * 0.40f;
            const bool   hover= isMouseOver(true);
            const double tMs  = (double) juce::Time::currentTimeMillis();

            const float slow    = 0.5f + 0.5f * (float) std::sin(tMs * 0.0018);
            const float breath  = 0.5f + 0.5f * (float) std::sin(tMs * 0.0030);
            const float beat    = 0.5f + 0.5f * (float) std::sin(tMs * 0.0065);
            const float beatHit = std::pow(beat, 4.0f);
            const float rotPhase= (float) std::fmod(tMs * 0.0012, 6.2831853);

            const float r = rBase * (0.98f + 0.04f * beatHit);

            const auto accentA = juce::Colour(0xFF7C3AED);
            const auto accentB = juce::Colour(0xFFC084FC);
            const auto accentHi= hover ? juce::Colour(0xFFE9D5FF) : juce::Colour(0xFFC4B5FD);
            const auto accent  = accentA.interpolatedWith(accentB, 0.3f + 0.7f * slow);

            for (int i = 4; i >= 1; --i) {
                const float rr = r + i * 3.0f + breath * 2.5f + beatHit * 1.5f;
                g.setColour(accent.withAlpha((0.18f - i * 0.035f)
                                             * (0.5f + 0.5f * breath)));
                g.fillEllipse(cx - rr, cy - rr, rr * 2, rr * 2);
            }

            if (beatHit > 0.15f) {
                const float burstR = r + 2.0f + beatHit * 6.5f;
                g.setColour(juce::Colour(0xFFE879F9).withAlpha(0.22f * beatHit));
                g.drawEllipse(cx - burstR, cy - burstR, burstR * 2, burstR * 2,
                              1.2f + beatHit * 1.5f);
            }

            {
                juce::ColourGradient body(
                    juce::Colour(0xFF241F4A).interpolatedWith(
                        juce::Colour(0xFF2A1F55), slow),
                    cx, cy - r * 0.4f,
                    juce::Colour(0xFF03030B), cx, cy + r, true);
                body.addColour(0.55, juce::Colour(0xFF100D2A));
                body.addColour(0.85, juce::Colour(0xFF07051A));
                g.setGradientFill(body);
                g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
            }

            {
                juce::ColourGradient rim(
                    accent.withAlpha(0.0f), cx, cy - r * 0.2f,
                    accent.withAlpha(0.55f + 0.15f * beat),
                    cx, cy + r * 0.95f, true);
                g.setGradientFill(rim);
                g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
            }

            {
                juce::Path sweep;
                const float arcR = r - 0.5f;
                const float sweepLen = 1.25f;
                sweep.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                                    rotPhase, rotPhase + sweepLen, true);
                for (int pass = 0; pass < 3; ++pass) {
                    const float w = 1.4f + pass * 0.9f;
                    const float a = 0.70f - pass * 0.20f;
                    g.setColour(juce::Colour(0xFFE879F9).withAlpha(a));
                    g.strokePath(sweep, juce::PathStrokeType(
                        w, juce::PathStrokeType::curved,
                        juce::PathStrokeType::rounded));
                }
            }

            {
                juce::ColourGradient gloss(
                    juce::Colour(0x70FFFFFF), cx, cy - r,
                    juce::Colours::transparentWhite, cx, cy - r * 0.1f, false);
                g.setGradientFill(gloss);
                juce::Path arc;
                arc.addEllipse(cx - r * 0.88f, cy - r * 1.02f,
                               r * 1.76f, r * 1.05f);
                g.fillPath(arc);
            }

            g.setColour(accentHi.withAlpha(0.92f));
            g.drawEllipse(cx - r, cy - r, r * 2, r * 2, 1.5f);
            g.setColour(accentHi.withAlpha(0.32f));
            g.drawEllipse(cx - r + 3.2f, cy - r + 3.2f,
                          (r - 3.2f) * 2, (r - 3.2f) * 2, 0.7f);

            {
                juce::ColourGradient txtGlow(
                    accentHi.withAlpha(0.45f * (0.6f + 0.4f * beat)),
                    cx, cy - r * 0.10f,
                    accentHi.withAlpha(0.0f), cx - r, cy - r * 0.10f, true);
                g.setGradientFill(txtGlow);
                g.fillEllipse(cx - r * 0.75f, cy - r * 0.45f,
                              r * 1.5f, r * 0.70f);
            }

            const float textScale = 1.0f + 0.04f * beatHit;
            const float fontH = juce::jmax(11.0f, r * 0.55f * textScale);
            g.setFont(juce::Font(juce::FontOptions{}
                .withHeight(fontH).withStyle("Bold")));
            const float textOffY = -r * 0.12f;
            auto textRc = juce::Rectangle<float>(cx - r, cy - r + textOffY,
                                                 r * 2, r * 2).toNearestInt();
            g.setColour(juce::Colour(0x90000000));
            g.drawText("LIVE", textRc.translated(0, 1),
                       juce::Justification::centred);
            g.setColour(juce::Colours::white);
            g.drawText("LIVE", textRc, juce::Justification::centred);

            {
                const float uw = r * 0.60f;
                const float uh = juce::jmax(1.2f, r * 0.060f);
                const float uy = cy + textOffY + fontH * 0.42f + 1.5f;
                const float shift = 0.5f + 0.5f * (float) std::sin(rotPhase * 2.0f);
                juce::ColourGradient underline(
                    accentHi.withAlpha(0.0f), cx - uw * 0.5f, uy,
                    accentHi.withAlpha(1.0f),
                    cx - uw * 0.5f + uw * shift, uy, false);
                underline.addColour(0.5, juce::Colour(0xFFE879F9));
                underline.addColour(1.0, accentHi.withAlpha(0.0f));
                g.setGradientFill(underline);
                g.fillRoundedRectangle(cx - uw * 0.5f, uy, uw, uh, uh * 0.5f);
            }

            {
                const int   N       = 4;
                const float spacing = r * 0.17f;
                const float barW    = juce::jmax(1.8f, r * 0.095f);
                const float baseY   = cy + r * 0.62f;
                const float maxH    = r * 0.32f;
                const float phases[N] = { 0.0f, 1.1f, 2.3f, 0.55f };
                for (int i = 0; i < N; ++i) {
                    const float ph = phases[i];
                    const float pv = 0.30f + 0.70f
                        * (0.5f + 0.5f * (float) std::sin(tMs * 0.014 + ph));
                    const float h  = juce::jmax(1.8f, maxH * pv);
                    const float x  = cx + (i - (N - 1) * 0.5f) * spacing - barW * 0.5f;
                    const float y  = baseY - h;
                    juce::ColourGradient bar(
                        juce::Colour(0xFFFDF4FF), x, y,
                        accent,                     x, y + h, false);
                    bar.addColour(0.5, juce::Colour(0xFFE879F9));
                    g.setGradientFill(bar);
                    g.fillRoundedRectangle(x, y, barW, h, barW * 0.4f);
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.fillRoundedRectangle(x + barW * 0.22f, y + 0.4f,
                                           barW * 0.56f, juce::jmax(0.8f, barW * 0.35f),
                                           barW * 0.2f);
                }
            }

            {
                const float orbitR = r + 3.0f + breath * 1.2f;
                for (int i = 0; i < 3; ++i) {
                    const float phase = rotPhase + i * 2.0944f;
                    const float ox = cx + std::cos(phase) * orbitR;
                    const float oy = cy + std::sin(phase) * orbitR;
                    const float dR = 1.4f + 0.5f * (float) std::sin(phase * 1.7f);
                    g.setColour(juce::Colour(0xFFE879F9).withAlpha(0.28f));
                    g.fillEllipse(ox - dR - 1.5f, oy - dR - 1.5f,
                                  (dR + 1.5f) * 2, (dR + 1.5f) * 2);
                    g.setColour(juce::Colour(0xFFFDF4FF));
                    g.fillEllipse(ox - dR, oy - dR, dR * 2, dR * 2);
                }
            }

            {
                const float dotR = juce::jmax(2.4f, r * 0.12f);
                const float angle = 2.42f;
                const float dx = std::cos(angle) * (r - dotR - 1.8f);
                const float dy = std::sin(angle) * (r - dotR - 1.8f);
                g.setColour(juce::Colour(0xFFF43F5E)
                             .withAlpha(0.26f + 0.50f * beat));
                g.fillEllipse(cx + dx - dotR - 2.2f, cy + dy - dotR - 2.2f,
                              (dotR + 2.2f) * 2, (dotR + 2.2f) * 2);
                juce::ColourGradient dotG(
                    juce::Colour(0xFFFFE4E6),
                    cx + dx - dotR * 0.35f, cy + dy - dotR * 0.35f,
                    juce::Colour(0xFFBE123C), cx + dx, cy + dy + dotR, true);
                g.setGradientFill(dotG);
                g.fillEllipse(cx + dx - dotR, cy + dy - dotR,
                              dotR * 2, dotR * 2);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.fillEllipse(cx + dx - dotR * 0.5f, cy + dy - dotR * 0.65f,
                              dotR * 0.5f, dotR * 0.38f);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) {
                juce::PopupMenu m;
                m.addItem(1, BM_TJ("live.badge.showLive"));
                m.addSeparator();
                m.addItem(2, BM_TJ("live.badge.repositionCorner"));
                m.addItem(3, BM_TJ("live.badge.hideTrayOnly"));
                m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
                    if (r == 1) owner_.expandToFull();
                    else if (r == 2) {
                        auto a = juce::Desktop::getInstance().getDisplays()
                                     .getPrimaryDisplay()->userArea;
                        if (auto* w = findParentComponentOfClass<FloatingBadge>())
                            w->setTopLeftPosition(a.getRight() - w->getWidth() - 16,
                                                  a.getBottom() - w->getHeight() - 80);
                    } else if (r == 3) {
                        owner_.hideFloatingBadge();
                        owner_.ensureTrayIconVisible();
                    }
                });
                return;
            }
            dragger_.startDraggingComponent(findParentComponentOfClass<FloatingBadge>(), e);
        }

        void mouseDrag(const juce::MouseEvent& e) override {
            if (auto* w = findParentComponentOfClass<FloatingBadge>()) {
                dragger_.dragComponent(w, e, nullptr);
                badge_.breakDock();
            }
        }

        void mouseUp(const juce::MouseEvent& e) override {
            if (e.mouseWasDraggedSinceMouseDown()) return;
            owner_.expandToFull();
        }

        // Intentionally no mouseEnter -> expand

    private:
        BeatMateLiveWindow& owner_;
        FloatingBadge&      badge_;
        juce::ComponentDragger dragger_;
    };

    BeatMateLiveWindow& owner_;
    std::unique_ptr<Content> content_;
    bool                 dockBroken_ { false };

   #if JUCE_WINDOWS
    HWINEVENTHOOK        eventHook_ { nullptr };
   #else
    void*                eventHook_ { nullptr };
   #endif

    // Static pointer for the C-style SetWinEventHook callback (single instance)
    static FloatingBadge* s_self;
};

BeatMateLiveWindow::FloatingBadge* BeatMateLiveWindow::FloatingBadge::s_self = nullptr;

namespace {

constexpr const char* kSettingsKey = "beatmate.live.autoHidePolicy";

#if JUCE_WINDOWS
static BeatMateLiveWindow* s_hotkeyOwner = nullptr;

static const char* kDJProcessNames[] = {
    "rekordbox.exe",
    "serato.exe",
    "seratodjpro.exe",
    "traktor.exe",
    "virtualdj8.exe",
    "virtualdj.exe",
    "engine dj.exe",
    "engine_prime.exe",
    "enginedj.exe"
};
static const char* kDJTitleSubstrings[] = {
    "rekordbox", "serato", "traktor", "virtualdj", "engine"
};

static bool matchesAny(const std::string& haystack, const char* const* needles, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (haystack.find(needles[i]) != std::string::npos) return true;
    return false;
}

// RegisterHotKey needs a thread that runs a Win32 message loop
class HotkeyThread : public juce::Thread
{
public:
    HotkeyThread(int id) : juce::Thread("BeatMateLiveHotkey"), id_(id) {}

    void run() override
    {
        osThreadId_ = ::GetCurrentThreadId();

        if (!RegisterHotKey(nullptr, id_,
                            MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'B'))
        {
            spdlog::warn("[BeatMateLive] RegisterHotKey Ctrl+Shift+B failed (gle={})",
                         (unsigned) GetLastError());
            return;
        }
        registered_ = true;

        MSG msg;
        while (!threadShouldExit() && GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == WM_HOTKEY && msg.wParam == (WPARAM) id_)
            {
                juce::MessageManager::callAsync([] {
                    if (s_hotkeyOwner)
                        s_hotkeyOwner->expandToFull();
                });
            }
            else if (msg.message == WM_QUIT)
            {
                break;
            }
        }

        if (registered_)
            UnregisterHotKey(nullptr, id_);
    }

    void stop()
    {
        signalThreadShouldExit();
        if (osThreadId_ != 0)
            PostThreadMessageW(osThreadId_, WM_QUIT, 0, 0);
        stopThread(1000);
    }

private:
    int   id_         = 0;
    bool  registered_ = false;
    DWORD osThreadId_ = 0;
};
#endif // JUCE_WINDOWS

} // namespace

juce::String BeatMateLiveWindow::policyToString(AutoHidePolicy p)
{
    switch (p)
    {
        case AutoHidePolicy::Disabled:       return "Disabled";
        case AutoHidePolicy::Fade:           return "Fade";
        case AutoHidePolicy::MinimizeToTray: return "MinimizeToTray";
        case AutoHidePolicy::CompactDock:    return "CompactDock";
        case AutoHidePolicy::FloatingIcon:   return "FloatingIcon";
    }
    return "Fade";
}

BeatMateLiveWindow::AutoHidePolicy
BeatMateLiveWindow::policyFromString(const juce::String& s)
{
    if (s == "Disabled")       return AutoHidePolicy::Disabled;
    if (s == "MinimizeToTray") return AutoHidePolicy::MinimizeToTray;
    if (s == "CompactDock")    return AutoHidePolicy::CompactDock;
    if (s == "FloatingIcon")   return AutoHidePolicy::FloatingIcon;
    return AutoHidePolicy::Fade;
}

BeatMateLiveWindow::BeatMateLiveWindow()
    : BeatMateLiveWindow(nullptr) {}

BeatMateLiveWindow::BeatMateLiveWindow(Services::Library::TrackDataProvider* provider)
    : juce::DocumentWindow("Live Suggest",
                           juce::Colour(0xFF0A0E14),
                           juce::DocumentWindow::allButtons)
{
    auto* view = provider ? new BeatMateLiveView(provider) : new BeatMateLiveView();
    setContentOwned(view, false);
    setResizable(true, true);
    setResizeLimits(180, 180, 2400, 2000);
    {
        const int w = 360, h = 640;
        auto area = juce::Desktop::getInstance()
                        .getDisplays().getPrimaryDisplay()->userArea;
        setBounds(area.getRight() - w,
                  area.getY() + juce::jmax(0, (area.getHeight() - h) / 2),
                  w, h);
    }
    setUsingNativeTitleBar(true);
    setAlwaysOnTop(true);
    setVisible(true);

    policy_ = loadPolicyFromSettings();

   #if JUCE_WINDOWS
    s_hotkeyOwner = this;
    registerGlobalHotkey();
   #endif

    startTimer(kPollMs);
}

BeatMateLiveWindow::~BeatMateLiveWindow()
{
    stopTimer();

   #if JUCE_WINDOWS
    unregisterGlobalHotkey();
    if (s_hotkeyOwner == this)
        s_hotkeyOwner = nullptr;
   #endif

    if (m_isShrunk)
        applyPolicyExit();
}

void BeatMateLiveWindow::closeButtonPressed()
{
    stopTimer();
    setVisible(false);
    floatingBadge_.reset();
    hideTrayIcon();
    m_isShrunk   = false;
    m_shrinkKind = ShrinkKind::None;

    if (savedMainWindow_ != nullptr) {
        if (auto* mainDoc = dynamic_cast<juce::DocumentWindow*>(savedMainWindow_.getComponent()))
            mainDoc->setMinimised(false);
        savedMainWindow_->setVisible(true);
        savedMainWindow_->toFront(true);
        savedMainWindow_ = nullptr;
    }

    if (onCloseCallback)
        onCloseCallback();
}

void BeatMateLiveWindow::setCompactMode(bool compact)
{
    if (compact)
    {
        setResizeLimits(180, 180, 1200, 1400);
        centreWithSize(200, 200);
    }
    else
    {
        setResizeLimits(720, 500, 2400, 2000);
        centreWithSize(700, 900);
    }
}

void BeatMateLiveWindow::mouseDown(const juce::MouseEvent& e)
{
    m_userInteracting = true;
    m_mouseLeftAtMs   = 0.0;
    if (m_isShrunk)
        expandToFull();
    juce::DocumentWindow::mouseDown(e);
}

void BeatMateLiveWindow::setAutoHidePolicy(AutoHidePolicy policy)
{
    if (policy == policy_) return;

    if (m_isShrunk)
        applyPolicyExit();

    policy_ = policy;
    savePolicyToSettings();

    if (djForeground_ && policy_ != AutoHidePolicy::Disabled)
        applyPolicyEnter();
}

BeatMateLiveWindow::AutoHidePolicy
BeatMateLiveWindow::loadPolicyFromSettings() const
{
    if (!g_serviceLocator) return AutoHidePolicy::Fade;
    auto* sm = g_serviceLocator->tryGet<Services::Config::SettingsManager>();
    if (!sm) return AutoHidePolicy::Fade;
    auto s = juce::String(sm->get<std::string>(kSettingsKey, std::string("Fade")));
    return policyFromString(s);
}

void BeatMateLiveWindow::savePolicyToSettings() const
{
    if (!g_serviceLocator) return;
    auto* sm = g_serviceLocator->tryGet<Services::Config::SettingsManager>();
    if (!sm) return;
    sm->set<std::string>(kSettingsKey, policyToString(policy_).toStdString());
}

bool BeatMateLiveWindow::detectDJForeground()
{
   #if JUCE_WINDOWS
    HWND fg = ::GetForegroundWindow();
    if (!fg) return false;

    if (auto* peer = getPeer())
    {
        if ((HWND) peer->getNativeHandle() == fg) return false;
    }

    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        if (auto* c = desktop.getComponent(i))
            if (c->getWindowHandle() == (void*) fg) return false;
    }

    std::string exeLower;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(fg, &pid);
    if (pid != 0)
    {
        if (HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] {};
            DWORD sz = MAX_PATH;
            if (::QueryFullProcessImageNameW(h, 0, path, &sz))
            {
                std::wstring w(path);
                auto slash = w.find_last_of(L"\\/");
                if (slash != std::wstring::npos) w = w.substr(slash + 1);
                exeLower = juce::String(juce::CharPointer_UTF16(
                    reinterpret_cast<const juce::CharPointer_UTF16::CharType*>(w.c_str())))
                        .toLowerCase().toStdString();
            }
            ::CloseHandle(h);
        }
    }

    wchar_t title[512] {};
    ::GetWindowTextW(fg, title, 511);
    std::string titleLower = juce::String(title).toLowerCase().toStdString();

    constexpr size_t nProc  = sizeof(kDJProcessNames)    / sizeof(kDJProcessNames[0]);
    constexpr size_t nTitle = sizeof(kDJTitleSubstrings) / sizeof(kDJTitleSubstrings[0]);
    return matchesAny(exeLower, kDJProcessNames, nProc)
        || matchesAny(titleLower, kDJTitleSubstrings, nTitle);
   #elif defined(__APPLE__)
    juce::ChildProcess cp;
    juce::StringArray args { "/usr/bin/osascript", "-e",
        "tell application \"System Events\" to get name of first application process whose frontmost is true" };
    if (! cp.start(args))
        return false;
    const auto front = cp.readAllProcessOutput().trim().toLowerCase();
    if (front.isEmpty() || front.contains("beatmate"))
        return false;
    const std::string frontStd = front.toStdString();
    constexpr size_t nTitle = sizeof(kDJTitleSubstrings) / sizeof(kDJTitleSubstrings[0]);
    return matchesAny(frontStd, kDJTitleSubstrings, nTitle);
   #else
    return false;
   #endif
}

void BeatMateLiveWindow::onDJForegroundEntered()
{
    if (policy_ == AutoHidePolicy::Disabled) return;
    if (m_isShrunk) return;
    applyPolicyEnter();
}

void BeatMateLiveWindow::onDJForegroundExited()
{
    if (m_isShrunk)
        applyPolicyExit();
}

void BeatMateLiveWindow::applyPolicyEnter()
{
    savedFullBounds   = getBounds();
    savedAlwaysOnTop_ = isAlwaysOnTop();

    switch (policy_)
    {
        case AutoHidePolicy::Disabled:
            return;

        case AutoHidePolicy::Fade:
            setAlpha(0.55f);
            m_shrinkKind = ShrinkKind::Fade;
            break;

        case AutoHidePolicy::MinimizeToTray:
            setMinimised(true);
            m_shrinkKind = ShrinkKind::MinimizeToTray;
            break;

        case AutoHidePolicy::CompactDock:
        {
            // Anchor to the right edge so Live never covers the DJ app
            auto area = juce::Desktop::getInstance().getDisplays()
                                    .getPrimaryDisplay()->userArea;
            const int w = 220, h = 96;
            setBounds(area.getRight() - w,
                      area.getY(),
                      w, h);
            setAlwaysOnTop(true);
            m_shrinkKind = ShrinkKind::CompactDock;
            break;
        }

        case AutoHidePolicy::FloatingIcon:
        {
            setVisible(false);
            ensureFloatingBadge();
            m_shrinkKind = ShrinkKind::FloatingIcon;
            break;
        }
    }

    m_isShrunk        = true;
    m_userInteracting = false;
    m_mouseLeftAtMs   = 0.0;
    m_animatingAlpha  = false;

    onAutoHideApplied(m_shrinkKind);
    reapplyAutoHide = [this]
    {
        if (!m_isShrunk && djForeground_)
            applyPolicyEnter();
    };

    ensureTrayIconVisible();
}

void BeatMateLiveWindow::applyPolicyExit()
{
    switch (m_shrinkKind)
    {
        case ShrinkKind::None:
            break;

        case ShrinkKind::Fade:
            setAlpha(1.0f);
            break;

        case ShrinkKind::MinimizeToTray:
            setMinimised(false);
            toFront(true);
            break;

        case ShrinkKind::CompactDock:
            setAlwaysOnTop(savedAlwaysOnTop_);
            if (!savedFullBounds.isEmpty())
                setBounds(savedFullBounds);
            break;

        case ShrinkKind::FloatingIcon:
            hideFloatingBadge();
            if (!savedFullBounds.isEmpty())
                setBounds(savedFullBounds);
            setVisible(true);
            toFront(true);
            break;
    }

    m_isShrunk        = false;
    m_shrinkKind      = ShrinkKind::None;
    m_userInteracting = false;
    m_mouseLeftAtMs   = 0.0;
    m_animatingAlpha  = false;
    onAutoHideCleared();
    hideTrayIcon();
}

void BeatMateLiveWindow::ensureTrayIconVisible()
{
    if (!trayIcon_)
        trayIcon_ = std::make_unique<LiveTrayIcon>(*this);
}

void BeatMateLiveWindow::hideTrayIcon()
{
    trayIcon_.reset();
}

void BeatMateLiveWindow::ensureFloatingBadge()
{
    // Idempotent: re-assert every invariant Windows may have broken
    if (floatingBadge_) {
        if (!floatingBadge_->isOnDesktop())
            floatingBadge_->addToDesktop(floatingBadge_->getDesktopWindowStyleFlags());
        floatingBadge_->setAlwaysOnTop(true);
        floatingBadge_->setVisible(true);
        floatingBadge_->toFront(false);
        return;
    }
    floatingBadge_ = std::make_unique<FloatingBadge>(*this);
    if (floatingBadge_) {
        floatingBadge_->setAlwaysOnTop(true);
        floatingBadge_->setVisible(true);
        floatingBadge_->toFront(false);
    }
}

void BeatMateLiveWindow::hideFloatingBadge()
{
    // Hide only — destroying the peer causes a recreate storm
    if (floatingBadge_) floatingBadge_->setVisible(false);
}

// State mirroring only; do NOT stop the timer here
void BeatMateLiveWindow::onAutoHideApplied(ShrinkKind kind)
{
    m_isShrunk        = true;
    m_shrinkKind      = kind;
    m_userInteracting = false;
    m_mouseLeftAtMs   = 0.0;
}

void BeatMateLiveWindow::onAutoHideCleared()
{
    m_isShrunk        = false;
    m_shrinkKind      = ShrinkKind::None;
    m_animatingAlpha  = false;
    m_userInteracting = false;
    m_mouseLeftAtMs   = 0.0;
}

void BeatMateLiveWindow::expandToFull()
{
    if (m_shrinkKind == ShrinkKind::CompactDock && !savedFullBounds.isEmpty())
        setBounds(savedFullBounds);

    if (m_shrinkKind == ShrinkKind::MinimizeToTray)
        setMinimised(false);

    {
        auto area = juce::Desktop::getInstance()
                        .getDisplays().getPrimaryDisplay()->userArea;
        const int w = juce::jmin(720, area.getWidth() - 40);
        const int h = juce::jmin(960, area.getHeight() - 60);
        setBounds(area.getRight() - w,
                  area.getY() + (area.getHeight() - h) / 2,
                  w, h);
    }

    // Hide the badge, do NOT reset() it (peer recreate storm)
    if (floatingBadge_) floatingBadge_->setVisible(false);

    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i) {
        auto* w = juce::TopLevelWindow::getTopLevelWindow(i);
        if (!w || w == this) continue;
        const auto name = w->getName();
        if (name.startsWithIgnoreCase("BeatMate V12")
            || (name.startsWithIgnoreCase("BeatMate")
                && !name.containsIgnoreCase("Live"))) {
            savedMainWindow_ = w;
            if (auto* mainDoc = dynamic_cast<juce::DocumentWindow*>(w))
                mainDoc->setMinimised(true);
            else
                w->setVisible(false);
            break;
        }
    }

    setVisible(true);

    m_expandStartTime = juce::Time::getCurrentTime();
    m_animatingAlpha  = true;

   #if JUCE_WINDOWS
    if (auto* peer = getPeer())
    {
        HWND hwnd = (HWND) peer->getNativeHandle();
        if (hwnd)
        {
            INPUT ip = {};
            ip.type = INPUT_KEYBOARD;
            ip.ki.wVk = VK_MENU;
            SendInput(1, &ip, sizeof(INPUT));
            ip.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &ip, sizeof(INPUT));
            AllowSetForegroundWindow(ASFW_ANY);
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            ShowWindow(hwnd, SW_MINIMIZE);
            ShowWindow(hwnd, SW_RESTORE);
            BringWindowToTop(hwnd);
            SetForegroundWindow(hwnd);
            SwitchToThisWindow(hwnd, TRUE);
        }
    }
   #else
    setAlwaysOnTop(true);
    toFront(true);
    setAlwaysOnTop(false);
   #endif

    // Force on top for a frame so Windows actually promotes it above the DJ app
    setAlwaysOnTop(true);
    toFront(true);
    juce::Timer::callAfterDelay(120, [weak = juce::Component::SafePointer<juce::Component>(this)]() {
        if (auto* w = dynamic_cast<BeatMateLiveWindow*>(weak.getComponent()))
            w->setAlwaysOnTop(false);
    });

    m_isShrunk        = false;
    m_userInteracting = false;
    m_mouseLeftAtMs   = 0.0;
}

bool BeatMateLiveWindow::isMouseNearWindow(int fringePx) const
{
    const auto mouse  = juce::Desktop::getInstance().getMousePosition();
    const auto bounds = getScreenBounds().expanded(fringePx);
    return bounds.contains(mouse);
}

void BeatMateLiveWindow::timerCallback()
{
    const bool dj = detectDJForeground();
    if (dj && !djForeground_)
    {
        djForeground_ = true;
        onDJForegroundEntered();
    }
    else if (!dj && djForeground_)
    {
        djForeground_ = false;
        onDJForegroundExited();
    }

    if (m_animatingAlpha)
    {
        const double elapsed =
            (juce::Time::getCurrentTime() - m_expandStartTime).inMilliseconds();
        const float t = juce::jlimit(0.0f, 1.0f,
                                     (float) (elapsed / (double) kAlphaFadeMs));
        setAlpha(juce::jmap(t, 0.55f, 1.0f));
        if (t >= 1.0f) m_animatingAlpha = false;
    }

    // While shrunk, no auto-expand on hover: reopen only via the badge
    if (m_isShrunk) return;

    if (isVisible() && !m_userInteracting)
    {
        const bool overUs = isMouseNearWindow(kFringePx);
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (overUs) {
            m_mouseLeftAtMs = 0.0;
        } else {
            if (m_mouseLeftAtMs == 0.0) m_mouseLeftAtMs = nowMs;
            else if ((nowMs - m_mouseLeftAtMs) > 900.0) {
                m_mouseLeftAtMs = 0.0;
                setVisible(false);
                ensureFloatingBadge();
                return;
            }
        }
    }

    if (m_userInteracting)         return;
    if (m_shrinkKind == ShrinkKind::None) return;
    if (!djForeground_)            return;

    const bool overUs = isMouseNearWindow(kFringePx);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (overUs)
    {
        m_mouseLeftAtMs = 0.0;
        return;
    }
    if (m_mouseLeftAtMs == 0.0)
    {
        m_mouseLeftAtMs = nowMs;
        return;
    }
    if ((nowMs - m_mouseLeftAtMs) < (double) kReHideDelayMs)
        return;

    if (reapplyAutoHide)
    {
        m_mouseLeftAtMs = 0.0;
        reapplyAutoHide();
    }
}

void BeatMateLiveWindow::registerGlobalHotkey()
{
   #if JUCE_WINDOWS
    if (hotkeyRegistered_) return;
    auto t = std::make_unique<HotkeyThread>(hotkeyId_);
    t->startThread();
    hotkeyThread_ = std::move(t);
    hotkeyRegistered_ = true;
   #endif
}

void BeatMateLiveWindow::unregisterGlobalHotkey()
{
   #if JUCE_WINDOWS
    if (!hotkeyRegistered_) return;
    if (auto* t = static_cast<HotkeyThread*>(hotkeyThread_.get()))
        t->stop();
    hotkeyThread_.reset();
    hotkeyRegistered_ = false;
   #endif
}

void BeatMateLiveWindow::toggleVisibilityAndForeground()
{
    if (!isVisible() || isMinimised())
        expandToFull();
    else
        setVisible(false);
}

} // namespace BeatMate::UI
