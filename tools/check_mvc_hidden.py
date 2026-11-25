#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 cps2recomp contributors. Part of cps2recomp.
"""MVC's hidden characters, both ways. Hold Start: Start held through both of player 1's picks gives a hidden character
each time (Chun-Li as Shadow Lady first, Morrigan as Lilith second; the second pick is taken at 00BE5E, not 00BCDA).
Beside the grid: with no Start held, player 1 moves up from Ryu to Orange Hulk, then right of Mega Man to Roll, and
player 2 down from Gambit to Shadow Lady, then down from War Machine to Lilith; and the six faces are drawn in their
places (the fight's life-bar faces: tile, place), which Hold Start does not draw."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
START, LP, RIGHT, LEFT, DOWN, UP = 0x400, 0x10, 1, 2, 4, 8
SHADOW_LADY, LILITH, ORANGE_HULK, ROLL = 0x2A, 0x2C, 0x26, 0x10
FRAMES = 1400
FACES_AT = 690  # (the screen down, no one moved yet)
FACES = {(128, 0): 0x6ECC, (176, 0): 0x6EC6, (224, 0): 0x6EC2,     # Red Venom, Orange Hulk, Gold War Machine
         (128, 192): 0x6EE6, (176, 192): 0x6EEE, (272, 64): 0x6EE0}  # Shadow Lady, Lilith, Roll


def faces(objects):
    """The faces among the frame's objects: (x, y) -> tile, for 2-by-2 objects at the places"""
    found = {}
    for i in range(0, len(objects), 8):
        x, y, tile, attributes = (int.from_bytes(objects[i + k:i + k + 2], 'big') for k in (0, 2, 4, 6))
        if tile == 0x8000 and attributes == 0x8000:
            break
        place = ((x & 0x3FF) - 64, (y & 0x3FF) - 16)
        if place in FACES and attributes & 0xFF00 == 0x1100:
            found[place] = tile
    return found


def write(path, players):
    frames = sorted(set().union(*players))
    def at(rows, f):
        return rows[max(k for k in rows if k <= f)]
    path.write_text(''.join(f'{f} {at(players[0], f):04x} {at(players[1], f):04x} 0000 0000\n' for f in frames))


def presses(rows, held, steps):
    for frame, press in steps:
        rows[frame] = held | press
        rows[frame + 4] = held


def hold_start(path):
    rows = {0: 0, 600: START, 606: 0, 650: START}
    presses(rows, START, ((700, LEFT), (740, LP),                   # first pick: Ryu -> Chun-Li, Start held
                          (830, LEFT), (860, DOWN), (900, LP)))     # second: Ryu -> Chun-Li -> Morrigan
    rows[960] = 0
    write(path, ({**rows}, {0: 0}))
    return 'free_play,hidden_characters=1', {0: (SHADOW_LADY, LILITH)}


def beside_grid(path):
    p1 = {0: 0, 600: START, 606: 0}
    p2 = {0: 0, 640: START, 646: 0}
    presses(p1, 0, ((700, UP), (740, LP),                           # Ryu up: Orange Hulk
                    (830, RIGHT), (860, DOWN), (890, RIGHT), (920, LP)))  # Ryu right Zangief, down Mega Man, right Roll
    # (player 2 chooses once player 1 has: the board takes no moves from the one player for a while after the other's
    # pick)
    presses(p2, 0, ((975, DOWN), (995, DOWN), (1015, LEFT), (1035, DOWN), (1055, LP),  # Spider-Man down Venom, down
                    # War Machine, left Gambit, down Shadow Lady; then (from Spider-Man again) down, down, down Lilith
                    (1200, DOWN), (1230, DOWN), (1260, DOWN), (1290, LP)))
    write(path, (p1, p2))
    return 'free_play,hidden_characters=2', {0: (ORANGE_HULK, ROLL), 1: (SHADOW_LADY, LILITH)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=ROOT / 'build/mvsc/mvsc')
    parser.add_argument('--rom', type=Path, default=ROOT / 'rom/mvsc')
    args = parser.parse_args()
    for name, make in (('Hold Start', hold_start), ('Beside the grid', beside_grid)):
        with tempfile.TemporaryDirectory(prefix='mvc-hidden-') as tmp:
            work = Path(tmp)
            features, wanted = make(work / 'input.txt')
            result = subprocess.run([args.program.resolve(), '--rom', args.rom.resolve(), '--headless', '--frames',
                                     str(FRAMES + 100), '--movie', 'input.txt', '--features', features, '--settings',
                                     'settings.ini', '--eeprom', 'eeprom', '--trace', 'FF40B0-FF40B7:trace',
                                     '--dump-ram', f'{FACES_AT}:at'],
                                    cwd=work, capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(result.stderr or f'game exited {result.returncode}')
            last = (work / 'trace').read_bytes()[-8:]
            for side, picks in wanted.items():
                got = tuple(int.from_bytes(last[4 * side + 2 * n:4 * side + 2 * n + 2], 'big') for n in range(2))
                if got != picks:
                    raise SystemExit(f'FAIL {name}: player {side + 1} picked {got[0]:02X} and {got[1]:02X}, '
                                     f'expected {picks[0]:02X} and {picks[1]:02X}')
            drawn = faces((work / 'at.objects').read_bytes())
            if drawn != (FACES if make is beside_grid else {}):
                raise SystemExit(f'FAIL {name}: faces drawn {sorted(drawn.items())}')
            print(f'PASS {name}: ' + ', '.join(f'player {s + 1} {p[0]:02X} {p[1]:02X}' for s, p in wanted.items()) +
                  (', the six faces drawn' if drawn else ', no faces drawn'))


if __name__ == '__main__':
    main()
