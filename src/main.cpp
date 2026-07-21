
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Application.h"
#include "app/NativeSplash.h"
#include "ui/styles/BeatMateLookAndFeel.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>

// Windows minidump-on-crash handler (diagnostic crash 139).
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #include <dbghelp.h>
 #pragma comment(lib, "dbghelp.lib")
 #pragma comment(lib, "Shell32.lib")

// Writes a minidump to %APPDATA%\BeatMate\crashdumps. Called from BOTH the
static std::atomic<bool> g_dumpWritten{false};
static void beatmateWriteMinidump(EXCEPTION_POINTERS* ex)
{
    if (g_dumpWritten.exchange(true)) return;

    {
        wchar_t ad[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, ad))) {
            wchar_t dir2[MAX_PATH];
            swprintf_s(dir2, L"%ls\\BeatMate\\crashdumps", ad);
            CreateDirectoryW(dir2, nullptr);
            SYSTEMTIME st2; GetLocalTime(&st2);
            wchar_t stack_path[MAX_PATH];
            swprintf_s(stack_path, L"%ls\\beatmate_%04d%02d%02d_%02d%02d%02d.stack.txt",
                       dir2, st2.wYear, st2.wMonth, st2.wDay,
                       st2.wHour, st2.wMinute, st2.wSecond);
            HANDLE hs = CreateFileW(stack_path, GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hs != INVALID_HANDLE_VALUE) {
                void* frames[64] = {};
                USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
                char line[256];
                int len = sprintf_s(line, "CaptureStackBackTrace N=%u\r\n", (unsigned) n);
                DWORD wb = 0; WriteFile(hs, line, len, &wb, nullptr);
                if (ex && ex->ExceptionRecord) {
                    len = sprintf_s(line, "ExceptionCode=0x%08lX  ExceptionAddr=0x%p  Param0=0x%p  Param1=0x%p\r\n",
                                    (unsigned long) ex->ExceptionRecord->ExceptionCode,
                                    (void*) ex->ExceptionRecord->ExceptionAddress,
                                    (void*) (uintptr_t) ex->ExceptionRecord->ExceptionInformation[0],
                                    (void*) (uintptr_t) ex->ExceptionRecord->ExceptionInformation[1]);
                    WriteFile(hs, line, len, &wb, nullptr);
                }
                for (USHORT i = 0; i < n; ++i) {
                    uintptr_t p = (uintptr_t) frames[i];
                    HMODULE mod = nullptr;
                    char modname[MAX_PATH] = "?";
                    uintptr_t rva = 0;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          (LPCSTR) frames[i], &mod) && mod != nullptr) {
                        char fullpath[MAX_PATH] = {};
                        if (GetModuleFileNameA(mod, fullpath, MAX_PATH) > 0) {
                            const char* slash = strrchr(fullpath, '\\');
                            const char* name = slash ? (slash + 1) : fullpath;
                            strncpy_s(modname, name, _TRUNCATE);
                        }
                        rva = p - (uintptr_t) mod;
                    }
                    len = sprintf_s(line, "  [%u] addr=0x%p  module=%s  rva=0x%llX\r\n",
                                    (unsigned) i, frames[i], modname, (unsigned long long) rva);
                    WriteFile(hs, line, len, &wb, nullptr);
                }
                CloseHandle(hs);
            }
        }
    }

    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        return;

    wchar_t dir[MAX_PATH];
    swprintf_s(dir, L"%ls\\BeatMate\\crashdumps", appdata);
    wchar_t parent[MAX_PATH];
    swprintf_s(parent, L"%ls\\BeatMate", appdata);
    CreateDirectoryW(parent, nullptr);
    CreateDirectoryW(dir, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%ls\\beatmate_%04d%02d%02d_%02d%02d%02d.dmp",
               dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ex;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h,
                          (MINIDUMP_TYPE)(MiniDumpWithDataSegs
                                        | MiniDumpWithFullMemory
                                        | MiniDumpWithHandleData
                                        | MiniDumpWithThreadInfo
                                        | MiniDumpWithProcessThreadData),
                          &mei, nullptr, nullptr);
        CloseHandle(h);
    }
}

