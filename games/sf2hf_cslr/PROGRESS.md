# Street Fighter II: Hyper Fighting CSL Remix Edition: progress

Corner Store Legends (CSL) Remix Edition (ROM set `sf2hf_cslr`), a community balance mod of *Street Fighter II′: Hyper
Fighting* (World 921209, `sf2hf`), on the classic CPS-1 board: the first CSL mod, whose extras the
[CSL Edition](../sf2hf_csl/PROGRESS.md) carried on. Built from Super Street Fighter II Turbo's sources (`sources = ssf2t`)
with `sf2hf`'s `family.h` mapping, as the CSL Edition is. In the launcher it is a mod of Hyper Fighting (`mod_of = sf2hf`).

## What is this game's own

Compared byte for byte with the CSL Edition's set:

- **The same**: every graphics ROM, the Z80 program and the OKI samples, the PALs, and `s2te_21.6f`.
- **Program**: `s2te_23.8f` differs in 256 bytes (hooks, and the move tables at 042C00-0445FF: damage, reach, dizzy
  points) and `s2te_22.7f` in 5,332 (hit and hurt boxes at 0848xx-0C67xx, and the Remix's own code at 0F0000-0F0FFF in
  place of the CSL Edition's there).
- **Every address `family.h`, `game.cfg` and `src/csl.cpp` name** (170) has the same bytes around it as in the CSL
  Edition, but one: 084A16, Hyper Fighting's stun limit (`cmpi.w #$1F,$5E(a6)`). The Remix puts `jsr $F00C2` there,
  which takes the limit from a table at 0F00F2 (30, 34, 35: Super Turbo's) by a byte of the player's, 2FD, that steps by
  two each time the player is stunned (to 4 at most; cleared every round). `family.h` places Super Turbo's stun table
  (07F1C6) at 0F00F2 and names 2FD (`SSF2T_FAMILY_STUN_TIMES`), so training's stun readout shows the limit the Remix
  uses; the other games of the family build as before.
- **Translated code**: seeded from the CSL Edition's `entries.txt` and `entries.txt.z80`; `make GAME=sf2hf_cslr explore`
  found nothing more (3,333 68000 entries, 104 Z80).

## The checks

- `check` over the ladder (44,000 frames, `free_play,no_damage,one_hit`) and the versus movie (12,000 frames): ok
  (translated code matches the interpreter; two runs alike). The movies are the CSL Edition's, which play the same here.
- `check-blocks` over the ladder (44,000 frames): 330,083,377 translated blocks checked against the interpreter, and
  48,951,616 of the sound driver's.
- `check-release FRAMES=40000`: ok (picture hash in the pull request).
- `check-rollback` (three ways, 400 rollbacks each; 0 of 3,000 frames over a frame's time) and `check-online` (2,992
  frames, matches the offline replay): ok.
- `check-rewrites`: CameraAcross, CameraUp and KeepIn over the ladder, versus and auto runs: 0 differed.
- `check-widescreen`: every stage painted at both ends (at most 10 unpainted columns); with invisible walls the game's
  memory matches the board's at every 4,000 frames of the ladder; 0 interpreter instructions.
- The stun readout in a training session reads `stun n/30` against the Remix's first limit (Hyper Fighting's read
  would have been the patched instruction's word).

## Next

- Needs hand playtesting by players who know the Remix.
