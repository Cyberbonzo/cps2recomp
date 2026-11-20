#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
#
# The port against a simulated FPGA core of the board (docs/ARCADE_ACCURACY.md): each side's 68000 work RAM, 64 KB a
# frame at the start of each vertical blank, one file of frames after another (the core's from the jtcores harness's
# WRAMLOG; the port's from a run that writes the same).
#
#   compare.py track REF OURS [--skip LOW-HIGH ...]
#       Each reference frame matched to an identical frame of ours within 30 frames of the last match (60 for the
#       first): how many match, the offset (ours - reference) as it changes, and the stretches with no match. --skip
#       leaves out bytes that hold no game state (the stack; a byte that flips with where the frame's end catches a
#       routine), offsets into work RAM in hex.
#   compare.py clock FILE... [--at ADDR] [--frames N]
#       A fighting game's round clock (a BCD byte; Street Fighter II's FF8ABE in Hyper Fighting): how many frames
#       each of its seconds takes, over every whole second in the first N frames. The speed of the game itself,
#       whichever way the fight goes.
import argparse, hashlib, statistics
from collections import Counter

FRAME = 0x10000


def frames(path):
    data = open(path, 'rb').read()
    return [data[i * FRAME:(i + 1) * FRAME] for i in range(len(data) // FRAME)]


def parse_skips(skips):
    ranges = []
    for s in skips or []:
        low, _, high = s.partition('-')
        ranges.append((int(low, 16) & 0xFFFF, (int(high or low, 16) & 0xFFFF) + 1))
    return sorted(ranges)


def keyer(skips):
    def key(frame):
        parts, at = [], 0
        for low, high in skips:
            parts.append(frame[at:low])
            at = max(at, high)
        parts.append(frame[at:])
        return hashlib.md5(b''.join(parts)).hexdigest()
    return key


def stretches(numbers):
    out = []
    for n in numbers:
        if out and n == out[-1][1] + 1:
            out[-1][1] = n
        else:
            out.append([n, n])
    return out


def track(args):
    ref, ours = frames(args.ref), frames(args.ours)
    key = keyer(parse_skips(args.skip))
    found = {}
    for i, frame in enumerate(ours):
        found.setdefault(key(frame), []).append(i + 1)
    offset, changes, misses = None, [], []
    for j in range(1, len(ref) + 1):
        near = [c for c in found.get(key(ref[j - 1]), [])
                if (abs(c - j) <= 60 if offset is None else abs((c - j) - offset) <= 30)]
        if not near:
            misses.append(j)
            continue
        best = min(near, key=lambda c: abs((c - j) - (offset or 0)))
        if best - j != offset:
            changes.append((j, best - j))
            offset = best - j
    print('reference frames %d, matched %d, unmatched %d' % (len(ref), len(ref) - len(misses), len(misses)))
    print('offset changes (reference frame, ours - reference):', changes)
    print('unmatched stretches:', stretches(misses))


def clock(args):
    at = int(args.at, 16) & 0xFFFF
    for path in args.files:
        data = frames(path)[:args.frames]
        bcd = [(f[at] >> 4) * 10 + (f[at] & 15) for f in data]
        changes = [(i + 1, bcd[i - 1], bcd[i]) for i in range(1, len(bcd)) if bcd[i] != bcd[i - 1]]
        # (a whole second: from one tick down by one to the next, both inside a fight)
        seconds = [b[0] - a[0] for a, b in zip(changes, changes[1:])
                   if a[1] - a[2] == 1 and b[1] - b[2] == 1 and b[0] - a[0] < 60]
        if not seconds:
            print('%s: no whole second of the clock' % path)
            continue
        print('%s: %d seconds, frames a second: mean %.2f, median %s, %s' % (
            path, len(seconds), statistics.mean(seconds), statistics.median(seconds), sorted(Counter(seconds).items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='what', required=True)
    t = sub.add_parser('track')
    t.add_argument('ref')
    t.add_argument('ours')
    t.add_argument('--skip', action='append')
    t.set_defaults(run=track)
    c = sub.add_parser('clock')
    c.add_argument('files', nargs='+')
    c.add_argument('--at', default='FF8ABE')
    c.add_argument('--frames', type=int, default=1 << 30)
    c.set_defaults(run=clock)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
