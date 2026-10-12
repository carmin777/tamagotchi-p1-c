# ROM 0xE00-0xFFF + bank 1 (0x1000-0x17FF, code only/jump tables/sprite format)

Conventions (same as in 0xC00-0xDFF.md): `M[a]` = RAM nibble; `Q[a]` = `M[0x200+a]` (page 2, YP=2);
`P[a]` = `M[a]` page 0. ROM addresses = word index (as in tama.asm). 12-bit words.
`retd l` / `lbpx mx,l`: `M(X)=l&F; M(X+1)=l>>4; X+=2` (retd also returns). `jpba`: `PC=(np<<8)|(B<<4)|A`, `np` = last
`pset` (or the page itself, after call/jp). So, throughout this stretch **"tables" are sequences of `lbpx...retd`
(the disassembler shows them as code) whose "value" is the set of bytes written to RAM at X**; an entry ends at the `retd`.
General evidence: tama.asm + `tamatool/src/lib/cpu.c` (op_lbpx_cb/op_retd_cb/op_jpba_cb); ROM read straight from `rom/tama.b`
(word = `((b[2a]<<8)|b[2a+1]) & 0xFFF`).

Range map:

| range | name | type |
|---|---|---|
| 0xE00-0xE0C | `anim_index_table` | 13 `retd` (index->offset) |
| 0xE0D | `jp_table_0xE00` | `jpba` (dispatcher of the two tables above/below) |
| 0xE0E-0xE59 | `anim_script_records` | data (animation scripts of the side icons) |
| 0xE5A-0xE7B | `menu_choose_option` (label_352) | routine |
| 0xE7C | `jp_table_0xE7D_buzzer` | `jpba` |
| 0xE7D-0xEFC | `sound_script` | data (music/beeps) |
| 0xEFD-0xEFF | `nop7` padding | |
| 0xF00-0xFD3 | `check_events` (label_357) | routine (core of the care rules) |
| 0xFD4-0xFD8 | `set_event` / `event_pending` (set_0x5C_to_a, label_385) | helpers |
| 0xF3D-0xF49 | `shrink_interval_25pct` (label_363) | helper |
| 0xFD9-0xFDE | `cmp8_hour` (label_386) | helper |
| 0xFDF-0xFEB | `add_care_mistake` (label_388) / `sat_inc4` (label_389) | helpers |
| 0xFEC-0xFFE | `hatch_egg` (label_391) | routine |
| 0x1000-0x102F | `gfx_dispatch` | graphics jump table |
| 0x1030-0x11DF, 0x1230-0x14FF | 16x8 sprites | lbpx data |
| 0x11E0-0x1221 | `set_init_mem_and_int` (+`_cont_set_mem`) | routine (bank 1) |
| 0x1222-0x122E | `copy_6_mx_my` | routine |
| 0x1500-0x1557 | small glyphs (4x8 and 3x8 digits, arrows) | data |
| 0x1558-0x1563 | `stage_param_jump_table` | 12 `jp` |
| 0x1564-0x15E5 | `stage_param_blocks` (10x13 bytes) | game data |
| 0x15E6-0x15FD | `hour24_to_12_table` | 24 `retd` |
| 0x1600-0x17FF | 8x8 glyphs | lbpx data |

---------------------------------------------------------------------------------------------------

## 1. Side-icon animations: `anim_index_table` 0xE00 + `anim_script_records` 0xE0E

Use: `play_anim` (label_272/273, 0x8A8-0x8FF; outside the range, notes 0x400-0x5FF) with `Y = anim_id` (0..12), A=pose -> M[5E],
B=seconds -> M[57]. 0x8B1-0x8B3: `X=0xA6; call jp_table_0xE00` with B:A = anim_id writes into `M[A6..A7]` the offset (retd 1 byte) of the
first record. On each frame (0x8C1-0x8D4) the record at `0xE00+offset` is read with another `call jp_table_0xE00` (X=0xA2)
and `M[A6..A7] += 2` (next consecutive record; a 3-byte record overwrites A6/A7 with `next`).

**Index** (offset in 0xE00 -> initial record): `[0]=0x0E [1]=0x11 [2]=0x16 [3]=0x1F [4]=0x24 [5]=0x24 [6]=0x29 [7]=0x2E
[8]=0x31 [9]=0x3C [10]=0x45 [11]=0x4E [12]=0x55`.

**Record** (written at X=0xA2): 2 bytes (`lbpx; retd`) = `[b0,b1]`, or 3 bytes (`lbpx; lbpx; retd`) = `[b0,b1,next]`
(`next` = record offset, in 0xE00+, where the loop goes back to). Fields:
- `b0 & 0x3F` = 8x8 glyph index `g` (ROM address `0x1600 + 8*g`; see §8). `g=0x3F` (b0=0xFF/0x7F) = "do not draw"
  (0x8D5-0x8DA tests M[A8]==F && M[A9]==3). Copies: `M[A8]=b0 low nibble`, `M[A9]=(b0>>4)&3`.
- `b0 >> 6` = frame wait counter: `M[AA] = (b0>>6)&3` (0x8CD-0x8D4); at 0x8BF `M[AA]` is decremented and,
  while it does not overflow, the frame is redrawn without advancing to the next record (number of extra frames of permanence) (?).
