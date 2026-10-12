#!/usr/bin/env python3
"""Compara snd.txt (mudanças do buzzer) do harness e da reescrita: mesma sequência de
eventos (liga com frequência f / desliga) e duração de cada tom com tolerância.
uso: tools/cmpsnd.py ref/snd.txt nova/snd.txt [--tol 0.06]"""
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
        print(f'evento {i} difere: ref {r[i]}  nova {n[i]}'); sys.exit(1)
    if i and abs((r[i][0] - r[i-1][0]) - (n[i][0] - n[i-1][0])) > a.tol and r[i-1][1].startswith('on'):
        print(f'duração do tom {i-1} difere: ref {r[i][0]-r[i-1][0]:.3f}s nova {n[i][0]-n[i-1][0]:.3f}s (t ref={r[i-1][0]:.3f})')
        sys.exit(1)
if len(r) != len(n):
    print(f'aviso: {len(r)} eventos na ref, {len(n)} na nova'); sys.exit(2)
print(f'ok: {len(r)} eventos de som iguais (durações dentro de ±{a.tol}s)')
