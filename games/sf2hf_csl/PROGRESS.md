# Street Fighter II: Hyper Fighting CSL Edition: progress

Corner Store Legends (CSL) Edition (ROM set `sf2hf_csl`), a community mod based on *Street Fighter II′: Hyper Fighting*
(World 921209, `sf2hf`), on the classic CPS-1 board. Added in response to issue #440 by building Super Street Fighter II
Turbo's sources (`sources = ssf2t`) with `sf2hf`'s `family.h` mapping.

## What is this game's own

Comparing the CSL ROM set byte for byte against `sf2hf` (World 921209):

- **Board & audio**: Identical classic CPS-1 board (`cps1-classic`), PALs, Z80 sound program (`s92_09.11a`) and OKI
  MSM6295 samples (`s92_18.11c`, `s92_19.12c`).
- **Program ROMs**: `s2te_23.8f` has 400 bytes patched (mostly hooks jumping into custom code at `0x0F0000..0x0FFFFF` in
  `s2te_22.7f`, which return to the original routines such as `0x013A32`), plus updated data/tables/palettes in
  `s2te_22.7f` (`0x0F0000..0x0FFFFF`) and `s2te_21.6f` (`0x100000..0x10FFFF`, `0x160000..0x16FFFF`).
- **Graphics ROMs**: All twelve `s92-*.3a..6c` chips have small custom edits (0.01%–0.22% per chip, including the title
  screen and sprite/font tweaks).
- **Compatibility with `sf2hf`**: All 47 `video_writes` and every code/RAM address in `family.h` match `sf2hf`.
- **Translated code**: Seeded from `sf2hf`'s `entries.txt` and `entries.txt.z80`, expanded with `make GAME=sf2hf_csl
  explore` and movie entry recording to 3,333 68000 entries (55,621 instructions in 15,025 blocks) and 104 Z80 entries
  (1,817 instructions in 367 blocks).

## The checks

- `check` over the ladder (44,000 frames, `free_play,no_damage,one_hit`; M. Bison beaten, credits rolled, back to title)
  and the versus movie (12,000 frames): ok (translated code matches interpreter; two runs alike; 0 interpreter
  instructions on both).
- `check-blocks` over the ladder (44,000 frames): 293,711,181 translated blocks checked against the interpreter, and
  47,379,996 of the sound driver's.
- `check-rewrites`: CameraAcross (32,780 calls on ladder), CameraUp (32,780), KeepIn (65,600), plus versus and auto
  runs: 0 differed.
- `check-widescreen`: all 12 stages painted at both ends (at most 14 unpainted columns, under the 20 allowed); with
  invisible walls the game's memory matches the board's at every 4,000 frames of the ladder; 0 interpreter instructions.
- `check-rollback` (three ways, 400 rollbacks each; 0 of 3,000 frames over a frame's time) and `check-online` (2,992
  frames, matches offline replay): ok.
- `check-release FRAMES=40000`: ok (picture hash in the pull request).
- `make check` and `tools/audit.sh`: ok.

## Next

- Needs hand playtesting by players familiar with CSL Edition.
- Its stages are not guests in the other games yet (same as `sf2hf`).
