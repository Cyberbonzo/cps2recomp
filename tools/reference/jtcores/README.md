# The FPGA cores as the reference

The board timing (docs/TIMING.md) is checked against jotego's [jtcores](https://github.com/jotego/jtcores)
(GPL-3.0-or-later) simulated with Verilator, at commit `bfced64`. What the comparison changes in it (kept as a patch
outside this repository, which holds no patch files):

- `modules/jtframe/verilator/test.cpp` (the harness):
  - `WRAMLOG=1`: the 68000's work RAM (64 KB) appended to `wram.bin` at each vertical blank;
  - `BUSLOG=1`: each frame's 68000 clocks, those the video DMA took, the board's waits, the SDRAM's debt, QSound
    waits and accesses;
  - `MOVIE=file`: a cps2recomp input movie played into the cabinet;
  - `PCTRACE=from:to`: every bus access of those frames, with its clock, to `pctrace.txt`; `PCLOG=addr,...`;
  - `RAWFRAMES=1`: each frame's picture, raw, in the simulation's own process;
  - `NOFRAMES=1`, and the frame dumps' forked converts reaped (unreaped, six runs at once filled the process table);
  - savestates: `SAVEAT=f1,f2,... SAVEPREFIX=p` writes `p_<frame>.vlt/.harness`, `LOADSTATE=p_<frame>` resumes from
    one, byte for byte (the build must be `--savable`: `VERILATOR_EXTRA_ARGS`, `bin/jtsim-funcs`; a state loads only
    into the build that made it, so later runs start that folder's `obj_dir/sim` directly, not `jtsim`).
- `modules/jtframe/verilator/sdram.{h,cpp}`: the SDRAM model's state saved and restored.
- `modules/jtframe/hdl/cpu/jtframe_68kdtack_cen.v`: the cycle recovery's counter wide enough for long holds.
- `cores/cps15/hdl/jtcps15_sound.v`: the QSound DSP always ready (the simulation has no DSP program).
- `cores/cps1/hdl/jtcps1_main.v`: `CPS2RECOMP_BOARD12` (`jtsim -d CPS2RECOMP_BOARD12`): the 68000 at 12 MHz with the
  DMA's bus requests kept, as Hyper Fighting's board is (jtcps1's own 12 MHz is its turbo, which drops them).

The scripts expect the checkout at `~/code/recomp/cps2-reference/jtcores` with jtframe's `setprj.sh` sourced
(`env.sh`) and the ROM files made by `jtframe mra`:

- `run_ref.sh CORE SET FRAMES`: power-on, no input (the attract mode);
- `run_fight.sh CORE SET NAME MOVIE FRAMES [NVRAM]`, `run_fight_state.sh` (the same, savable, `SAVEAT=`);
- `run_state.sh DIR FRAMES [VAR=value...]`, `run_state1.sh` (a preloaded CPS-2 or CPS-1 run, savable);
- `frames.py`, `loops.py`, `tick.py`: the core's `pctrace.txt` against the port's block trace (an experiment
  build's `CPS2RECOMP_TRACE`): each frame's events, each loop's period, one tick block by block.

The boot offsets, the same-fight method and the results: docs/ARCADE_ACCURACY.md.
