#!/usr/bin/env python3
"""Compara dumps de RAM (ram.txt) do harness e da reescrita, dump a dump (pela ordem),
só nos nibbles que a reescrita mapeia (os outros aparecem como '-').
uso: tools/cmpram.py ref/ram.txt nova/ram.txt"""
import sys
NAMES = {0x10:'sec',0x12:'min',0x14:'hour',0x40:'hunger',0x41:'happy',0x42:'care_mistakes',0x43:'discipline',
 0x46:'weight',0x48:'sick',0x49:'sick_count',0x4A:'sleep',0x4B:'light',0x4D:'poop',0x4E:'lifespan_over',
 0x4F:'lifespan_strikes',0x50:'stage',0x51:'discipline_misses',0x54:'age',0x5C:'event',0x5D:'species',
 0x7B:'sound_on',0x7C:'egg',0x200:'hunger_timer',0x202:'happy_timer',0x204:'sleep_minutes',0x206:'poop_timer',
 0x208:'call_hunger',0x209:'call_discipline',0x20A:'empty_heart_min',0x20D:'sick_timer',0x210:'stage_timer',
 0x213:'call_q213',0x214:'call_q214',0x215:'nap',0x216:'baby_awake',0x230:'params',
 0x5A:'rng',0x82:'game_rounds',0x83:'game_wins',0x84:'game_result'}
NAMES.update({0x73:'food_choice',0x78:'saved_cursor78'})   # menu_care.c
def name(a):
    return next((f'{NAMES[b]}+{a-b}' if a != b else NAMES[b]) for b in sorted(NAMES, reverse=True) if b <= a)
def load(p):
    dumps, cur = [], None
    for l in open(p):
        if l.startswith('@'):
            cur = [l.strip(), []]; dumps.append(cur)
        elif cur:
            cur[1].extend(l.split(':', 1)[1].replace(' ', '').strip())
    return dumps
ref, new = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for (hr, r), (hn, n) in zip(ref, new):
    diffs = [(i, r[i], n[i]) for i in range(min(len(r), len(n))) if n[i] != '-' and r[i] != n[i]]
    print(f'{hr}  |  {hn}: ' + ('ok' if not diffs else f'{len(diffs)} diferença(s)'))
    for i, a, b in diffs:
        print(f'   0x{i:03X} {name(i):22s} ref={a} nova={b}')
    bad += bool(diffs)
sys.exit(1 if bad else 0)