static LONG CALLBACK beatmateVectoredHandler(EXCEPTION_POINTERS* ex)
{
    if (!ex || !ex->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ex->ExceptionRecord->ExceptionCode;
    const bool fatal =
        code == EXCEPTION_ACCESS_VIOLATION
     || code == EXCEPTION_STACK_OVERFLOW
     || code == EXCEPTION_INT_DIVIDE_BY_ZERO
     || code == EXCEPTION_ILLEGAL_INSTRUCTION
     || code == EXCEPTION_PRIV_INSTRUCTION
     || code == EXCEPTION_IN_PAGE_ERROR
     || code == 0xC0000374u; // STATUS_HEAP_CORRUPTION
    if (!fatal) return EXCEPTION_CONTINUE_SEARCH;
    beatmateWriteMinidump(ex);
    return EXCEPTION_CONTINUE_SEARCH; // let JUCE/OS still terminate the process
}

static LONG WINAPI beatmateCrashFilter(EXCEPTION_POINTERS* ex)
{
    beatmateWriteMinidump(ex);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

class BeatMateApplication : public juce::JUCEApplication
{
public:
    BeatMateApplication() = default;

    const juce::String getApplicationName() override
    {
        return "BeatMate V12";
    }

    const juce::String getApplicationVersion() override
    {
        return BEATMATE_VERSION;
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);

        setupLogging();

        spdlog::info("=========================================");
        spdlog::info("BeatMate V{} Professional - Starting...", BEATMATE_VERSION);
        spdlog::info("Build: {} {}", __DATE__, __TIME__);
        spdlog::info("Exe: {}", juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                                    .getFullPathName().toStdString());
        spdlog::info("JUCE version: {}", juce::SystemStats::getJUCEVersion().toStdString());
        spdlog::info("OS: {}", juce::SystemStats::getOperatingSystemName().toStdString());
        spdlog::info("CPU: {} cores", juce::SystemStats::getNumCpus());
        spdlog::info("RAM: {} MB", juce::SystemStats::getMemorySizeInMegabytes());
        spdlog::info("=========================================");

        m_lookAndFeel = std::make_unique<BeatMate::UI::BeatMateLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(m_lookAndFeel.get());

        // Native Win32 splash on its own thread — animates independently
        m_nativeSplash = std::make_unique<BeatMate::NativeSplash>();
        m_nativeSplash->show();

        juce::Timer::callAfterDelay(30, [this]() {
            try {
                m_app = std::make_unique<BeatMate::Application>();
                BeatMate::NativeSplash* splashPtr = m_nativeSplash.get();
                auto progress = [splashPtr](float pct, const char* label) {
                    if (!splashPtr) return;
                    juce::String s = juce::String::fromUTF8(label);
                    splashPtr->setProgress(pct, s.toWideCharPointer());
                };
                if (!m_app->initialize(progress)) {
                    spdlog::critical("Failed to initialize BeatMate application");
                    if (m_nativeSplash) m_nativeSplash->close();
                    m_nativeSplash.reset();
                    quit();
                    return;
                }
                m_app->show();
                if (m_nativeSplash) m_nativeSplash->finishAndClose(12);
                m_nativeSplash.reset();
                if (auto* mw = m_app->getMainWindow()) {
                    mw->toFront(true);
                    mw->repaint();
                }
                spdlog::info("BeatMate V{} ready", BEATMATE_VERSION);
            } catch (const std::exception& e) {
                spdlog::critical("STARTUP EXCEPTION (std::exception): {}", e.what());
                if (m_nativeSplash) { m_nativeSplash->close(); m_nativeSplash.reset(); }
               #if JUCE_WINDOWS
                try { beatmateWriteMinidump(nullptr); } catch (...) {}
               #endif
                throw;
            } catch (...) {
                spdlog::critical("STARTUP EXCEPTION (unknown type)");
                if (m_nativeSplash) { m_nativeSplash->close(); m_nativeSplash.reset(); }
               #if JUCE_WINDOWS
                try { beatmateWriteMinidump(nullptr); } catch (...) {}
               #endif
                throw;
            }
        });
    }

    void shutdown() override
    {
        spdlog::info("BeatMate V{} - Shutting down...", BEATMATE_VERSION);

        if (m_app) {
            m_app->shutdown();
            m_app.reset();
        }

        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        m_lookAndFeel.reset();

        spdlog::info("BeatMate V{} - Shutdown complete", BEATMATE_VERSION);
        spdlog::info("=========================================");
    }

    void systemRequestedQuit() override
    {
        spdlog::warn("[BeatMate] systemRequestedQuit called — quitting application");
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
        if (m_app) {
            m_app->show(); // bring existing window to front
        }
    }

private:
    void setupLogging()
    {
        auto logDir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("BeatMate").getChildFile("Logs");
        logDir.createDirectory();

        auto logPath = logDir.getChildFile("beatmate.log").getFullPathName().toStdString();

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath, 10 * 1024 * 1024, 5);

        auto logger = std::make_shared<spdlog::logger>("beatmate",
            spdlog::sinks_init_list{console_sink, file_sink});

        logger->set_level(spdlog::level::debug);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::warn);
        spdlog::flush_on(spdlog::level::critical);

        spdlog::info("spdlog configured successfully");
        spdlog::info("Logs directory: {}", logDir.getFullPathName().toStdString());
    }

    class SplashWindow : public juce::DocumentWindow
    {
    public:
        SplashWindow()
            : juce::DocumentWindow("BeatMate V12 - Chargement...",
                                    juce::Colour(0xFF08080A),
                                    juce::DocumentWindow::closeButton,
                                    true)
        {
            setUsingNativeTitleBar(true);
            setDropShadowEnabled(true);
            setResizable(false, false);

            auto* content = new SplashContent();
            setContentOwned(content, true);

            int w = 480, h = 280;
            auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            if (screen != nullptr) {
                auto bounds = screen->userArea;
                setBounds(bounds.getCentreX() - w/2, bounds.getCentreY() - h/2, w, h);
            } else {
                setBounds(400, 300, w, h);
            }
            setAlwaysOnTop(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        class SplashContent : public juce::Component, private juce::Timer
        {
        public:
            SplashContent() { startTimer(30); }
            ~SplashContent() override { stopTimer(); }
            void paint(juce::Graphics& g) override
            {
                auto bounds = getLocalBounds().toFloat();
                juce::ColourGradient gradient(juce::Colour(0xFF1E1B4B), 0, 0,
                                              juce::Colour(0xFF08080A), (float)getWidth(), (float)getHeight(), false);
                g.setGradientFill(gradient);
                g.fillRoundedRectangle(bounds, 12.0f);

                g.setColour(juce::Colour(0xFF6366F1).withAlpha(0.3f));
                g.drawRoundedRectangle(bounds.reduced(1.0f), 12.0f, 1.5f);

                g.setColour(juce::Colour(0xFFF1F5F9));
                g.setFont(juce::Font("Segoe UI", 36.0f, juce::Font::bold));
                g.drawText("BeatMate V12", bounds.removeFromTop(140).reduced(0, 20),
                           juce::Justification::centredBottom, false);

                g.setColour(juce::Colour(0xFF94A3B8));
                g.setFont(juce::Font("Segoe UI", 13.0f, juce::Font::plain));
                g.drawText("Professional DJ Suite", 0, 140, getWidth(), 20,
                           juce::Justification::centred, false);

                g.setColour(juce::Colour(0xFF6366F1));
                float centerX = (float)getWidth() / 2.0f;
                float dotY = 210.0f;
                for (int i = 0; i < 3; ++i) {
                    float alpha = 0.3f + 0.7f * std::abs(std::sin((m_phase + i * 0.4f)));
                    g.setColour(juce::Colour(0xFF6366F1).withAlpha(alpha));
                    g.fillEllipse(centerX - 20.0f + i * 16.0f, dotY, 8.0f, 8.0f);
                }

                g.setColour(juce::Colour(0xFF64748B));
                g.setFont(juce::Font("Segoe UI", 11.0f, juce::Font::plain));
                g.drawText("Chargement en cours...", 0, 235, getWidth(), 20,
                           juce::Justification::centred, false);
            }
            void timerCallback() override { m_phase += 0.1f; repaint(); }
        private:
            float m_phase = 0.0f;
        };
    };


    std::unique_ptr<BeatMate::Application> m_app;
    std::unique_ptr<BeatMate::UI::BeatMateLookAndFeel> m_lookAndFeel;
    std::unique_ptr<SplashWindow> m_splash;            // legacy JUCE splash, unused
    std::unique_ptr<BeatMate::NativeSplash> m_nativeSplash;
};

#if JUCE_WINDOWS
struct MinidumpInstaller {
    MinidumpInstaller() {
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);
        AddVectoredExceptionHandler(/*first=*/1, beatmateVectoredHandler);
        SetUnhandledExceptionFilter(beatmateCrashFilter);
    }
};
static MinidumpInstaller g_minidumpInstaller;
#endif

START_JUCE_APPLICATION(BeatMateApplication)
