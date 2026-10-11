#!/usr/bin/env python3
"""Imprime o trecho de tama.asm (agg23) entre dois endereços de ROM, com comentários.
uso: tools/asm.py 0x100 0x1ff   |   tools/asm.py label_32"""
import re, sys, os
ASM = os.path.join(os.path.dirname(__file__), '..', 'tamagotchi-disassembled', 'tama.asm')
lines = open(ASM).read().split('\n')
addr_of = {}
for i, l in enumerate(lines):
    m = re.search(r'// (0x[0-9A-F]+)\s+\(0x', l)
    if m: addr_of[i] = int(m.group(1), 16)
def line_for(a):
    best = None
    for i, x in addr_of.items():
        if x >= a and (best is None or x < addr_of[best]): best = i
    return best
if len(sys.argv) == 2 and not sys.argv[1].startswith('0x'):
    lab = sys.argv[1] + ':'
    i = next(i for i, l in enumerate(lines) if l.startswith(lab))
    j = i + 1
    while j < len(lines) and not re.match(r'^[a-z_0-9]+:', lines[j]): j += 1
    print('\n'.join(lines[max(0, i - 8):j])); sys.exit()
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
i = line_for(lo); j = line_for(hi + 1) or len(lines)
while i > 0 and addr_of.get(i - 1) is None and lines[i - 1].strip() != '' or (i > 0 and addr_of.get(i-1) is None and lines[i-1].strip().startswith('//')): i -= 1
print('\n'.join(lines[i:j]))
