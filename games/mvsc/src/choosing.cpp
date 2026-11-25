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
// there (an empty box for Roll; the others are under the title and the words below the grid), so their faces are
// drawn there, over the title and the words. Start held still works too.
#include "mvsc.h"
#include <vector>

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

    // Beside the grid, their faces in their places: each hidden character's face from the fight's life bars (its tile
    // by the table at 006AA6, 2 by 2; the others' faces are their other selves' drawn in their own colours, Roll's her
    // own) in its colours there (the first of the three palettes 1C7268's pointer gives), in the middle of its place,
    // a place being 48 by 32 like the grid's. The places are where the cursor's box goes for each (its objects with the
    // SELECT title at 136, 0, as it is once the screen has come down).
    struct Place {
        uint16_t character;
        int16_t x, y; // the place's top left, the title at 136, 0
    };
    constexpr Place kPlaces[] = {
        {kRedVenom, 120, 0},     {kOrangeHulk, 168, 0}, {kGoldWarMachine, 216, 0},
        {kShadowLady, 120, 192}, {kLilith, 168, 192},   {kRoll, 264, 64},
    };
    constexpr uint32_t kObjects = 0x708000;                       // the frame's objects, 8 bytes each (02499C)
    constexpr uint32_t kPalettes = 0x914000;                      // the objects' 32 palettes
    constexpr uint32_t kFaceTiles = 0x006AA6, kFaceColours = 0x1C7268;
    // The SELECT title: 7 by 2, palette 16, one of three tiles (coming down; then as it shines)
    constexpr uint16_t kTitleSize = 0x1616, kTitleTiles[] = {0xB616, 0xB630, 0xB637};
    // The cursors' boxes (palettes 0A, 0B): the box (filled: on the grid its face is over it, and shows its edge) and
    // the 1P or 2P on its corner, which goes over the face
    constexpr uint16_t kRingPalettes[] = {0x0A, 0x0B}, kRingTiles[] = {0xB5F0, 0xB620};
    constexpr uint16_t kBoxTiles[] = {0xB60B, 0xB60F, 0xB614, 0xB615};
    // A choosing record's step (its first byte): from 5 the special partner, the mode and the speed are chosen in a
    // box on the player's side (player 2's over Roll's place); at 11 the player is done, and with all done the screen
    // goes
    constexpr uint8_t kMenus = 5, kDone = 11;
    constexpr int kTitleX = 136, kTitleY = 0, kObjectX = 64, kObjectY = 16;
    // Theirs: palettes no object uses while the grid is shown (the cursors on every character, the special partner, the
    // mode and the speed chosen; the fight's life bars have 00-09 only after it)
    constexpr uint16_t kFacePalettes[] = {0x15, 0x18, 0x19, 0x1D, 0x05, 0x09};

    // 024AB4: the frame's objects all written (A4 where the list's end goes; the later over the earlier): the faces
    bool TheirFaces(M68K &c) {
        if (FeatureValue("hidden_characters") != kBesideGrid) {
            return false;
        }
        const uint8_t step[2] = {Ram8(kChoosing), Ram8(kChoosing + kChoosingSize)};
        if ((step[0] == 0 || step[0] >= kDone) && (step[1] == 0 || step[1] >= kDone) && (step[0] | step[1]) != 0) {
            return false; // (everyone done: the grid goes)
        }
        M68KBus &bus = c.bus;
        const uint32_t end = c.a[4] & 0xFFFFFF;
        if (end < kObjects || end - kObjects > 0x7000) {
            return false;
        }
        // The title (where the screen is: it comes down from above), and the boxes' 1P and 2P
        int titleX = 0, titleY = 0;
        uint16_t titleHigh[2] = {};
        bool title = false;
        std::vector<uint32_t> labels;
        for (uint32_t o = kObjects; o < end; o += 8) {
            const uint16_t x = bus.Read16(o), y = bus.Read16(o + 2), tile = bus.Read16(o + 4), attributes = bus.Read16(o + 6);
            if ((x | y | tile | attributes) == 0) {
                continue;
            }
            if (!title && (attributes & 0xFF1F) == kTitleSize && (x & 0x3FF) == kTitleX + kObjectX &&
                (tile == kTitleTiles[0] || tile == kTitleTiles[1] || tile == kTitleTiles[2])) {
                title = true;
                titleX = (x & 0x3FF) - kObjectX;
                titleY = static_cast<int16_t>((y & 0x3FF) << 6) / 64 - kObjectY;
                titleHigh[0] = x & 0xFC00;
                titleHigh[1] = y & 0xFC00;
            }
            const bool box = tile == kBoxTiles[0] || tile == kBoxTiles[1] || tile == kBoxTiles[2] || tile == kBoxTiles[3];
            if (((attributes & 0x1F) == kRingPalettes[0] || (attributes & 0x1F) == kRingPalettes[1]) && tile >= kRingTiles[0] &&
                tile <= kRingTiles[1] && !box) {
                labels.push_back(o);
            }
        }
        if (!title) {
            return false;
        }
        // The faces (over the boxes), then the boxes' 1P and 2P again over them, before the list's end
        uint32_t to = end;
        const int dx = titleX - kTitleX, dy = titleY - kTitleY;
        bool drawn[6] = {};
        for (int i = 0; i < 6; i++) {
            const Place &place = kPlaces[i];
            if (place.character == kRoll && step[1] >= kMenus) {
                continue; // (under player 2's box)
            }
            drawn[i] = true;
            const uint8_t *tile = ProgramOpcodes(kFaceTiles + place.character);
            const uint8_t *pointer = Program(kFaceColours + place.character * 2);
            const uint8_t *colours =
                Program((pointer[0] << 24 | pointer[1] << 16 | pointer[2] << 8 | pointer[3]) & 0xFFFFFF);
            for (uint32_t k = 0; k < 16; k++) {
                bus.Write16(kPalettes + kFacePalettes[i] * 32 + k * 2,
                            static_cast<uint16_t>(0xF000 | (colours[k * 2] << 8) | colours[k * 2 + 1]));
            }
            const int x = place.x + 8 + dx + kObjectX, y = place.y + dy + kObjectY;
            bus.Write16(to, static_cast<uint16_t>(titleHigh[0] | (x & 0x3FF)));
            bus.Write16(to + 2, static_cast<uint16_t>(titleHigh[1] | (y & 0x3FF)));
            bus.Write16(to + 4, static_cast<uint16_t>(tile[0] << 8 | tile[1]));
            bus.Write16(to + 6, static_cast<uint16_t>(0x1100 | kFacePalettes[i]));
            to += 8;
        }
        for (const uint32_t o : labels) { // (those on a face drawn)
            const int x = (bus.Read16(o) & 0x3FF) - kObjectX - dx;
            const int y = static_cast<int16_t>((bus.Read16(o + 2) & 0x3FF) << 6) / 64 - kObjectY - dy;
            bool on = false;
            for (int i = 0; i < 6; i++) {
                on |= drawn[i] && x >= kPlaces[i].x && x < kPlaces[i].x + 48 && y >= kPlaces[i].y && y < kPlaces[i].y + 32;
            }
            if (!on) {
                continue;
            }
            for (uint32_t k = 0; k < 8; k += 2) {
                bus.Write16(to + k, bus.Read16(o + k));
            }
            to += 8;
        }
        c.a[4] = (c.a[4] & 0xFF000000) | to;
        c.cycles += 400;
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
    AddHook(HookAt(0x024AB4), "TheirFaces", TheirFaces);
    TheGame().features.push_back({"hidden_characters", "Game", "Hidden characters",
                                  "The hidden characters with no code. Hold Start: hold Start as you choose War Machine for "
                                  "Gold War Machine, Hulk for Orange Hulk, Venom for Red Venom, Chun-Li for Shadow Lady, "
                                  "Morrigan for Lilith, or Mega Man for Roll. Beside the grid: move to their places as the "
                                  "board's codes open them, all open: above Chun-Li, Ryu and Zangief (Red Venom, Orange Hulk, "
                                  "Gold War Machine), below Gambit and War Machine (Shadow Lady, Lilith), right of Mega Man "
                                  "(Roll), their faces drawn there. Start held works too.",
                                  0, {"Off", "Hold Start", "Beside the grid"}});
}

} // namespace cps2recomp
