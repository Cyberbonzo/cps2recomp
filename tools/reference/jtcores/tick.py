# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# tick.py CORE PORT CFRAME PFRAME: one game tick (009DB0 to 009ED2) on both, block by block: where the clocks differ
import sys
from collections import defaultdict
def frames(rows):
    out = []; cur = None; t0 = 0
    for clk, ad in rows:
        if ad == 0x197C:
            if cur is not None: out.append(cur)
            cur = []; t0 = clk
        if cur is not None: cur.append((clk - t0, ad))
    return out
core = []
for line in open(sys.argv[1]):
    fr, clk, fc, ad = line.split()
    if int(fc) & 3 == 2: core.append((int(clk), int(ad, 16)))
port = []
for line in open(sys.argv[2]):
    p = line.split()
    if p[0] in 'bi': port.append((int(p[1]), int(p[2], 16)))
cf, pf = frames(core), frames(port)
def tick(fr):
    s = next(k for k, (t, a) in enumerate(fr) if a == 0x9DB0)
    e = next(k for k in range(s, len(fr)) if fr[k][1] == 0x9ED2)
    return fr[s:e + 1]
c, p = tick(cf[int(sys.argv[3])]), tick(pf[int(sys.argv[4])])
p = [x for k, x in enumerate(p) if k == 0 or x[1] != p[k - 1][1] or True]
# match each port block to the core's next fetch of that address (within a window)
pairs = []; ci = 0
for t, a in p:
    j = ci
    while j < len(c) and c[j][1] != a and j - ci < 400: j += 1
    if j < len(c) and c[j][1] == a:
        pairs.append((c[j][0], t, a)); ci = j + 1
print('tick: core %d clocks, port %d; matched %d of %d port blocks' % (c[-1][0] - c[0][0], p[-1][0] - p[0][0], len(pairs), len(p)))
by = defaultdict(lambda: [0, 0, 0])
for (c1, q1, a1), (c2, q2, a2) in zip(pairs, pairs[1:]):
    b = by[a1]; b[0] += c2 - c1; b[1] += q2 - q1; b[2] += 1
rows = sorted(by.items(), key=lambda x: -(x[1][0] - x[1][1]))
print('block   times  core   port   core-port')
for a, (cc, pp, n) in rows[:30]:
    print('%06X %5d %7d %7d %+7d' % (a, n, cc, pp, cc - pp))
print('...most negative:')
for a, (cc, pp, n) in rows[-10:]:
    print('%06X %5d %7d %7d %+7d' % (a, n, cc, pp, cc - pp))