- `b1` = drawing position in the video buffer: `X = 0x100 + b1` (XL=b1 low, XH=b1 high; `b1>=0x80` = lower half of the screen;
  `b1=0x50` = off-screen/null). Column = `(b1&0x7F)/2`, rows 0-7 if `b1<0x80` otherwise 8-15. For M[5D]==1 (baby) there is an XH adjustment at
  0x8DF-0x8EF (`XH |= 8` if XH≠0, then `X += 0xF8`...) (?).
The glyphs are drawn by `fetch_and_misc_render` (§8) at (Y=0xA8).

Decoded records (offset: b0 b1 [next] -> glyph):
```
0E: FF 50 ->0E            anim 0 : empty, repeats
11: 60 30 ; 13: 61 30 ->11  anim 1 : large Z (0x1700) / small z (0x1708) at x=24,rows 0-7  = SLEEPING pet
16: FF 50 ; 18: FF 50 ; 1A: 5E A8 ; 1C: 5F A8 ->1A   anim 2 : 2 empty frames, then stink (0x16F0/0x16F8) at x=20,rows 8-15 = POOP
1F: 65 30 ; 21: 64 30 ->1F   anim 3 : anger marks (0x1728/0x1720) = CALL/ATTENTION
24: 62 30 ; 26: 7F 50 ->24   anim 4 and 5 (same record) : skull (0x1710) alternating with empty = SICK
29: 7F 50 ; 2B: 63 30 ->29   anim 6 : empty / sparkle (0x1718) = HAPPY
2E: D7 B0 ->2E               anim 7 : glyph 0x16B8 ("FF") at x=24, rows 8-15 (held, wait=3)
31: 10 00 ; 33: 10 80 ; 35: D1 80 ; 37: D2 80 ; 39: D3 80 ->39   anim 8  : bread (0x1680) + 3 bites (0x1688,0x1690,0x1698) = EAT MEAL
3C: 10 00 ; 3E: 10 80 ; 40: 52 80 ; 42: 53 80 ->42                anim 9  : bread variant (g=0x12,0x13)
45: 14 00 ; 47: 14 80 ; 49: D5 80 ; 4B: D6 80 ->0E                anim 10 : snack (0x16A0) + bites (0x16A8,0x16B0), ends in anim 0
4E: 14 00 ; 50: 14 80 ; 52: 56 80 ->0E                            anim 11 : snack, variant
55: 10 00 ; 57: D0 80 ->57                                        anim 12 : bread and holds
```
Glyph identification by `g`: 0x10 bread, 0x11/0x12/0x13 bread bites (0x1688/0x1690/0x1698), 0x14 snack, 0x15/0x16 snack bites, 0x17 "FF", 0x1E/0x1F = stink
(0x16F0/0x16F8), 0x20/0x21 = large/small Z, 0x22 skull, 0x23 sparkle, 0x24/0x25 anger marks
(glyph names from "Useful Addresses.md", checked against the addresses 0x16F0-0x1728).
Confidence in the semantic names of the animations: medium (deduced from the glyphs; ids called in other notes: 2=food? see 0x200-0x3FF).

Pseudo-C of the record:
```c
typedef struct { uint8_t glyph:6, hold:2; uint8_t pos; uint8_t next; bool has_next; } AnimRec;
// anim_ids[13] = {0x0E,0x11,0x16,0x1F,0x24,0x24,0x29,0x2E,0x31,0x3C,0x45,0x4E,0x55}
```
(the 13 records are extracted from the ROM at build time: read words from 0xE0E; `lbpx`=0x9II, `retd`=0x1II; a record ends at the retd).

---------------------------------------------------------------------------------------------------

## 2. `menu_choose_option` — 0xE5A-0xE7B (agg23 `label_352`)
2-option menu (light yes/no; also used at 0xAC8). Inputs: B = timeout in s (callers pass 0xA), M[78] = initial option
(0/1). Output: **C=0,Z=1: confirmed (M); C=1,Z=1: cancelled (R button); C=1,Z=0: timeout or pending event**; A = cursor (M[75]).
Callers: 0x71C (light), 0xACA (`label_297`). Calls: `label_66` (0x1EC: M[75]=0 cursor, M[76]=A=2 number of items, M[77]=B timeout,
M[57]=B), `label_30` (buffer window at 0x140), `label_94` (0x2C3, button/cursor reading), `render` 0x792 `label_233`,
`copy_video_buf_to_vram`, `check_events`.
```c
int menu_choose_option(uint8_t timeout /*B*/) {
    M[75]=0; M[76]=2; M[77]=timeout; M[57]=timeout; copy_0x026_7_to_8_9();
    M[2A]=0; M[2B]=4; M[3A]=0; M[3B]=4;               // window and drawing cursor = 0x140
    saved = M[78]; M[75] = saved;                      // initial cursor
    for (;;) {
        flags = menu_poll_input();                     // label_94: L(bit2 of M[29]) => M[75]=(M[75]+1)%M[76] + beep 0x94;
                                                       //           M => C=0,Z=1; R => C=1,Z=1; timeout (M[57]==0 && M[77]!=0) => C=1,Z=0
        if (C || Z) break;
        M[90..91]=0xFF; M[98..99]=0xFF;                // glyph slots: FF = blank (g=0x3F)
        slot = (cursor==0) ? 0x90 : 0x98;              // row 0 or row 1 of the list of 8 glyphs
        M[slot]=7; M[slot+1]=2;                        // g=0x27 -> glyph 0x1738 = full arrow "->"
        render_8_glyphs();                             // label_233 (Y=0x90..)
        copy_video_buf_to_vram();
        if (check_events() /*NZ*/) { A=0; C=1; /*Z=0*/ break; }
    }
    M[78] = A;                                         // final cursor
    return;
}
```
(Button mapping in M[29]: bit0 R/C, bit1 M/B, bit2 L/A, per 0x2CE-0x2E4.)

