#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 cps2recomp contributors. Part of cps2recomp.
"""MVC's hidden characters: Start held through both of player 1's picks gives a hidden character each time
(Chun-Li as Shadow Lady first, Morrigan as Lilith second; the second pick is taken at 00BE5E, not 00BCDA)."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
START, LP, RIGHT, DOWN = 0x400, 0x10, 2, 4
SHADOW_LADY, LILITH = 0x2A, 0x2C
FRAMES = 1000


def movie(path):
    rows = {0: 0, 600: START, 606: 0, 650: START}
    for frame, press in ((700, RIGHT), (740, LP),          # first pick: Ryu -> Chun-Li, taken with Start held
                         (830, RIGHT), (860, DOWN), (900, LP)):  # second: Ryu -> Chun-Li -> Morrigan
        rows[frame] = START | press
        rows[frame + 4] = START
    rows[960] = 0
    path.write_text(''.join(f'{f} {p:04x} 0000 0000 0000\n' for f, p in sorted(rows.items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=ROOT / 'build/mvsc/mvsc')
    parser.add_argument('--rom', type=Path, default=ROOT / 'rom/mvsc')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mvc-hidden-') as tmp:
        work = Path(tmp)
        movie(work / 'input.txt')
        result = subprocess.run([args.program.resolve(), '--rom', args.rom.resolve(), '--headless', '--frames', str(FRAMES),
                                 '--movie', 'input.txt', '--features', 'free_play,hidden_characters', '--settings',
                                 'settings.ini', '--eeprom', 'eeprom', '--trace', 'FF40B0-FF40B3:trace'],
                                cwd=work, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stderr or f'game exited {result.returncode}')
        last = (work / 'trace').read_bytes()[-4:]
        picks = (int.from_bytes(last[0:2], 'big'), int.from_bytes(last[2:4], 'big'))
        if picks != (SHADOW_LADY, LILITH):
            raise SystemExit(f'FAIL: player 1 picked {picks[0]:02X} and {picks[1]:02X}, '
                             f'expected {SHADOW_LADY:02X} (Shadow Lady) and {LILITH:02X} (Lilith)')
        print('PASS: Shadow Lady and Lilith, both picked with Start held')


if __name__ == '__main__':
    main()
