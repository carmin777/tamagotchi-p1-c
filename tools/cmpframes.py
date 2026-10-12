#!/usr/bin/env python3
"""Compares the sequence of LCD frames of two runs (frames.txt format of
harness/trace and src/tama-headless).

- Discards transient frames (lasting less than --min s): the emulator shows
  partial copies of the VRAM during the ~100 ms of the copy.
- Merges consecutive equal frames and compares the two lists in order.
- Reports the time offset between corresponding frames.

Exits with code != 0 at the first divergence (showing the two frames side by side).
usage: tools/cmpframes.py ref/frames.txt new/frames.txt [--min 0.06] [--until T] [--max-drift S]
     tools/cmpframes.py ref new --tol 3   (tolerant mode: each frame on one side must
         appear on the other with an overlap of up to ±3 s; ignores the exact order — meant for
         long runs in which the animation pace drifts a little)
"""
import sys, argparse

def load(path, until):
    frames, t, cur = [], None, None
    for line in open(path):
        if line.startswith('@'):
            if cur is not None:
                frames.append((t, ''.join(cur)))
            t, cur = float(line[1:].split()[0]), []
        elif line.startswith('## '):
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
    # short frames (< --min) are merely not required to be matched; the full list counts as a match candidate
    ref_all, new_all = settle(load(a.ref, a.until), 0), settle(load(a.new, a.until), 0)
    end = min(ref[-1][0], new[-1][0])
    ri, ni = intervals(ref_all, end), intervals(new_all, end)
    bad = []
    for name, src, dst in (('ref', ref, ni), ('new', new, ri)):
        for i, (t, f) in enumerate(src):
            if t > end - a.tol:
                break
            t1 = src[i + 1][0] if i + 1 < len(src) else end
            if not any(u0 <= t1 + a.tol and u1 >= t - a.tol for u0, u1 in dst.get(f, [])):
                bad.append((t, name, f))
    bad.sort(key=lambda x: x[0])
    if bad:
        t, name, f = bad[0]
        print(f'{len(bad)} unmatched frame(s); first: t={t:.3f} only in {name}:')
        print(f)
        other = new if name == 'ref' else ref
        near = [x for x in other if abs(x[0] - t) < a.tol]
        if near:
            print(f'(in the other run, near t={t:.1f}:)')
            print(near[0][1])
        sys.exit(1)
    print(f'ok (tolerant ±{a.tol}s): {len(ref)} ref frames, {len(new)} new frames, up to {end:.1f}s')
    sys.exit(0)
n = min(len(ref), len(new))
drifts = []
for i in range(n):
    if ref[i][1] != new[i][1]:
        print(f'divergence at frame {i}: ref t={ref[i][0]:.3f}  new t={new[i][0]:.3f}')
        if i:
            print(f'(previous frame matched: ref t={ref[i-1][0]:.3f} new t={new[i-1][0]:.3f})')
        print('ref' + ' ' * 32 + 'new')
        print(side_by_side(ref[i][1], new[i][1]))
        sys.exit(1)
    drifts.append(new[i][0] - ref[i][0])
status = 0
if len(ref) != len(new):
    print(f'warning: {len(ref)} frames in ref, {len(new)} in new (the first {n} match)')
    status = 2
if drifts:
    worst = max(drifts, key=abs)
    print(f'ok: {n} equal frames; mean offset {sum(drifts)/len(drifts):+.3f}s, worst {worst:+.3f}s, final {drifts[-1]:+.3f}s')
    if a.max_drift is not None and abs(worst) > a.max_drift:
        status = 3
sys.exit(status)