---------------------------------------------------------------------------------------------------

## 3. `sound_script` — 0xE7C (`jpba`) / 0xE7D-0xEFC
Read by `configure_buzzer` (0x1A0; runs at the start of each prog timer interrupt, ~256 Hz (?) according to the init 0x11F4).
State: `M[32..33]` duration counter (8 bits, decremented every tick; on overflow (0x00 -> 0xFF) loads the next step),
`M[34]` freq (bits 0-2 = `F74` buzzer freq index; **bit3=1 => silence/pause**), `M[35]` = envelope (`F75`, always 0 here),
`M[36..37]` pointer (offset in 0xE00+, 0x7D..0xFC) to the next step, `M[58..59]` number of repeats (`M[59]==F` = infinite).
Step = bytes written at X=0x32 (and fires `jpba` with B:A = `M[37]:M[36]` already incremented by 2 words):
- `lbpx;retd` (2 words, 4 nibbles): `[dur, ctrl]` -> continues to the next step (pointer += 2 words already done).
- `lbpx;lbpx;retd` (3 words, 6 nibbles): `[dur, ctrl, next]` -> `M[36..37]=next` (jump/loop). If `M[59]!=F`, the jump is taken
  `M[58]` times; once exhausted (`M[58:59]` borrow) the pointer goes to 0x7D (silence).
