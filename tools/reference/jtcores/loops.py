# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps2recomp.
# loops.py CORE PORT: per address, the most common time between two visits (a loop's whole period) on each side
import sys
from collections import Counter, defaultdict
def load(path, core):
    rows=[]
    for line in open(path):
        w=line.split()
        if core:
            if int(w[2])&3==2: rows.append((int(w[1]),int(w[3],16)))
        elif w[0] in 'bi': rows.append((int(w[1]),int(w[2],16)))
    return rows
c=load(sys.argv[1],True); p=load(sys.argv[2],False)
pstarts=set(a for _,a in p)
def periods(rows, keep):
    last={}; d=defaultdict(Counter)
    for t,a in rows:
        if keep is not None and a not in keep: continue
        if a in last and 0 < t-last[a] < 2000: d[a][t-last[a]]+=1
        last[a]=t
    return d
pc,pp=periods(c,pstarts),periods(p,None)
out=[]
for a in pc:
    if a in pp:
        cm,cn=pc[a].most_common(1)[0]; pm,pn=pp[a].most_common(1)[0]
        if cn>=10 and pn>=10 and cm!=pm and abs(cm-pm)<200: out.append(((cm-pm)*min(cn,pn),a,cm,pm,cn,pn))
out.sort(key=lambda x:-abs(x[0]))
for w,a,cm,pm,cn,pn in out[:40]:
    print('%06X period core %4d (x%d) port %4d (x%d)  %+d' % (a,cm,cn,pm,pn,w))
