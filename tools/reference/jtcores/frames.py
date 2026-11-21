# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# frames.py CORE_TRACE PORT_TRACE: both runs cut into frames at the vblank handler (197C); each frame's key events
# (clocks after the handler's start)
import sys
W = {0x9DB0: 'tick', 0x9ED2: 'done?', 0x9EC0: 'more', 0x9EDC: 'wait', 0x9D0: 'yield', 0x26C: 'disp', 0x290: 'back'}
def cut(rows):
    frames = []; cur = None; t0 = 0
    for clk, ad in rows:
        if ad == 0x197C:
            if cur is not None: frames.append(cur)
            cur = []; t0 = clk
        elif cur is not None and ad in W:
            cur.append((clk - t0, W[ad]))
    return frames
core = []
for line in open(sys.argv[1]):
    fr, clk, fc, ad = line.split()
    if int(fc) & 3 == 2: core.append((int(clk), int(ad, 16)))
# (the core fetches each word as it prefetches: keep the first fetch of an address in a row)
port = []
for line in open(sys.argv[2]):
    p = line.split()
    if p[0] in 'bi': port.append((int(p[1]), int(p[2], 16)))
cf, pf = cut(core), cut(port)
def summary(f):
    return ' '.join('%s@%d' % (n, t) for t, n in f if n in ('tick', 'done?', 'more', 'wait'))[:300]
print('core frames', len(cf), 'port frames', len(pf))
for k in range(len(cf)):
    print('core', k, summary(cf[k]))
print()
for k in range(len(pf)):
    print('port', k, summary(pf[k]))