Steps (offset: dur ctrl [next]; "dur" in ticks-1 → duration = dur+1 ticks; ctrl: bit3=pause):
```
7D: dur=255 ctrl=8(pause) ->7D                 (idle; default pointer)
80: 48 ctrl3 ->7D                              tune 0x80: long beep (power on/reset)
83..91: dur=0, ctrl 7,6,5,4,3,2,1,0 ->7D       descending sweep (tune 0x83)
94: 1 c4 ->7D                                  button beep (default, M[7B]!=0)
97: 1 c5 ; 99: 1 pause ; 9B: 1 c3 ->7D          tune 0x97 (sound on/off)
9E: 5 c7 ; A0: 3 pause ; A2: 11 c7 ->7D          tune 0x9E (error/negative)
A5: 1 c2 ; A7: 1 pause ; A9: 7 c3 ; AB: 7 pause ->A5     tune 0xA5 (bedtime/call, loop with repeats M[58])
AE: 2 c1 ; B0: 2 c4 ; B2: 2 c2 ; B4: 2 c5 ; B6: 2 c3 ; B8: 2 c6 ; BA: 2 c4 ; BC: 2 c7 ->AE       tune 0xAE (evolution, loop)
BF: 18 c3 ; C1: 25 pause ->BF    C4: 24 c3 ; C6: 40 pause ->C4    C9: 28 c3 ; CB: 68 pause ->C9   tunes 0xBF/0xC4/0xC9 (death, 3 tones)
CE: 80 c3 ->7D                                  tune 0xCE (tombstone)
D1: 3 c4 ; D3: 2 c2 ->D1                         tune 0xD1 (hatching, 6 repeats)
D6: 12 c2 ; D8: 12 c4 ; DA: 12 c6 ; DC: 12 c4 ; DE: 12 c2 ; E0: 12 pause ; E2: 16 c1 ->7D     tune 0xD6 (jingle)
E5: 12 c4 ; E7: 6 c2 ; E9: 12 c3 ; EB: 6 c4 ; ED: 16 c5 ->7D                                  tune 0xE5
F0: 6 c1 ; F2: 7 c2 ; F4: 8 c3 ; F6: 10 c4 ; F8: 12 c5 ; FA: 14 c6 ->7D                        tune 0xF0 (rising)
```
Who starts them (pointer in Y, repeats in A, B=0 or F): see the `grep` of the calls to `if_0x7B_set_clear_0x32_store_*` (`M[7B]=0` turns
sound off): 0x80 (reset, 0x21B/0x23A), 0x94 (default beep: 0x27F,0x2D0,...,0x82A), 0xA5 (0x31A), 0xAE (0x34D), 0xBF/0xC4/0xC9/0xCE (0x381-0x39F),
0xD6 (0x641), 0x97 (0x668/0x67D), 0xE5 (0x6A7), 0x83 (0x6C9), 0x9E (0x6CE), 0xF0 (0xAE8), 0xD1 (0xFF1, hatching).
The freq values `ctrl&7` map to `hw_set_buzzer_freq` (4096, 3277, 2731, 2341, 2048, 1638, 1365, 1170 Hz (?) — see tamalib's hal).

Pseudo-C (`jp_table_0xE7D_buzzer` = step lookup):
```c
typedef struct { uint8_t dur, ctrl; int next; /* -1 = following */ } SoundStep;   // extracted from the ROM at build time (0xE7D..0xEFC)
```

---------------------------------------------------------------------------------------------------

## 4. `check_events` — 0xF00-0xFD8 (agg23 `label_357`)
"Waiting" call from all the UI loops (0x2AF clock screen, 0x52D pet loop, 0x6C3, 0x831, 0xE75 menu).
Computes the **pending event M[5C]** (0=none) and returns **NZ if M[5C]≠0** (the caller dispatches at 0x307, notes 0x200-0x3FF).
Only works if `M[7C]==0xF` (the pet has hatched) and `M[5C]==0`. XP=0 for `P`, YP=2 for `Q`. The counter variables (Q0-Q17)
are decremented by `minute_tick` (0xC7A, notes 0xC00-0xDFF); here only thresholds are tested.
Events: 1 bedtime; 2 hunger/happiness heart reached zero; 3 "discipline" call (?); 4 health timer (Q6/7=0);
5 evolve; 6 death.

RAM (all confirmed by trace baby0.sav, except those marked (?)):
| address | meaning |
|---|---|
| P7C | egg phase (5->0 in 5 min; F = hatched) |
| P5C | pending event |
| P5D | form/character (0 egg,1 baby,...) — index of the parameter table §6 |
| P50 | evolution branch (F = final form, no more evolution) |
| P14,P15 | **time of day, binary** (0..23); clock: P10/11 sec, P12/13 min, P14/15 hour |
| P4A bit3 | sleeping; P4B=F "just woke up / new day" |
| P48 bit3 | sick; P49 number of illnesses (>=3 => event 6) |
| P40,P41 | hunger/happiness hearts in 1/4 (4 = 1 heart) |
| P42 | care mistakes (sat F); P51 "discipline" counter (sat F); P4F, P4E lifespan/age (F=exhausted) |
| P43 | step of the call accumulator (init 0; =8 if P50 in {2,4}) (?) |
| P54,P55 | age in days, BCD, saturates at 99 |
| Q0:1 / Q2:3 | count (min) until losing 1/4... heart (hunger / happiness); reload from Q34:35 / Q36:37 |
| Q4:5 | minutes asleep (sleep) — Q5>=4 and !=F => care mistake (light on?) (?) |
| Q6:7 | timer of event 4 |
| Q8 / Q9 | call counters 1 and 2 (+1/min, F => penalty) |
| QB,QC | part of the counter "minutes with an empty heart" Q[A..C]; >=0x2D0 (12 h) and !=baby => death |
| QD,QE,QF | sickness/poop counter Q[D..F]: 0 and healthy => sick; sick and >=0x160 => death |
| Q10..12 | remaining time of the current form (min); 0 => evolves/lifespan |
| Q13,Q14 | Q13 counts heart losses (reload Q243); Q14 call accumulator |
| Q15 | baby: nap duration (min); Q16:17 baby: awake time (init 0x28) |
| Q30..Q33 | wake time (Q31:30) and bedtime (Q33:32) — come from the form block §6 |

Faithful pseudo-C:
```c
bool check_events(void) {                      // returns M[5C] != 0
    if (P[7C] != 0xF) goto done;
    if (P[5C] != 0)   goto done;
    uint8_t hour = atomic(P[14] | P[15]<<4);   // clear_0x07D; ldpx a,mx; ld b,mx; set_f_0x07D
    uint8_t wake = Q8(0x30), bed = Q8(0x32);   // Q[31]:Q[30], Q[33]:Q[32]
    if (P[4A] & 8) {                           // ---- sleeping
        bool wake_now;
        if (P[5D] == 1) wake_now = (Q[15] == 0);                  // baby: the nap is over
        else            wake_now = !(hour < wake) && (hour < bed); // wake <= hour < bed
        if (wake_now) {
            P[4B] = 0xF; P[4A] = 0;                               // woke up
            age_bcd_inc();                                        // D=1: P54 += 1; P55 += carry; if it overflows P54=P55=9; D=0
            if (P[50] == 0xF && is_zero3(Q[0x10])) {              // final form, lifespan exhausted
                Q8_set(0x34, Q8(0x34) - (Q8(0x34) >> 2));         // shrink_interval_25pct: interval *= 0.75 per day
                Q8_set(0x36, Q8(0x36) - (Q8(0x36) >> 2));
            }
            goto done;                                            // (F2C/F2F: if P50!=F or Q10..12!=0 it also exits)
        }
        if (Q[5] != 0xF && Q[5] >= 4) { Q[5] = 0xF; add_care_mistake(); }  // still night; 0xF35-0xF3B
        goto done;
    }
    // ---- awake: bedtime?
    if (P[5D] == 1) { if (Q[15] != 0 && is_zero2(Q[0x16])) { P[5C] = 1; goto done; } }   // baby: awake time Q16:17 ran out
    else            { if (hour >= bed || hour < wake)        { P[5C] = 1; goto done; } }
    // ---- death conditions (event 6)
    bool dead = (P[49] >= 3);
    if (P[5D] != 1) {                                         // the baby is immune to the next two
        if (Q[0xC] >= 2 && Q[0xB] >= 0xD) dead = true;        // >= 0x2D0 min with an empty heart (Q[A..C])
        if ((P[48] & 8) && Q[0xF] >= 1 && Q[0xE] >= 6) dead = true;  // sick for >= 0x160 min
    }
    if (P[4F] >= 5) dead = true;
    if (dead) { P[5C] = 6; goto done; }
    // ---- evolution / lifespan
    if (is_zero3(Q[0x10])) {                                  // the form's counter reached 0
        if (P[50] < 0xF) { P[5C] = 5; goto done; }            // evolve
        P[4E] = 0xF;                                          // final form: lifespan exhausted
    }
    if (is_zero2(Q[6])) { P[5C] = 4; goto done; }
    if (!(P[48] & 8) && is_zero3(Q[0xD])) { P[48] = 0xF; P[49]++; }        // falls sick
    if (is_zero2(Q[0])) {                                     // hunger: the counter reached zero
        Q8_set(0, Q8(0x34));                                  // reloads (atomic)
        if (P[40] >= 4) { P[40] -= 4; if (Q[13]) Q[13]--; }   // add mx,0xC / carry
        else            { P[40] = 0; P[5C] = 2; goto done; }
    }
    if (is_zero2(Q[2])) {                                     // happiness
        Q8_set(2, Q8(0x36));
        if (P[41] >= 4) { P[41] -= 4; if (Q[13]) Q[13]--; }
        else            { P[41] = 0; P[5C] = 2; goto done; }
    }
    if (Q[13] == 0) {                                         // 0xFB7-0xFC5
        Q[13] = Q[0x43];
        unsigned s = Q[14] + P[43] + 1; Q[14] = s & 0xF;
        if (!(s > 0xF) && P[5D] != 1) { P[5C] = 3; goto done; }   // event 3 only if there was NO carry and it is not the baby
    }
    if (Q[8] == 0xF) { Q[8] = 0; if (P[5D] != 1) add_care_mistake(); }   // call 1 ignored
    if (Q[9] == 0xF) { Q[9] = 0; P[51] = sat_inc4(P[51]); }              // call 2 ignored
done:
    return P[5C] != 0;                                        // label_385: cp P5C,0 ; NZ => there is an event
}
```
Observations:
- Code order: deaths (`label_367`-`label_371`) before evolution; 0xF71 uses `P[4F]<5`. The check order is confirmed by the
  exits: each `set_0x5C_to_a` (0xFD4) is a `return`.
- The "sleeping" branch does not check the wake-up time for the baby via thresholds (it uses Q15). "Awake, baby" branch: only sleeps if Q15≠0 and Q16:17==0
  (0xF4A-0xF52; `calz label_31` = tests 2 nibbles).
- Event 3 (P43/Q13/Q14): "accumulator" logic literally as in the code; game meaning = discipline call (?).
- `P[4B]=F` on waking is consumed outside this range (age/"new day") (?).
Evidence: trace baby0.sav (3600 s): P5C=2 (0xFD5) at every heart loss, P5C=4 at t=864 s; Q0x208 resets at 0xFC9; P40 `0->C->0` (0xF99/F9B)
confirms `add mx,0xC`; hour 0x08->0x09 in P14/P15.

### Helpers
| address | name | effect |
|---|---|---|
| 0xFD4-0xFD5 `set_0x5C_to_a` | `set_event(a)` | `P[5C]=A` and falls into 0xFD6 |
| 0xFD6-0xFD8 `label_385` | `event_pending()` | `cp P[5C],0; ret` (NZ = event) |
| 0xFD9-0xFDE `label_386` | `cmp8_hour(Y)` | compares `B:A` (8 bits, hour) with `Q[Y]:Q[Y-1]` (Y points at the HIGH nibble): `cp b,my`; if equal `Y--` and `cp a,my`. C=1 if hour < value. Used with Y=0x31 (wake), 0x33 (sleep) |
| 0xF3D-0xF49 `label_363` | `shrink_interval_25pct(Y)` | `v=Q8(Y); Q8(Y)=v-(v>>2)` (rrc/rrc/and b,3 + sub/sbc); at Y=0x34 and 0x36 |
| 0xFDF-0xFE7 `label_388` | `add_care_mistake()` | `P[42]=sat_inc4`; if `P[50]==F && Q[10..12]==0` also `P[4F]=sat_inc4` |
| 0xFE8-0xFEB `label_389` | `sat_inc4(X)` | `M[X]++`, stops at F |
| 0xFEC-0xFFE `label_391` | `hatch_egg()` | see below |

`label_31` (0xD4) / `label_32` (0xD7), used above: `is_zero2(Y)` / `is_zero3(Y)` = Z if `Q[Y]|Q[Y+1](|Q[Y+2])==0` (with interrupts off).

## 5. `hatch_egg` — 0xFEC-0xFFE (`label_391`)
Called at 0x516 when `P[7C]==0` (the egg was born). Does: `P[7C]=F`; plays tune 0xD1 (6 repeats, B=0) if `P[7B]`; `play_anim(A=2,B=4 s, id=0)`;
`P[74]=F`; `P[5D]=1,P[5E]=1` (Babytchi); `label_142` (0x465, prepares tables/sprite) and `jp label_347` (0xDD2 `load_form_params`: loads the
block of §6 into Q[0x30..0x49] and copies Q[0x240..242]->Q[0x210..212], Q[0x238..23A]->Q[0x20D..20F]).
```c
void hatch_egg(void){ P[7C]=0xF; play_tune(0xD1, /*rep*/6, 0); play_anim(2,4,/*id*/0);
    P[74]=0xF; P[5D]=1; P[5E]=1; render_pet(); load_form_params(); }
```

---------------------------------------------------------------------------------------------------

## 6. Per-form parameters: `stage_param_jump_table` 0x1558 + blocks 0x1564-0x15E5
`load_form_params` (0xDD2): `A = P[5D]+8; B = 5` -> M0/M1; M2=5 -> `render_asset` (page 0x15, offset `0x58+P[5D]`) with X=0x230 (XP=2).
The destination is one of the 12 `jp` at 0x1558..0x1563 (`jp 0x64,0x64,0x71,0x7E,0x8B,0x98,0xA5,0xB2,0xBF,0xCC,0xD9,0x98` in page 0x15), which
leads to a **13-byte** block (`lbpx` x12 + `retd`) = 26 nibbles in **Q[0x30..0x49]**. Index = P[5D] (0..11). **Game data**:

Layout (byte = nibble pair, lo first, Q address):
| byte | Q | field |
|---|---|---|
| 0 | 30,31 | wake-up hour (0xFF = n/a: egg/baby) |
| 1 | 32,33 | bedtime hour |
| 2 | 34,35 | interval (min) for losing 1/4 heart of **hunger** (reload of Q0:1) |
| 3 | 36,37 | same for **happiness** (Q2:3) |
| 4-5 | 38-3A | 12 bits: reload of the counter Q[D..F] (sickness/poop) — copied to Q0D in `load_form_params` |
| 5 hi | 3B | added to P[48] at 0x612 (`add mx,my`) (?) |
| 6,7 | 3C:3D, 3E:3F | min. and max. weight (BCD) — clamp at 0x6E5 (`label_214`: if weight<min -> min; else if weight>=max -> max) (?) |
| 8-9 | 40-42 | 12 bits: form duration (min) -> Q[10..12] |
| 9 hi | 43 | reload of Q13 (number of heart losses between calls) |
| 10 | 44,45 | (?) Q45 used at 0x657 (`label_282`) |
| 11 | 46,47 | initial weight (BCD) (?) |
| 12 | 48,49 | flag in Q48 (bit3 only tested at 0xDEA, never set in the data), Q49 (?) |

Table (hex values; idx = P[5D]):
```
idx block  wake  sleep hunger happy Q0D-F(min) Q3B min_w max_w duration(Q10-12) Q43 Q44,45 w0 Q48,49
0,1 1564    FF     FF     03   04    02D        C     05       05       03C(60 min)     F   0,8     11    0,0
2   1571    09     14     32   3C    3DE        C     10       99       564             6   0,8     11    0,0
3   157E    09     15     4B   55    678        C     20       99       8AC             6   0,8     11    1,0
4   158B    09     15     4B   55    294        C     20       99       564             6   0,8     11    0,0
5   1598    09     16     51   5B    F3C        8     30       99       FFF             F   F,8     11    1,0
6   15A5    09     16     51   5B    AF8        8     30       99       C30             7   8,8     11    1,0
7   15B2    0B     17     37   41    A20        8     30       99       B40             7   0,B     23    1,0
8   15BF    09     16     3C   46    492        C     20       99       618             F   F,5     43    1,0
9   15CC    09     16     3C   46    168        D     10       99       30C             7   8,8     11    0,0
10  15D9    0A     16     2D   32    294        C     20       99       5A0             7   0,8     11    1,0
11  1598 (same as idx 5)
```
(wake/sleep hour in binary hour: 0x14=20h, 0x15=21h, 0x16=22h, 0x17=23h). 5 and 11 point to the same block (`jp 0x98` at
positions 0x155D and 0x1563). Duration 0xFFF for idx 5/11 (never expires). Form names: see 0x200-0x3FF.md §8 (0=egg,1=Babytchi,2=Marutchi,
3=Tamatchi, 4=Kuchitamatchi, 5..B adults (?)).
Evidence: the RAM at 0x230 of `out/baby0.sav` (`FFFF 3040 D20C 5050 C30F 0811`) is exactly the block at 0x1564; the pair `09,14..17` matches
the schedules; 0x03C=60 min is the Babytchi's time until evolving; Q0D=02D (45) matches the initial value cited in 0x200-0x3FF.md.

## 7. `hour24_to_12_table` — 0x15E6-0x15FD (24 `retd`, indexed by hour 0..23)
`pset 0x15; call; jpba` with B:A = hour (0..23), writes to M[X],M[X+1] the (BCD) byte:
`12,01,02,03,04,05,06,07,08,09,10,11 | 92,81,82,83,84,85,86,87,88,89,90,91` (bit7 = PM). The nibbles are: lo = units, hi = tens (+8 = PM).
Used by the clock screen (render_clock, 0x5xx). Pseudo-C:
```c
uint8_t hour12_bcd(uint8_t h){ uint8_t h12 = h%12 ? h%12 : 12; uint8_t bcd = (h12/10)<<4 | (h12%10); return bcd | (h>=12 ? 0x80:0); }
```

---------------------------------------------------------------------------------------------------

## 8. Bank 1: graphics 0x1000-0x17FF

### 8.1 How the sprite is addressed
`render_asset` (0x000): `A=(M2&7)*4` with carry into B (b:a ∈ {0..0x1C}); `pset 0x10; jp gfx_jp_ret_zp` (0x1020) -> `call gfx_jp_table_jpba`
(0x1023 = `jpba`) falls to `0x1000 + 4*(M2&7)`. Each of the 8 entries (0x1000,0x1004,...,0x101C) is
`ld a,m0 ; ld b,m1 ; pset 0x1k ; jpba`, that is, **it jumps to page 0x10+k, offset `M1:M0`**:

    sprite address = 0x1000 + 0x100*(M2 & 7) + 0x10*M1 + M0         (M0,M1,M2 = RAM 0x000,0x001,0x002)

The code there is a queue of `lbpx mx,imm` ended by `retd`; it **writes directly into RAM at X** (the caller's XP,XH,XL) and returns to the
`call` at 0x1020, which goes to 0x1021 `pset 0; jp set_f_0x07D` (0x009), which re-enables interrupts and returns to render_asset's caller.
On return, X points right after the written data (preserved by `set_f_0x07D`). For the rewrite: no need to emulate; just
extract at build time: `sprite_words(addr)`.

**Format**: 1 word = 1 column of 8 pixels: `imm = 0x9II` (`lbpx`) or `0x1II` (`retd`, last); byte `II`: **bit0 = top row**, bit7 = bottom
row; low nibble = M[X] (rows 0-3), high nibble = M[X+1] (rows 4-7). Length = number of words up to the `retd` (inclusive).
```c
// extracts: returns the number of columns; cols[i] = byte (bit0 = top)
int sprite_read(const uint16_t *rom, uint16_t addr, uint8_t *cols) {
    int n=0; for(;;){ uint16_t w = rom[addr++] & 0xFFF; cols[n++] = w & 0xFF; if ((w>>8)==0x1) return n; /* (w>>8)==0x9: continues */ }
}
```
(Verified by printing 0x16C0/0x1030/0x16E0/0x1600 as ASCII: Babytchi, heart, "0": bit0 = top.)

### 8.2 Sizes / regions
| region | granularity | size | uses / formula |
|---|---|---|---|
| 0x1030-0x14FF (except 0x11E0-0x122F) | blocks of 16 words (`LLLLLLLLLLLLLLLR`) | 16 columns x 8 rows (upper or lower half of a 16x16 pet) | `0x1000 + 0x10*spr`, `spr=(id&0x7F)+3` (0x949-0x95F: M25:M24+3 -> M2:M1, M0=0); the pets are drawn in 2 halves (upper in rows 0-7, lower in 8-15). List in "Useful Addresses.md" |
| 0x1500-0x1527 | 4 words | 4 col. (large digits 0-9) | `0x1500 + 4*d` (`setup_addr_and_render_clock` 0x5F3, M0=4d, M1, M2=5) |
| 0x1528-0x154F | 4-word slots with retd at a variable position | small digits 3 col. (0x1528+4n, n=0..9), each slot 3 columns + an extra `retd` | `render_clock_with_b_0` (0x5EF): `0x1500 + 4*(a+10)` |
| 0x1550-0x1557 | 4 words | filled arrow (0x1550) and hollow (0x1554), 3 col. (+1 empty column) | cursor icons |
| 0x1558-0x15FD | game data (see §6, §7) | | |
| 0x1600-0x17FF | **8 words** (`LLLLLLLR`) | 8 col. x 8 rows glyph | `0x1600 + 8*g`, `g = 16*B + A` (0..63); see 8.3 |
| 0x17E0-0x17FF | blank glyphs | | `g=0x3C..0x3F` |
The blocks are aligned to 16 (or 8) words and the last one is always `retd`; sprites can be read by the same `sprite_read` (natural length).
Non-sprites inside 0x1000-0x17FF: 0x1000-0x102F (dispatcher), 0x11E0-0x122F (code), 0x1550-0x1563 (jp), 0x1564-0x15E5 (data), 0x15E6-0x15FD (table).

### 8.3 8x8 glyphs (0x1600-0x17FF) via `misc_render` (0x7A5, notes 0x400-0x5FF / 0x792 `label_233`)
Input A=g_lo, B=g_hi (nibble pair read from `M[Y],M[Y+1]`): `g=B*16+A`. `B>=4` => `clear_16_addrs` (clears 16 nibbles at X). Otherwise
`M0=(A&1)<<3; M1=(A>>1)|((B&1)<<3); M2=6+(B>>1)` => `0x1600 + 8*g` (checked: A=1 -> 0x1608; B=1 -> 0x1680). Routine 0x7A5 assumes the carry
is clear on entry (`rrc a` without a preceding `rst f`) (?). `label_233` draws 8 glyphs from `M[0x90..0x9F]` (pairs) at increasing X, 16
nibbles (8 columns) each, switching to the 2nd row after 4 (`adc xh,4`). Name of each glyph: agg23's table ("Useful Addresses.md", 0x1600-0x17D8).

### 8.4 Video buffer and mapping to the LCD (to extract/draw)
The E0C6S46 LCD maps VRAM nibble `o` (0xE00+o = rows 0-7... bank 0; 0xE80+o = rows 8-15) like this (cpu.c `set_lcd`): `seg=o>>1`,
`com0=bank*8 + (o&1)*4`, pixel bit `i` of the nibble = `(seg_x, com0+i)`, with `seg_x` from the hw.c table:
`seg_x[40]={0,1,2,3,4,5,6,7,32,8,9,10,11,12,13,14,15,33,34,35,31,30,29,28,27,26,25,24,36,23,22,21,20,19,18,17,16,37,38,39}`
(values >=32 are icons; seg 8 = icons, 17-19, 28, 37-39 also). That is, column x (0..31): x<8: seg x; 8<=x<16: seg x+1; 16<=x<24: seg 52-x; 24<=x<32: seg 51-x.
The game draws into a **RAM buffer in page 1** and `copy_video_buf_to_vram` (0x056) copies it to the VRAM, **packed by columns**: column x of the buffer
is at `B + 2x` (rows 0-7) and `B + 0x80 + 2x` (rows 8-15), where `B = 0x100 + 0x10*M[2B] + M[2A]` (window; `clear_page_0x100` sets M2A=0 => B=0x100;
`label_30` sets B=0x140). So a sprite word written at `X = B+2x` occupies column x, and a 16-column-wide image covers `B+2x .. B+2x+31`.
The buffer has slack of 2 screens (0x100-0x13F+0x180-0x1BF and +0x40), used for sliding the window (entry animation, 0xA00 notes). The icons (seg 8, 17-19,
28, 37-39) are copied separately (`label_242`, mask in M2C:2D).
Whoever writes the sprite sets XP=1 and `X = (M3B:M3A)` = cursor (x,y) (`label_233`, 0x792): `X = 0x100 + 0x10*M3B + M3A`.

---------------------------------------------------------------------------------------------------

## 9. Bank 1: code — initialization
### 9.1 `set_init_mem_and_int` — 0x11E0-0x11F6 (+ continuation 0x244 and 0x1200-0x1221)
Called from `springboard_set_init_mem_and_int` (0x242 -> `pset 0x11; jp 0x1E0`), at reset (0x237) and at soft reset (0x211). Returns to bank 0
via `copy`/`set_init_mem_and_int_bank0_ret` (0x255). Sequence:
1. I/O (XP=F): `F10..F14 = 8,0,1,0,0` (clock timer mask: only 1 Hz; stopwatch off; **prog timer on**; serial/K0 off), `F26..27 = 07` (prog timer reload),
   `F54 = F` (buzzer/outputs off), `F71 = 8,8` (LCD all off; medium contrast), `F76 = 3,2,2,2` (reset clock/stopwatch/prog timer; prog timer at 256 Hz).
2. (0x244, bank 0): clears RAM 0x000-0x06F, 0x100-0x17F, 0x200-0x237, VRAM 0xE50-0xE77 and 0xE80-0xEA7 (nibbles).
3. `set_init_mem_and_int_cont_set_mem` (0x1200): `P[24:25]=0x00` (current frame/sprite), `P[14:15]=0x02,0x00`->**hour=0x20 (>=24 = clock not set)** (?),
   `P[32:33]=0`, `P[36:37]=0x7D` (idle music), `P[58:59]=FF`, `P[7B]=F` (sound on), `P[7D]=F` (interrupts ok), `P[3C]=0` (clock stopped),
   `P[74]=0`, `P[7C]=5` (**egg: 5 min**), `F71=0` (LCD normal), `F78=1` (starts prog timer), reads F00-F02 (clears interrupt factors).
```c
void init_mem_and_int(void){ /* I/O and clearings above */ M[0x024]=M[0x025]=0; M[0x014]=0; M[0x015]=2; M[0x032]=M[0x033]=0; M[0x036]=0xD; M[0x037]=7;
    M[0x058]=M[0x059]=0xF; M[0x07B]=0xF; M[0x07D]=0xF; M[0x03C]=0; M[0x074]=0; M[0x07C]=5; /* F71=0; F78=1; read F00..F02 */ }
```
### 9.2 `copy_6_mx_my` — 0x1222-0x122E
`ldpx my,mx` x6 interleaved with `ldpy a,a` (Y++): copies 6 nibbles from M[X..X+5] to M[Y..Y+5] (used only to save/restore the clock
0x010..0x015 <-> 0x272..0x277 at soft reset, 0x20C and 0x218); returns to bank 0 via 0x229. 0x122F `nop7`.

### 9.3 `gfx_dispatch` — 0x1000-0x102F
0x1000-0x101F: 8 entries of 4 words (see 8.1). 0x1020-0x1022 `gfx_jp_ret_zp`: `call 0x1023; pset 0; jp set_f_0x07D`. 0x1023 `jpba`.
0x1024-0x102F: `nop7` (padding). The first sprite starts at 0x1030.

---------------------------------------------------------------------------------------------------

## 10. Open points (?)
- Fine semantics of events 3 and 4, of P43, Q13/Q14 and of Q4:5 (light/sleep) — described exactly as in the code; game names are hypothetical.
- XH adjustment for the baby at 0x8DF-0x8EF (anim script) and exact use of `M[AA]` (hold) — see the code at 0x8C0-0x8F3.
- Fields Q3B, Q44/45, Q46/47, Q48/49 of the form block (§6): origin/use only partially traced (0x612, 0x657, 0x6E5, 0xDEA).
- Carry-in of `rrc a` in `misc_render` (0x7A7).
- `rom` read by script (python) and checked by trace (baby0.sav) only at the cited points; the tunes and animations were not reproduced in audio/video.
