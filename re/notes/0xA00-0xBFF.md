# ROM 0xA00–0xBFF

Sources: `tools/asm.py 0xA00 0xBFF`, `tamatool/src/lib/cpu.c`, paths exercised in `out/a00` (harness, clock/egg phase only; cov.bin
confirmed A01/ABD/B01/BC0/BCA/BCB/BCC at boot). (?) = hypothesis.

The range has only 3 real code routines (0xABE–0xAFF), one lookup stub (0xABD) and the rest is data: two `retd` tables
(0xA00–0xABC, 0xB00–0xBBF), a pointer table + animation scripts (0xBC0–0xBE7) and three tile "strings"
(0xBE8–0xBFB). 0xBFC–0xBFF = nop7 (padding).

## `retd` table convention (applies to A00, B00, BC0, 98x, C00 …)
`pset P; call X` where X is a `jpba` stub → PC = P<<8 | (B<<4 | A). The word pointed to is `retd v`: it returns to the caller and writes
`M(X)=v & 0xF`, `M(X+1)=v>>4`, and `X += 2` (keeps XP). That is: **B:A = 8-bit index, result = byte in M(X),M(X+1)**
(low nibble first). A `lbpx mx,v` word at the indexed position does the same and **keeps executing** the next
word (which will be another lbpx/retd): it writes several consecutive bytes. This is how the "scripts" below change the pointer.

---

## 0xA00–0xABC  `pose_sprite_table`  (agg23: "Jump table for jp_table_0xA00")
Table of 12 rows x 16 columns, index = `(M5D << 4) | M5E` (M5D = character/stage id 0..B, M5E = "pose mode"
1..A; col 0 and cols > A are 0/unused). Value `v`: **M6E = v & 0xF, M6F = v >> 4** = sprite ids of frame A and frame B of the
pose (the drawing picks M6E or M6F according to bits of the script, see 0xBCA). (?) exact meaning of the ids: they index the sprite table
in C00/D00 via `label_288/289` (0x928/0x939), outside the range.
Row 0 (egg) only has cols 0–4.

```
M5D\M5E  0  1  2  3  4  5  6  7  8  9  A  B
 0      00 10 21 33 44
 1      00 15 12 33 43 45 00 41 61 13 33
 2      00 15 03 87 B9 25 A9 B1 47 78 88
 3      00 13 02 44 65 87 99 A1 CB BB BB
 4      00 10 03 65 47 22 77 42 87 78 88
 5      00 11 02 33 24 51 76 81 76 66 66
 6      00 21 03 44 35 11 85 60 75 75 55
 7      00 10 32 55 26 77 98 47 26 66 66
 8      00 10 02 33 24 76 54 80 94 94 44
 9      00 21 43 55 36 87 96 3A BB 5B BB
 A      00 01 12 33 24 22 54 22 64 54 44
 B      00 10 02 33 21 44 55 44 51 11 11 11   (only row with col B; ends at 0xABB; 0xABC = retd 0)
```
(values in hex, byte `v`; e.g. M5D=1,M5E=1 -> 0x15 -> M6E=5, M6F=1.)

Readers: `label_142` 0x46F (X=0x6E, B:A = M5D:M5E -> M6E/M6F) and `label_125` 0x3DF (A=4, B=M6 = character id,
X=0 -> M0/M1; uses M1 = frame-B sprite as a static portrait; used in the evolution animation, 0x392–0x39B).

### 0xABD  `lookup_pose_sprites` (agg23 `jp_table_0xA00`)
`jpba` stub. Call with `pset 0xA; call 0xABD`. Input B:A, X=destination; output M(X),M(X+1). Callers: 0x3DF, 0x46F.

---

