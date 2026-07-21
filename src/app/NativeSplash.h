#pragma once


#include <atomic>
#include <thread>

namespace BeatMate {

class NativeSplash {
public:
    NativeSplash();
    ~NativeSplash();

    NativeSplash(const NativeSplash&) = delete;
    NativeSplash& operator=(const NativeSplash&) = delete;

    void show();
    void close();

    void setProgress(float pct, const wchar_t* label);

    void finishAndClose(int stepMs = 12);

private:
    std::thread       m_thread;
    std::atomic<bool> m_running { false };
    void*             m_hwnd = nullptr; // HWND, void* to keep windows.h out of header
};

} // namespace BeatMate
