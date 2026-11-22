// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Street Fighter II': Hyper Fighting - Corner Store Legends (CSL) Edition: CSL-specific optional features.
//
// At 0F101E, CSL's colour-picker opener sets the countdown 0x2F2(a4) to 0x5A (90 frames, ~1.5 s) in 1-player mode,
// and only bumps it to 0x384 (900 frames, ~12-15 s) at 0F1030 when both players are active (0x64C(a5) and 0x94C(a5)).
// The select screen at 0BA5BA already holds the fighter's transition while 0x2F0(a4) is nonzero, so bumping 0x2F2(a4)
// to 0x384 at 0F1036 gives 1-player arcade mode the same colour-selection time as 2-player mode.
#include "ssf2t.h"

namespace ssf2t::family {

void RegisterOwnSources() {
    using namespace cps2recomp;
    TheGame().features.push_back(
        {"csl_colour_time", "Game", "1P colour select time",
         "Give 1-player arcade mode the same ~12 seconds to pick a character colour as 2-player mode (instead of ~1.5 seconds).",
         kFeatureEnhanced});
    AddHook(0x0F1036, "csl_colour_time", [](M68K &cpu) {
        if (FeatureOn("csl_colour_time")) {
            SetRam16(cpu.a[4] + 0x2F2, 0x0384);
        }
        return false;
    });
}

} // namespace ssf2t::family
