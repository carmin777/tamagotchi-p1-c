#!/usr/bin/env python3
"""Compares snd.txt (buzzer changes) of the harness and of the rewrite: same sequence of
events (on with frequency f / off) and duration of each tone within a tolerance.
usage: tools/cmpsnd.py ref/snd.txt new/snd.txt [--tol 0.06]"""
import sys, argparse
ap = argparse.ArgumentParser(); ap.add_argument('ref'); ap.add_argument('new')
ap.add_argument('--tol', type=float, default=0.06); a = ap.parse_args()
def load(p):
    ev = []
    for l in open(p):
        s = l.split()
        ev.append((float(s[0]), ' '.join(s[1:])))
    return ev
r, n = load(a.ref), load(a.new)
for i in range(min(len(r), len(n))):
    if r[i][1] != n[i][1]:
        print(f'event {i} differs: ref {r[i]}  new {n[i]}'); sys.exit(1)
    if i and abs((r[i][0] - r[i-1][0]) - (n[i][0] - n[i-1][0])) > a.tol and r[i-1][1].startswith('on'):
        print(f'duration of tone {i-1} differs: ref {r[i][0]-r[i-1][0]:.3f}s new {n[i][0]-n[i-1][0]:.3f}s (t ref={r[i-1][0]:.3f})')
        sys.exit(1)
if len(r) != len(n):
    print(f'warning: {len(r)} events in ref, {len(n)} in new'); sys.exit(2)
print(f'ok: {len(r)} equal sound events (durations within ±{a.tol}s)')