## 0xABE–0xAD9  `menu_light`  (labels 297–300)  — "light" menu
Entry: `jp` from the menu dispatcher (table of `pset;jp` at 0x700, entry 0x704; see 0x54B..0x549). Ends with `jp label_160`
(0x51A, menu loop) or `label_159` (0x517, leaves the menu). Does not return with `ret`.
Pseudo-C:
```c
void menu_light(void) {                    // 0xABE
    M[0x78] = (M[0x4B] & 1) ^ 1;           // initial cursor: 0 if the light is on, 1 if off
    copy_tiles(0x90, tiles_light);         // call 0xBF0, writes 16 nibbles to M[0x90..0x9F]
    flags = choose_2(/*B=*/0xA);           // label_352 @0xE5A; A = option (0/1), M[0x78] = cursor
    if (!C) {                              // M pressed (NC,Z): confirms
        if (A == 0) M[0x4B] = 0xF;         // option 0: light ON   (M4B: F = on, 0 = off)
        else { M[0x4B] = 0; M[0x205] = 0xF; } // option 1: light OFF; M205 = F ("light turned off" event?) (?)
        goto menu_loop;                    // label_160
    }
    if (!Z) goto menu_exit;                // C,NZ = timeout/exit (label_159)
    goto menu_loop;                        // C,Z = R pressed (cancel)
}
```
RAM: 0x4B (light state, bit0 used), 0x78 (cursor/choice, output of choose_2), 0x205 (?, set when turning off).
Calls: label_309 (0xBF0), label_352 (0xE5A).

## 0xADA–0xAEF  `menu_discipline`  (labels 301–304) — "discipline" icon
Entry: `jp` from 0x70F (slot 0x70E of the menu table). Ends in `label_160`.
```c
void menu_discipline(void) {               // 0xADA
    if (!(M[0x4A] & 8)) {                  // high bit of 0x04A = (?) pet sleeping/unable to be disciplined
        if (M[0x209] != 0) {               // (?) flag "pet threw a tantrum / attention call pending"
            M[0x209] = 0;
            M[0x43] = sat_add_nib(M[0x43], 4);   // +4, saturates at 0xF  -> discipline meter (4 units = 1 bar) (?)
            sound(0xF0);                   // if_0x7B_set_clear_0x32_store_yhl_set_0x059_to_0xF with Y=0xF0 (only plays if M[0x7B]!=0)
        }
        play_reaction(/*M5E*/8, /*M57*/5, /*Y*/3);  // label_273 @0x8A9: runs the script of table E00 (Y=3) with pose mode 8
    }
    goto menu_loop;                        // label_160
}
```
RAM: 0x4A bit3, 0x209, 0x43 (0x043: discipline), 0x7B (sound on?), 0x5E/0x57 (via label_273).

## 0xAF0–0xAFF  `blit_buffer_slide` (labels 305/306; call `label_305`)
Repeatedly copies the video buffer (page 0x100) to the VRAM, **sliding the origin** of the copy; it is the "scroll" animation
(screen clear/entry). Input: A = limit (high nibble of the final offset), B = step (nibbles). Exits with `ret`.
```c
void blit_buffer_slide(u4 A, u4 B) {       // 0xAF0
    M[0] = A;                              // m0 = A (clobbers M[0])
    M[0x2A] = M[0x2B] = 0;                 // origin offset = 0   ({M2B,M2A} is the low byte of the 0x1xx pointer)
    for (;;) {
        copy_video_buf_to_vram();          // calz 0x56: reads from 0x100 + {M2B,M2A} -> 0xE00/0xE80...
        if (M[0x2B] >= M[0])  return;      // cp mx,a ; jp nc
        {M[0x2B],M[0x2A]} += B;            // add mx,b ; adc mx,0
    }
}
```
No explicit wait: the time comes from the cost of the copy (CPU at 32 kHz). Equivalent to `label_156` (0x500) but counting up.
Callers: 0x3C7 (A=2,B=2), 0x4FB (A=4,B=4), 0xD88 (A=4,B=4; toilet/clean poop: after `M[0x4D]=0`).
Effects: M0, M2A, M2B, VRAM. Calls calz `zero_a_xp`, `copy_video_buf_to_vram`.

---

