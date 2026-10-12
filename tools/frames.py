#!/usr/bin/env python3
"""Lista frames de um frames.txt entre dois tempos: tools/frames.py arq T0 T1 [--full]"""
import sys
path, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
full = '--full' in sys.argv
cur, t = [], None
def flush():
    if t is not None and t0 <= t <= t1:
        f = ''.join(cur)
        print(f'@{t:.3f} {abs(hash(f)) % 100000:05d}')
        if full: print(f, end='')
for l in open(path):
    if l.startswith('@'):
        flush(); t = float(l[1:].split()[0]); cur = []
    elif not l.startswith('## ') and t is not None:
        cur.append(l)
flush()
