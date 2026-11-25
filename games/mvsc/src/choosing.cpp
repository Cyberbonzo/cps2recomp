// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy
//
// Marvel vs. Capcom: the hidden characters chosen with no code (the Game page's "Hidden characters"), two ways.
// Hold Start: Start held as a player chooses a character gives its hidden other self, where it has one (War Machine
// Gold War Machine, Hulk Orange Hulk, Venom Red Venom, Chun-Li Shadow Lady, Morrigan Lilith, Mega Man Roll). The
// choice is the game's own (a button pressed on the cursor's character takes it: 00BCDA for a player's first
// character, 00BE5E for the second), with the character changed as the game takes it.
// Beside the grid: the places the board's codes open, all open from the start, as the board's own flag (the byte at
// 0000C8, never set on a sold board) opens them: Red Venom above Chun-Li, Orange Hulk above Ryu, Gold War Machine
// above Zangief, Shadow Lady below Gambit, Lilith below War Machine, Roll right of Mega Man. The board draws no face
// there (an empty box for Roll; the others are under the title and the words below the grid): the large picture
// beside the grid shows who is chosen. Start held still works too.
#include "mvsc.h"

namespace cps2recomp {

namespace {
    using namespace mvsc;

    enum { kOff, kHoldStart, kBesideGrid }; // the feature's choices
    constexpr uint32_t kCodes = 0x19;      // byte of the choosing record: the codes entered, a bit each (00C752)
    constexpr uint8_t kAllPlaces = 0x40;   // ... all six places open (the cursor's moves by the table at 0E3194)

    constexpr uint32_t kCursor = 0x0C; // word of the choosing record: the character under the player's cursor
    bool g_start[2] = {false, false};  // each player's Start held this frame

    uint16_t Hidden(uint16_t who) {
        switch (who) {
        case kWarMachine: return kGoldWarMachine;
        case kHulk: return kOrangeHulk;
        case kVenom: return kRedVenom;
        case kChunLi: return kShadowLady;
        case kMorrigan: return kLilith;
        case kMegaMan: return kRoll;
        default: return who;
        }
    }

    // 00BCDA (first character) and 00BE5E (second): a player's choosing record (A4) takes the character under its cursor
    bool Chosen(M68K &c) {
        const uint32_t record = c.a[4] & 0xFFFFFF;
        if (record != kChoosing && record != kChoosing + kChoosingSize) {
            return false;
        }
        const int n = record == kChoosing ? 0 : 1;
        if (FeatureValue("hidden_characters") != kOff && g_start[n]) {
            SetRam16(record + kCursor, Hidden(Ram16(record + kCursor)));
        }
        return false;
    }
    // 00C1CA: a player's cursor moved (both picks: 00C276 calls it too), by the table its codes say
    bool CursorMove(M68K &c) {
        const uint32_t record = c.a[4] & 0xFFFFFF;
        if ((record == kChoosing || record == kChoosing + kChoosingSize) && FeatureValue("hidden_characters") == kBesideGrid) {
            SetRam8(record + kCodes, kAllPlaces);
        }
        return false;
    }
} // namespace

void ChoosingBeforeFrame(Machine &machine) {
    for (int n = 0; n < 2; n++) {
        g_start[n] = (machine.players[n] & kStart) != 0;
    }
}

void RegisterChoosing() {
    AddHook(HookAt(0x00BCDA), "Chosen", Chosen);
    AddHook(HookAt(0x00BE5E), "Chosen second", Chosen);
    AddHook(HookAt(0x00C1CA), "CursorMove", CursorMove);
    TheGame().features.push_back({"hidden_characters", "Game", "Hidden characters",
                                  "The hidden characters with no code. Hold Start: hold Start as you choose War Machine for "
                                  "Gold War Machine, Hulk for Orange Hulk, Venom for Red Venom, Chun-Li for Shadow Lady, "
                                  "Morrigan for Lilith, or Mega Man for Roll. Beside the grid: move to their places as the "
                                  "board's codes open them, all open: above Chun-Li, Ryu and Zangief (Red Venom, Orange Hulk, "
                                  "Gold War Machine), below Gambit and War Machine (Shadow Lady, Lilith), right of Mega Man "
                                  "(Roll); the large picture shows who is chosen. Start held works too.",
                                  0, {"Off", "Hold Start", "Beside the grid"}});
}

} // namespace cps2recomp