## 0xB00–0xBBF  `pose_anim_table`  (agg23 "Jump table for jp_table_0xB00")
Same format/index as A00: `(M5D<<4)|M5E`. Value `v`: **M0 = v & 0xF = frame script id** (index 0..7 in table 0xBC0),
**M1 = v >> 4 = movement script id** (index into the page-9 table at 0x98B+M1, which gives the pointer M6A/M6B — outside the range).
```
M5D\M5E  0  1  2  3  4  5  6  7  8  9  A  B
 0      00 00 A1 00 00
 1      00 20 00 30 11 02 05
 2      00 40 00 00 11 02 00 00 03
 3      00 00 07 51 11 02 05 00 06 05
 4      00 60 00 00 11 04 05
 5      00 05 07 00 11 02 00 00 00 05
 6      00 70 07 80 11 04
 7      00 70 07 50 11 04 00 00 00 05
 8      00 40 07 50 11 02
 9      00 60 00 80 11 02
 A      00 00 07 50 11 04 00 05 00 05
 B      00 90 07 50 11 04 05 05 00 05      (frame-script ids used: 0..7)
```
(empty cells = 0x00; row 0 col 2 = 0xA1 -> script 1, movement 0xA.) In col 3/4 most are 0x11 (same script/mov. in poses 4).
Readers: `label_142` 0x474 (X=0, B:A = M5D:M5E, tail to 0xBC0 afterwards) and, for the script bytes, `label_145` via
`jp_table_0xB00_2` (0x4E3).

### 0xBC0–0xBC9  `anim_script_ptr_table`
10 `retd` entries, index = M0 (script id). Value = offset (in page B) of the script:
`CA, CD, D0, D3, D8, DB, DE, E3, E8, E8`. Entries 8 and 9 (E8) point to tile data and are never used (B00 only gives 0..7).
`label_142` (0x47A–0x481) does `jpba` to 0xBC0+M0 with X=0x64 -> **M64/M65 = script pointer** (M64 = low nibble).

### 0xBCA–0xBE7  `anim_scripts` (script bytes read one by one by `label_145`, 0x486–0x495)
Execution per animation tick (`label_145`):
```c
if (M5F-- != 0) goto movement;             // add mx,0xF ; jp c : frame counter M5F still > 0
p = {M65,M64}; {M65,M64}++;                // 8-bit pointer in page B
pset 0xB; call (0xB00 | p)  with X=0x62;   // retd v  -> M62=v&F, M63=v>>4  (and, if lbpx+retd: M64/M65 = new pointer)
M5F = M62;                                 // step duration = M62 + 1 ticks
```
Format of a step: **M62 (low nibble) = duration-1**; **M63 (high nibble) = drawing flags**:
bit1 = uses sprite M6F (frame B) in the upper half, bit0 = same in the lower half (otherwise M6E),
bit3 = mirror upper half (XOR with M70.bit1), bit2 = mirror lower half (XOR with M70.bit0).
(derived from 0x4A8–0x4E1, which calls label_288/289 in page 9; (?) which half is "upper/lower" — the first drawing uses `fan b,2`, the second `fan b,1`.)
An `lbpx v; retd p` pair writes 4 nibbles = step `v` **and** M64/M65=`p`: it is the script's "goto p" (loop).

Scripts (address = offset in page B; `;loop` = lbpx+retd that goes back to the given address):
```
id  entry    steps (v = hi:lo)                            effect
0   BCA      01  ; 31 -> loop BCA                         2 ticks A (M63=0), 2 ticks B (M63=3): alternates frames A/B. (egg)
1   BCD      07  ; 37 -> loop BCD                         8 ticks A, 8 ticks B (slow)
2   BD0      00  ; 30 -> loop BD0                         1 tick each (fast)
3   BD3      01 31 01 ; F1 -> loop BD3                    A,B,A, then mirrored B (F1: flips + frame B)
4   BD8      00 ; F0 -> loop BD8                          1 tick A/ 1 tick mirrored (M63=F = both flips + B)
5   BDB      01 ; F1 -> loop BDB
6   BDE      01 31 01 ; B1 -> loop BDE                    B1 = flip only the upper half + frame B
7   BE3      01 31 01 ; 31 -> loop BE6 (stays at 0x31)    A,B,A and stays on B
```
Words: BCA:retd 01, BCB:lbpx 31, BCC:retd CA | BCD:retd 07, BCE:lbpx 37, BCF:retd CE | BD0:retd 00, BD1:lbpx 30, BD2:retd D0 |
BD3:01 BD4:31 BD5:01 BD6:lbpx F1 BD7:retd D3 | BD8:00 BD9:lbpx F0 BDA:retd D8 | BDB:01 BDC:lbpx F1 BDD:retd DB |
BDE:01 BDF:31 BE0:01 BE1:lbpx B1 BE2:retd DE | BE3:01 BE4:31 BE5:01 BE6:lbpx 31 BE7:retd E6.
Careful: the pair's `lbpx v` becomes a step (M62/M63) and the `retd p` becomes M64/M65 (X advances 4 nibbles: 62,63,64,65).
Confirmed at boot (egg, M5D=0,M5E=1 -> script 0): cov shows BCA 5x, BCB 4x, BCC 4x (alternation).

