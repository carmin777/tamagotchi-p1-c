#!/usr/bin/env python3
"""Compara a sequência de frames do LCD de duas execuções (formato frames.txt do
harness/trace e do src/tama-headless).

- Descarta frames transitórios (que duram menos que --min s): o emulador mostra
  cópias parciais da VRAM durante os ~100 ms da cópia.
- Junta frames iguais consecutivos e compara as duas listas em ordem.
- Reporta a defasagem de tempo entre frames correspondentes.

Sai com código != 0 na primeira divergência (mostrando os dois frames lado a lado).
uso: tools/cmpframes.py ref/frames.txt nova/frames.txt [--min 0.06] [--until T] [--max-drift S]
     tools/cmpframes.py ref nova --tol 3   (modo tolerante: cada frame de um lado tem que
         aparecer no outro com sobreposição de até ±3 s; ignora a ordem exata — serve para
         execuções longas em que o ritmo das animações deriva um pouco)
"""
import sys, argparse

def load(path, until):
    frames, t, cur = [], None, None
    for line in open(path):
        if line.startswith('@'):
            if cur is not None:
                frames.append((t, ''.join(cur)))
            t, cur = float(line[1:].split()[0]), []
        elif line.startswith('##'):
            continue
        elif cur is not None:
            cur.append(line)
    if cur is not None:
        frames.append((t, ''.join(cur)))
    if until is not None:
        frames = [f for f in frames if f[0] <= until]
    return frames

def settle(frames, min_dur):
    out = []
    for i, (t, f) in enumerate(frames):
        end = frames[i + 1][0] if i + 1 < len(frames) else float('inf')
        if end - t < min_dur:
            continue
        if out and out[-1][1] == f:
            continue
        out.append((t, f))
    return out

def side_by_side(a, b):
    la, lb = a.split('\n'), b.split('\n')
    return '\n'.join(f'{x:34s} {y}' for x, y in zip(la, lb))

ap = argparse.ArgumentParser()
ap.add_argument('ref'); ap.add_argument('new')
ap.add_argument('--min', type=float, default=0.06)
ap.add_argument('--until', type=float)
ap.add_argument('--max-drift', type=float, default=None)
ap.add_argument('--tol', type=float, default=None)
a = ap.parse_args()
ref = settle(load(a.ref, a.until), a.min)
new = settle(load(a.new, a.until), a.min)

def intervals(frames, end):
    out = {}
    for i, (t, f) in enumerate(frames):
        t1 = frames[i + 1][0] if i + 1 < len(frames) else end
        out.setdefault(f, []).append((t, t1))
    return out

if a.tol is not None:
    end = min(ref[-1][0], new[-1][0])
    ri, ni = intervals(ref, end), intervals(new, end)
    bad = []
    for name, src, dst in (('ref', ref, ni), ('nova', new, ri)):
        for i, (t, f) in enumerate(src):
            if t > end - a.tol:
                break
            t1 = src[i + 1][0] if i + 1 < len(src) else end
            if not any(u0 <= t1 + a.tol and u1 >= t - a.tol for u0, u1 in dst.get(f, [])):
                bad.append((t, name, f))
    bad.sort(key=lambda x: x[0])
    if bad:
        t, name, f = bad[0]
        print(f'{len(bad)} frame(s) sem par; primeiro: t={t:.3f} só na {name}:')
        print(f)
        other = new if name == 'ref' else ref
        near = [x for x in other if abs(x[0] - t) < a.tol]
        if near:
            print(f'(na outra execução, perto de t={t:.1f}:)')
            print(near[0][1])
        sys.exit(1)
    print(f'ok (tolerante ±{a.tol}s): {len(ref)} frames ref, {len(new)} frames nova, até {end:.1f}s')
    sys.exit(0)
n = min(len(ref), len(new))
drifts = []
for i in range(n):
    if ref[i][1] != new[i][1]:
        print(f'divergência no frame {i}: ref t={ref[i][0]:.3f}  nova t={new[i][0]:.3f}')
        if i:
            print(f'(frame anterior bateu: ref t={ref[i-1][0]:.3f} nova t={new[i-1][0]:.3f})')
        print('ref' + ' ' * 32 + 'nova')
        print(side_by_side(ref[i][1], new[i][1]))
        sys.exit(1)
    drifts.append(new[i][0] - ref[i][0])
status = 0
if len(ref) != len(new):
    print(f'aviso: {len(ref)} frames na ref, {len(new)} na nova (os {n} primeiros batem)')
    status = 2
if drifts:
    worst = max(drifts, key=abs)
    print(f'ok: {n} frames iguais; defasagem média {sum(drifts)/len(drifts):+.3f}s, pior {worst:+.3f}s, final {drifts[-1]:+.3f}s')
    if a.max_drift is not None and abs(worst) > a.max_drift:
        status = 3
sys.exit(status)
