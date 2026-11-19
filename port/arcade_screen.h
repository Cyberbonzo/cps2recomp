// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
//
// An arcade monitor's screen (the launcher's Arcade monitor page): a mode made at the board's exact rate with
// Switchres (third_party/switchres, Windows), and the screen's refreshes as they happen, for the frame delay of a game
// synced to the screen (frontend_sdl.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace port {

// A mode Switchres made and switched the desktop to
struct MadeMode {
    int width = 0, height = 0;
    double vfreq = 0, hfreq = 0; // (hertz)
    bool exact = false;          // its refresh is the one asked for (else the nearest the monitor's ranges allow)
};
// Whether this build has Switchres (Windows)
bool SwitchresBuilt();
// The desktop of the screen `screen` (0 the main one, then the order the system lists them) switched to a mode of
// `width` x `height` at `rate` hertz, made by Switchres for the monitor `monitor` (one of Switchres's presets: arcade_15,
// generic_15...); false and `error` if it could not be
bool SwitchresMake(int screen, const char *monitor, int width, int height, double rate, MadeMode &made, std::string &error);
// Whether the desktop is in a mode Switchres made
bool SwitchresActive();
// The desktop as it was, and Switchres's modes taken back (at the end, or when the Arcade monitor page goes back to off)
void SwitchresRestore();

// The screen's refreshes, as the system says they happen (Windows: D3DKMTWaitForVerticalBlankEvent, on a thread of its
// own). Started for the screen a window is on; elsewhere, or where the system can't say, never Valid
class RefreshClock {
public:
    ~RefreshClock();
    void Start(SDL_Window *window);
    void Stop();
    // The time (SDL_GetTicksNS) of the refresh that follows `when`, and the time between refreshes; false while the
    // clock has seen too few of them to say
    bool Next(uint64_t when, uint64_t &next, double &period) const;

private:
    struct Impl;
    [[maybe_unused]] Impl *m_impl = nullptr;
};

} // namespace port
