#!/opt/homebrew/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# run_state.sh DIR FRAMES [env...]: a run of the board's model in jtcores/cores/cps2/ver/DIR from its clean/ SDRAM (a
# preloaded ROM, no input), built savable at -O3; extra VAR=value words are passed to the simulation
# (SAVEAT=n SAVEPREFIX=p, LOADSTATE=p, PCTRACE=a:b, MOVIE=file)
d=$1 frames=$2; shift 2
source "$HOME/code/recomp/cps2-reference/env.sh"
cd "$HOME/code/recomp/cps2-reference/jtcores/cores/cps2/ver/$d" || exit 1
[ -e rom.bin ] || ln -sf ../../../../rom/ssf2xjr1.rom rom.bin
cp clean/sdram_bank?.bin .
rm -f wram.bin buslog.txt
ms=$(( frames * 1000 * 100 / 5964 + 200 ))
export VERILATOR_EXTRA_ARGS=--savable OPT_FAST="-O3 -march=native" OPT_SLOW="-O3 -march=native" OPT_GLOBAL="-O3 -march=native"
env NOFRAMES=1 WRAMLOG=1 BUSLOG=1 QUICKEXTRA=64 "$@" jtsim -verilator -time $ms > sim-$frames.log 2>&1
echo "$d: $(wc -l < buslog.txt) frames"
