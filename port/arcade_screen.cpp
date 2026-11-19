// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
//
// arcade_screen.h: Switchres's exact-rate modes (GPL-2.0-or-later, downloaded at build time) and the screen's refresh clock, on Windows; elsewhere neither.
#include "arcade_screen.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define SR_WIN32_STATIC
#include <windows.h>
#include <switchres_wrapper.h> // (downloaded at build time: CMakeLists.txt)
#endif

namespace port {

#ifdef _WIN32

namespace {
    bool switchresUp = false;   // (sr_init done)
    bool switchresMade = false; // (the desktop in a mode it made)

    // (Switchres's own lines, in the game's log)
    void SwitchresSays(const char *format, ...) {
        va_list args;
        va_start(args, format);
        fputs("switchres: ", stderr);
        vfprintf(stderr, format, args);
        va_end(args);
    }
} // namespace

bool SwitchresBuilt() {
    return true;
}

bool SwitchresMake(int screen, const char *monitor, int width, int height, double rate, MadeMode &made, std::string &error) {
    SwitchresRestore();
    sr_init();
    switchresUp = true;
    sr_set_log_level(2);
    sr_set_log_callback_info(reinterpret_cast<void *>(&SwitchresSays));
    sr_set_log_callback_error(reinterpret_cast<void *>(&SwitchresSays));
    sr_set_log_callback_debug(reinterpret_cast<void *>(&SwitchresSays));
    sr_set_disp(-1); // (the settings every screen it adds starts from)
    sr_set_monitor(monitor);
    // (the board's picture is never interlaced or doubled: a mode of its own lines, or none)
    sr_set_option("interlace", "0");
    sr_set_option("doublescan", "0");
    // (the main screen as the system says; another by its number in the system's order)
    char which[8] = "auto";
    if (screen > 0 && screen < 10) {
        snprintf(which, sizeof which, "%d", screen);
    }
    if (sr_init_disp(screen > 0 ? which : "auto", nullptr) < 0) {
        error = "Switchres could not open the screen";
        SwitchresRestore();
        return false;
    }
    sr_mode mode{};
    if (!sr_switch_to_mode(width, height, rate, 0, &mode)) {
        error = "Switchres could not make or switch to the mode";
        SwitchresRestore();
        return false;
    }
    switchresMade = true;
    made.width = mode.width;
    made.height = mode.height;
    made.vfreq = mode.vfreq;
    made.hfreq = mode.hfreq;
    made.exact = !mode.is_refresh_off && std::fabs(mode.vfreq - rate) < 0.01;
    return true;
}

bool SwitchresActive() {
    return switchresMade;
}

void SwitchresRestore() {
    if (switchresUp) {
        sr_deinit(); // (the desktop's mode, and the modes it changed, as they were)
    }
    switchresUp = switchresMade = false;
}

// The refresh clock: a thread that waits for each refresh of the window's screen
namespace {
    using D3dkmtHandle = UINT;
    struct OpenAdapterFromHdc {
        HDC hdc;
        D3dkmtHandle adapter;
        LUID luid;
        UINT source;
    };
    struct WaitForVerticalBlank {
        D3dkmtHandle adapter;
        D3dkmtHandle device;
        UINT source;
    };
    struct CloseAdapter {
        D3dkmtHandle adapter;
    };
    using OpenFn = LONG(APIENTRY *)(OpenAdapterFromHdc *);
    using WaitFn = LONG(APIENTRY *)(const WaitForVerticalBlank *);
    using CloseFn = LONG(APIENTRY *)(const CloseAdapter *);