---

## 0xBE8–0xBFB  tile strings (`call`able routines that only do `lbpx`; they write to RAM at X and return via `retd`)
Each writes 16 nibbles (8 8-bit tile ids) starting at M(X) (the caller sets X first). Values = tile ids (0xFF = blank).
| address | label | name | bytes written | use |
|---|---|---|---|---|
| 0xBE8–0xBEF | label_308 | `tiles_food_choice` | FF 28 29 2A FF 2B 2C 2D | food menu (two 3-tile options, [marker+icon]x2); caller 0x719 with X=0x90 |
| 0xBF0–0xBF7 | label_309 | `tiles_light_choice` | FF 38 39 FF FF 0E 17 FF | light menu; caller 0xAC7 with X=0x90 |
| 0xBF8–0xBFB | label_310 | `tiles_happy_header` | 31 32 33 FF | header of the "happy" status screen (M41); caller 0x88A (see 0x883 label_264 with 2E 2F 30 FF for "hungry") |
Layout of the first two: the first 4 tiles = option 0, the next 4 = option 1; `label_352` overwrites the 1st tile of the selected
half with 0x27 (cursor) and puts FF in the other one (0xE68–0xE70), then redraws (label_233). Visual meaning of the ids (?).

---

## Relevant external routines used by this range (outside the range, contract only)
- `choose_2` = `label_352` @0xE5A: 2-option selector. Input: M90..M9F with tiles, M78 = initial cursor, B = timeout (goes to M77/M57 via label_66).
  Output: A = option, M78 = cursor. Flags: **NC,Z = confirmed (M)**; **C,Z = cancelled (R)**; **C,NZ = timeout/finished**. L toggles the cursor (M75 mod M76, via label_94 @0x2C3).
- `copy_video_buf_to_vram` = calz 0x56 (0x056): copies page 0x100 (starting at `{1,M2B,M2A}`) to 0xE00/0xE80.
- `play_reaction` = `label_273` @0x8A9: M5E←A, M57←B, Y=id of the script in table E00 that sequences pose modes.
- `anim_set_mode` = `label_142` @0x465 (uses tables A00/B00/BC0); `anim_step` = `label_145` @0x486.

## Summary RAM map (addresses read/written in this range)
| addr | meaning |
|---|---|
| 0x5D | character/stage id (row of tables A00/B00; 0=egg, 1=baby, …) |
| 0x5E | pose mode (column): 1 normal (label_176 @0x59x), 3 if M4A.bit3, 9 if M48.bit3, 4 portrait, 8 scolding… (?) |
| 0x5F | counter of remaining ticks of the frame-script step |
| 0x62/0x63 | current script step: duration-1 / drawing flags |
| 0x64/0x65 | frame-script pointer (offset), page B |
| 0x6A/0x6B | movement-script pointer (page 9), comes from 0x98B+M1 |
| 0x6E/0x6F | sprite ids frame A / frame B of the pose (table A00) |
| 0x0/0x1 | temporary output of tables B00 (script, movement) and A00 in label_125 |
| 0x2A/0x2B | offset of the buffer→VRAM copy origin (blit_buffer_slide) |
| 0x4B | light (F = on, 0 = off) |
| 0x205 | (?) flag set when the light is turned off |
| 0x4A bit3 | (?) pet sleeping/indisposed; blocks discipline |
| 0x209 | (?) pending discipline/attention (cleared on disciplining) |
| 0x43 | discipline meter (+4 per action, saturates at F) (?) |
| 0x75/76/77/78 | cursor, number of options, timeout, choice of the 2-option selector |
| 0x90–0x9F | menu tile row (filled by the strings BE8/BF0) |
| 0x7B | sound enabled (?) — used by if_0x7B_set… before playing buzzer 0xF0 |
