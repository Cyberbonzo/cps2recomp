#!/opt/homebrew/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# run_fight_state.sh (savable build; SAVEAT=frames saves fight_<frame> in the run dir) as run_fight.sh:
# run_fight.sh CORE SET NAME MOVIE FRAMES [NVRAM]: a fight on the board's model, from power-on with a full ROM load
# (and the settings memory, if given), the movie's input; work RAM and bus counts every frame
core=$1 set=$2 name=$3 movie=$4 frames=$5 nvram=$6
# (the movie and the settings memory as absolute paths: the run changes into its own folder)
movie=$(realpath "$movie") || exit 1
[ -n "$nvram" ] && { nvram=$(realpath "$nvram") || exit 1; }
source "$HOME/code/recomp/cps2-reference/env.sh"
J="$HOME/code/recomp/cps2-reference/jtcores"
d="$J/cores/$core/ver/$name"; mkdir -p "$d" && cd "$d" || exit 1
for e in dip mod; do [ -e "$J/rom/$set.$e" ] && cp "$J/rom/$set.$e" "$J/rom/$name.$e"; done
ln -sf "../../../../rom/$set.rom" rom.bin
cp "$movie" movie.txt
[ -n "$nvram" ] && cp "$nvram" nvram.bin
rm -f wram.bin buslog.txt sdram_bank?.bin
export VERILATOR_EXTRA_ARGS=--savable OPT_FAST="-O3 -march=native" OPT_SLOW="-O3 -march=native" OPT_GLOBAL="-O3 -march=native"
ms=$(( frames * 1000 * 100 / 5964 + 300 ))
SAVEAT=${SAVEAT:-} SAVEPREFIX=$PWD/fight NOFRAMES=1 WRAMLOG=1 BUSLOG=1 MOVIE=$PWD/movie.txt jtsim -verilator -load -time $ms > sim-fight.log 2>&1
echo "$name: $(wc -l < buslog.txt 2>/dev/null) frames"