    // (shared with the thread, which may be waiting on a refresh still when the clock is stopped: it lets go of it)
    struct Refreshes {
        std::atomic<bool> stop{false};
        std::mutex lock;
        uint64_t last = 0;
        double period = 0;
        int seen = 0;
    };
} // namespace

struct RefreshClock::Impl {
    std::shared_ptr<Refreshes> refreshes;
};

RefreshClock::~RefreshClock() {
    Stop();
}

void RefreshClock::Start(SDL_Window *window) {
    Stop();
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    if (!gdi) {
        gdi = LoadLibraryA("gdi32.dll");
    }
    const auto open = gdi ? reinterpret_cast<OpenFn>(reinterpret_cast<void *>(GetProcAddress(gdi, "D3DKMTOpenAdapterFromHdc"))) : nullptr;
    const auto wait = gdi ? reinterpret_cast<WaitFn>(reinterpret_cast<void *>(GetProcAddress(gdi, "D3DKMTWaitForVerticalBlankEvent"))) : nullptr;
    const auto close = gdi ? reinterpret_cast<CloseFn>(reinterpret_cast<void *>(GetProcAddress(gdi, "D3DKMTCloseAdapter"))) : nullptr;
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!open || !wait || !close || !hwnd) {
        return;
    }
    MONITORINFOEXA info{};
    info.cbSize = sizeof info;
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        return;
    }
    HDC dc = CreateDCA(nullptr, info.szDevice, nullptr, nullptr);
    if (!dc) {
        return;
    }
    OpenAdapterFromHdc adapter{};
    adapter.hdc = dc;
    const LONG opened = open(&adapter);
    DeleteDC(dc);
    if (opened != 0) {
        fprintf(stderr, "sync to the screen: the screen's refreshes can't be followed (D3DKMT %ld)\n", opened);
        return;
    }
    m_impl = new Impl;
    m_impl->refreshes = std::make_shared<Refreshes>();
    std::thread([refreshes = m_impl->refreshes, adapter, wait, close] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        WaitForVerticalBlank each{adapter.adapter, 0, adapter.source};
        while (!refreshes->stop) {
            if (wait(&each) != 0) {
                break;
            }
            const uint64_t now = SDL_GetTicksNS();
            std::lock_guard<std::mutex> hold(refreshes->lock);
            if (refreshes->last) {
                const double between = static_cast<double>(now - refreshes->last);
                // (a refresh not waited for, the thread held up: not a period; the period an average of many)
                if (refreshes->period == 0) {
                    refreshes->period = between;
                } else if (between < refreshes->period * 1.5) {
                    refreshes->period += (between - refreshes->period) / 64;
                }
            }
            refreshes->last = now;
            refreshes->seen++;
        }
        CloseAdapter done{adapter.adapter};
        close(&done);
    }).detach();
}

void RefreshClock::Stop() {
    if (m_impl) {
        m_impl->refreshes->stop = true;
        delete m_impl;
        m_impl = nullptr;
    }
}

bool RefreshClock::Next(uint64_t when, uint64_t &next, double &period) const {
    if (!m_impl) {
        return false;
    }
    std::lock_guard<std::mutex> hold(m_impl->refreshes->lock);
    const Refreshes &r = *m_impl->refreshes;
    // (some refreshes seen, and the last not long ago: the thread is keeping up)
    if (r.seen < 30 || r.period <= 0 || when > r.last + static_cast<uint64_t>(r.period * 4)) {
        return false;
    }
    period = r.period;
    const double after = when > r.last ? static_cast<double>(when - r.last) : 0;
    next = r.last + static_cast<uint64_t>((std::floor(after / period) + 1) * period);
    return true;
}

#else

bool SwitchresBuilt() {
    return false;
}
bool SwitchresMake(int, const char *, int, int, double, MadeMode &, std::string &error) {
    error = "this build has no Switchres (Windows only)";
    return false;
}
bool SwitchresActive() {
    return false;
}
void SwitchresRestore() {}

struct RefreshClock::Impl {};
RefreshClock::~RefreshClock() {}
void RefreshClock::Start(SDL_Window *) {}
void RefreshClock::Stop() {}
bool RefreshClock::Next(uint64_t, uint64_t &, double &) const {
    return false;
}

#endif

} // namespace port
