#!/usr/bin/env python3
"""Compara duas sequências de frames (formato harness/trace frames.txt).
Compara o LCD amostrado em instantes regulares (-p passo em s), tolerando
uma defasagem de até -t segundos. Sai !=0 na primeira divergência.
uso: tools/cmpframes.py ref/frames.txt nova/frames.txt [-p 0.1] [-t 0.1]"""
import sys, argparse, bisect
def load(p):
    ts, fr = [], []
    cur = None
    for l in open(p):
        if l.startswith('@'):
            if cur is not None: fr.append(''.join(cur))
            ts.append(float(l[1:].split()[0])); cur = []
        elif l.startswith('##'): continue
        elif cur is not None: cur.append(l)
    if cur is not None: fr.append(''.join(cur))
    return ts, fr
def at(ts, fr, t):
    i = bisect.bisect_right(ts, t) - 1
    return fr[i] if i >= 0 else None
ap = argparse.ArgumentParser()
ap.add_argument('ref'); ap.add_argument('new')
ap.add_argument('-p', type=float, default=0.1); ap.add_argument('-t', type=float, default=0.1)
a = ap.parse_args()
rt, rf = load(a.ref); nt, nf = load(a.new)
end = min(rt[-1], nt[-1]) if rt and nt else 0
t, bad = 0.0, 0
while t <= end:
    r = at(rt, rf, t)
    ok = any(at(nt, nf, t + d) == r for d in (0, -a.t, a.t, -a.t/2, a.t/2))
    if not ok:
        print(f'divergência em t={t:.3f}s\n--- ref\n{r}--- nova\n{at(nt, nf, t)}'); sys.exit(1)
    t += a.p
print(f'ok: {len(rf)} frames ref, {len(nf)} frames nova, até {end:.1f}s')
