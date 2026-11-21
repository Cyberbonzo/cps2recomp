#!/opt/homebrew/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# run_state1.sh DIR SET FRAMES [env...]: run_state.sh for the CPS-1 core (cores/cps1/ver/DIR, clean/ SDRAM, rom SET)
d=$1 set=$2 frames=$3; shift 3
source "$HOME/code/recomp/cps2-reference/env.sh"
cd "$HOME/code/recomp/cps2-reference/jtcores/cores/cps1/ver/$d" || exit 1
[ -e rom.bin ] || ln -sf ../../../../rom/$set.rom rom.bin
cp clean/sdram_bank?.bin .
rm -f wram.bin buslog.txt
ms=$(( frames * 1000 * 100 / 5964 + 200 ))
export VERILATOR_EXTRA_ARGS=--savable OPT_FAST="-O3 -march=native" OPT_SLOW="-O3 -march=native" OPT_GLOBAL="-O3 -march=native"
env NOFRAMES=1 WRAMLOG=1 BUSLOG=1 "$@" jtsim -verilator -time $ms > sim-$frames.log 2>&1
echo "$d: $(wc -l < buslog.txt) frames"
