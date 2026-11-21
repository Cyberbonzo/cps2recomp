#!/opt/homebrew/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# run_ref.sh CORE SET FRAMES: the reference run for the cps2recomp board-timing comparison (docs/ARCADE_ACCURACY.md):
# power-on with no input, FRAMES frames, work RAM (wram.bin) and bus counts (buslog.txt) every frame
core=$1 set=$2 frames=${3:-6000}
source "$HOME/code/recomp/cps2-reference/env.sh"
d="$HOME/code/recomp/cps2-reference/jtcores/cores/$core/ver/$set"
mkdir -p "$d" && cd "$d" || exit 1
[ -e rom.bin ] || ln -sf "../../../../rom/$set.rom" rom.bin
if [ ! -d clean ]; then
    jtsim -verilator -load -time 200 > load.log 2>&1
    mkdir -p clean && cp sdram_bank?.bin clean/ || { echo "$set: load FAILED"; exit 1; }
fi
cp clean/sdram_bank?.bin .
rm -f wram.bin buslog.txt
ms=$(( frames * 1000 * 100 / 5964 + 200 ))
WRAMLOG=1 BUSLOG=1 QUICKEXTRA=64 jtsim -verilator -time $ms > sim-attract.log 2>&1
n=$(wc -l < buslog.txt 2>/dev/null || echo 0)
echo "$set: $n frames"
