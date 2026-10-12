# ROM 0xC00-0xDFF (12-bit words; addresses = word index, as in tama.asm)

Summary: the range has (1) the 1-minute tick routine (`label_312`), (2) counter helpers,
(3) two tables of pet sprites (C = upper half, D = lower half), (4) the evolution rules, (5) the end of the
post-evolution/hatching routine (`label_347`, continues at 0xE00 -> `label_214` at 0x638).
Convention: `M[xp:addr]`; `0x2xx` = RAM page 2 (xp=2). 4-bit nibbles. "BCD/decimal" is never used here
(all the routines start with D=0: `rst f,0xB`).

Note on `retd`: `retd l` does `M(X)=l&0xF; M(X+1)=l>>4; X+=2` and returns. `lbpx mx,l` does the same without returning.
That is why "tables" here are sequences of `retd` (1 byte per entry, accessed by `call` + `jpba`), and
the `lbpx...retd` stretches are **blocks of 8 bytes/nibbles written to RAM at X** (the disassembler shows them as code).
`jpba` = `PC = (NPP<<8) | B<<4 | A` (NPP comes from the last `pset`). So `pset C; call jp_table_0xC00` with B:A=index
executes the `retd` at position `0xC00 + index`.

---

## 1. `minute_tick` (label_312) — 0xC7A–0xCC7 (+ falls into 0xCC8–0xCD1)
Call: `clock_timer_int` (0x148, `pset 0xC; call label_312`), once per minute (when seconds 0x10/0x11 overflow 60 and
minutes 0x12/0x13 increment; only runs if 0x03C != 0, i.e. clock set). Enters with xp=0, D=0. Returns with xp possibly 2
(the caller readjusts xp).

Does the per-minute "game time" bookkeeping: egg countdown, and (if awake) all the pet's timers.

RAM:
- `M[7C]`: egg/game state. 0 = nothing to do on this tick; 1..E = countdown in minutes (initial value 5 at reset, 0x1215;
  it is the hatching time: 5 minutes); F = game active (pet born; `label_391` at 0xFEC sets F on hatching). Confirmed in the trace: 5->0 in 5 min, then F.
- `M[4A]` bit3 = sleeping (set F in `label_102` 0x30C, cleared at 0xF20 on waking).
- `M[5D]` = species/stage (see §6); `==1` = Babytchi.
- `M[48]` bit3 = (?) "poop present" (set F at 0xF8A when `M[20D..20F]==0`, see below).
- `M[40]`,`M[41]` = hunger/happiness hearts in 1/4 hearts (0..F, 4 = 1 heart). They decay by 4 at 0xF99/0xFAE when the timers 0x200/0x202 reach zero.
- Timers (xp=2): `0x200:0x201` (8 bits, hunger countdown; reload=3 at 0xF95), `0x202:0x203` (8 bits, happiness countdown; reload=4 at 0xFAA),
  `0x206:0x207` (8 bits, 3rd countdown, reload at 0x337/0x33C), `0x208` and `0x209` (nibbles, count up to F only if !=0; 0xFC9 clears 0x208 on reaching F and fires an event),
  `0x20A:0x20B:0x20C` (12 bits, "minutes with an empty heart"), `0x20D:0x20E:0x20F` (12 bits, poop countdown (?) — decrements;
  if `M[48]` bit3 is set, increments), `0x210:0x211:0x212` (12 bits, remaining stage time; decrements; initial copied from 0x240.. in `label_347`),
  `0x215` (nibble, sleeping Babytchi; init 5), `0x216:0x217` (8 bits, awake Babytchi; init 0x28), `0x204:0x205` (8 bits, minutes slept (?); `label_102` starts it at 0x00 if baby, otherwise 0x3D).

Pseudo-C:
```c
void minute_tick(void) {           // D=0
    if (M[0x7C] == 0) return;
    if (M[0x7C] != 0xF) { M[0x7C]--; return; }        // egg countdown
    if (M[0x04A] & 8) {                                // sleeping
        if (M[0x05D] == 1) { if (M[0x215]) M[0x215]--; }          // baby: 0x215 = max(0x215-1,0)
        else { sat_inc8(0x204); }                                 // adult: 0x204:5 += 1, stops at 0xFF
        return;
    }
    if (M[0x05D] == 1) dec8_sat(0x216);                // awake baby (8-bit dec, stops at 0)
    dec8_sat(0x200); dec8_sat(0x202);
    inc_nz_sat4(0x208); inc_nz_sat4(0x209);            // +1 if !=0, stops at F
    dec8_sat(0x206);
    if (M[0x40] == 0 || M[0x41] == 0) inc12(0x20A);    // some heart empty
    else { M[0x20A] = M[0x20B] = M[0x20C] = 0; }
    if (!(M[0x048] & 8)) dec12_sat(0x20D); else inc12(0x20D);
    dec12_sat(0x210);
}
```
(In the "sleeping/!baby" branch the code does `M204++; M205 += carry; if overflow M204=M205=0xF`.)
Evidence: cov.bin/trace (decrements every minute, 0x7C 5->0, 0x216 0x28->0, 0x20D vs M48).

## 2. Counter helpers (all expect the right xp; use X as a pointer, low nibble first)
| address | label | proposed name | effect |
|---|---|---|---|
| 0xCC7–0xCD1 | label_323/label_324 | `dec12_sat` (323 = with X=0x210) | decrements 12 bits at M[X..X+2], saturates at 0 (uses `adc xl,0xE` to go back X and `lbpx mx,0; ld mx,0`). Returns C=1 if it did not saturate. |
| 0xCD2–0xCD8 | label_326 | `dec8_sat` | same for 8 bits M[X],M[X+1] (`adc xl,0xF`). |
| 0xCD9–0xCDE | label_328 | `inc_nz_sat4` | `if (M[X] != 0 && M[X] != 0xF) M[X]++;` |
| 0xCDF–0xCE4 | label_330 | `inc12` | M[X..X+2] += 1 (12 bits, no saturation), returns (C of the last addition). |
Callers: label_326 only in minute_tick (X=0,2,6 with xp=2); label_324 also at 0x743 (feeding a snack: `dec12_sat(0x20D)`, i.e. eating advances the poop (?)); label_330 only in minute_tick (X=0x20A and 0x20D).

## 3. `add_heart` (label_331) — 0xCE5–0xCF4
Input: X = 0x40 (hunger) or 0x41 (happiness), xp=0. `M[X] += 4; if (carry) M[X]=0xF;` (+1 heart, saturates). Then, if `M[40]!=0 && M[41]!=0`
clears `M[0x208]` (xp=2) and restores xp=0.
Callers: 0x6B2 (end of the minigame: `M[83]>=3` => +happiness at 0x41), 0x734 (meal: X=0x40, only if `M[40]!=F`), 0x73B (snack: X=0x41).
```c
void add_heart(int X) { M[X] += 4; if (M[X] > 15) M[X] = 15;
    if (M[0x40] && M[0x41]) M[0x208] = 0; }
```

## 4. `init_result_tiles` (label_334) — 0xCF5–0xCFC
Writes 16 nibbles to M[0x90..0x9F] (called with X=0x90, xp=0, from 0x69D): pairs (a,b) = 8x8 tile indices for `fetch_and_misc_render`
(blocks 0x16xx/0x17xx, routine 0x792 `label_233` draws 8 tiles from M90..9F): `18 FF 1D FF 00 0F 00 FF`
-> [pet icon 0x16C0][blank][empty heart 0x16E8][blank][digit 0x1600+8d][VS 0x1678][digit][blank].
The caller (0x6A0) then does `M[98]=M[83]` (hits) and `M[9C]=5-M[83]` (misses): result screen of the minigame "X VS Y" (?).
0xCFD–0xCFF: `nop7` (padding).

## 5. Pet sprite tables: C (0xC00–0xC78) and D (0xD00–0xD7C); dispatchers 0xC79 and 0xD7D
- `jp_table_0xC00` (0xC79) and `jp_table_0xD00` (0xD7D) are just `jpba`. Use: `pset C|D; call jp_table_0x?00` with B:A = index -> writes
  `M[X]=lo nibble, M[X+1]=hi nibble` of the table byte.
- Only caller: `label_288` (0x928, upper half, table C) and `label_289` (0x939, lower half, table D); both end in
  `label_290/label_291` (0x949–0x97E, outside this range). Protocol:
```c
// input: A=frame (-> M0), B=mirror flag (bit3 -> m3), M[5D]=species
uint8_t base  = tblX[M[0x5D]];                 // X=1: M1=lo, M2=hi        (first 12 entries = pointers)
uint8_t id    = tblX[base + frame];            // X=0x24: M24=lo, M25=hi
id ^= (m3 & 8) << 4;                           // bit7 = mirror horizontally; B&8 inverts
uint8_t spr   = (id & 0x7F) + 3;               // (M25&7):(M24)+3
if (spr >= 0x80) return;                       // id 0x7F => does not draw (Babytchi upper)
asset_addr = 0x1000 + 0x10*spr;                // render_asset: m2=spr>>4 (page), m1=spr&F, m0=0
render_asset -> video buffer (xp=1) at X=(M3A,M3B)   // 32 nibbles = 16 columns x 8 rows
if (id & 0x80) mirror16columns();              // swaps nibble pairs (X+2k) <-> (X+30-2k), 8 iterations (0x973-0x97D)
```
- Both tables have the same structure: 12 pointers (bytes at 0x00–0x0B) to the start of the list for each M5D (0..11), then lists of ids.
  Number of frames per species (the same in C and D): `[5,7,12,13,9,9,9,10,10,12,7,6]`. The frame index is the same for both halves.
  Table C: 12 pointers at 0x00–0x0B, lists at 0x0C–0x78 (0x79 is the `jpba`). Table D: pointers at 0x00–0x0B, bytes 0x0C–0x0F = `7D` (padding, unused),
  lists at 0x10–0x7C (0x7D is the `jpba`). List 1 of C (Babytchi) is just `7F` (no upper half; the Babytchi is drawn whole by table D).
- C pointers: `0C 11 18 24 31 3A 43 4C 56 60 6C 73`; D: `10 15 1C 28 35 3E 47 50 5A 64 70 77`.
- Sprites = graphics data 0x1030–0x14F0 (see "Useful Addresses.md"); `m` = mirroring flag (bit7 of the id).
  Frame -> pose is not resolved (depends on the caller; probably 0=idle, then eat/happy/sleep...). Species map M5D (?): 0 egg, 1 Babytchi, 2 Marutchi, 3 Tamatchi, 4 Kuchitamatchi, 5 Mametchi, 6 Ginjirotchi, 7 Maskutchi, 8 (?)Tarakotchi, 9 Nyorotchi, 10 (?)Kuchipatchi, 11 Bill.
  Observed: M5D=0 egg, 1 after hatching, 2 after evolving from the baby (trace).

#### Table C (upper half): list per species

| M5D | species (?) | start idx | frames: idx -> id(hex) -> asset |
|---|---|---|---|
| 0 | egg (+sparkle/angel frames) | 0x0C | 0:43=1460 1:45=1480 2:47=14A0 3:49=14C0 4:4B=14E0 |
| 1 | Babytchi | 0x11 | 0:7F=1820 1:7F=1820 2:7F=1820 3:7F=1820 4:7F=1820 5:7F=1820 6:7F=1820 |
| 2 | Marutchi | 0x18 | 0:0C=10F0 1:09=10C0 2:09=10C0 3:0A=10D0 4:0A=10D0 5:07=10A0 6:8Cm=10F0 7:0D=1100 8:0D=1100 9:0F=1120 10:8Fm=1120 11:10=1130 |
| 3 | Tamatchi | 0x24 | 0:0C=10F0 1:09=10C0 2:0A=10D0 3:11=1140 4:0D=1100 5:0F=1120 6:10=1130 7:11=1140 8:09=10C0 9:0F=1120 10:10=1130 11:0D=1100 12:0A=10D0 |
| 4 | Kuchitamatchi | 0x31 | 0:17=11A0 1:18=11B0 2:18=11B0 3:19=11C0 4:19=11C0 5:1A=11D0 6:1A=11D0 7:9Am=11D0 8:9Am=11D0 |
| 5 | Mametchi | 0x3A | 0:20=1230 1:A2m=1250 2:24=1270 3:A6m=1290 4:A6m=1290 5:22=1250 6:A6m=1290 7:26=1290 8:24=1270 |
| 6 | Ginjirotchi | 0x43 | 0:27=12A0 1:28=12B0 2:29=12C0 3:2B=12E0 4:2C=12F0 5:ACm=12F0 6:2B=12E0 7:ACm=12F0 8:2C=12F0 |
| 7 | Maskutchi | 0x4C | 0:2D=1300 1:2D=1300 2:30=1330 3:31=1340 4:30=1330 5:32=1350 6:32=1350 7:33=1360 8:34=1370 9:B4m=1370 |
| 8 | (?) Tarakotchi - Kuchitamatchi-style head + Mametchi legs | 0x56 | 0:17=11A0 1:18=11B0 2:19=11C0 3:1A=11D0 4:9Am=11D0 5:1A=11D0 6:B8m=13B0 7:38=13B0 8:19=11C0 9:9Am=11D0 |
| 9 | Nyorotchi | 0x60 | 0:17=11A0 1:17=11A0 2:18=11B0 3:19=11C0 4:17=11A0 5:1A=11D0 6:9Am=11D0 7:18=11B0 8:98m=11B0 9:97m=11A0 10:98m=11B0 11:9Am=11D0 |
| 10 | (?) Kuchipatchi | 0x6C | 0:35=1380 1:35=1380 2:36=1390 3:37=13A0 4:37=13A0 5:B7m=13A0 6:37=13A0 |
| 11 | Bill/Oyajitchi (stage 4) | 0x73 | 0:3C=13F0 1:3E=1410 2:40=1430 3:41=1440 4:42=1450 5:41=1440 |

#### Table D (lower half): list per species

| M5D | species (?) | start idx | frames: idx -> id(hex) -> asset |
|---|---|---|---|
| 0 | egg (+sparkle/angel frames) | 0x10 | 0:44=1470 1:46=1490 2:48=14B0 3:4A=14D0 4:4C=14F0 |
| 1 | Babytchi | 0x15 | 0:05=1080 1:01=1040 2:02=1050 3:03=1060 4:04=1070 5:00=1030 6:06=1090 |
| 2 | Marutchi | 0x1C | 0:08=10B0 1:0B=10E0 2:08=10B0 3:08=10B0 4:0B=10E0 5:08=10B0 6:08=10B0 7:08=10B0 8:0B=10E0 9:08=10B0 10:08=10B0 11:0B=10E0 |
| 3 | Tamatchi | 0x28 | 0:14=1170 1:92m=1150 2:13=1160 3:12=1150 4:15=1180 5:13=1160 6:12=1150 7:13=1160 8:93m=1160 9:12=1150 10:16=1190 11:12=1150 12:92m=1150 |
| 4 | Kuchitamatchi | 0x35 | 0:08=10B0 1:0B=10E0 2:08=10B0 3:08=10B0 4:0B=10E0 5:08=10B0 6:0B=10E0 7:08=10B0 8:0B=10E0 |
| 5 | Mametchi | 0x3E | 0:21=1240 1:A3m=1260 2:25=1280 3:15=1180 4:21=1240 5:A3m=1260 6:A3m=1260 7:A3m=1260 8:16=1190 |
| 6 | Ginjirotchi | 0x47 | 0:21=1240 1:A3m=1260 2:2A=12D0 3:25=1280 4:15=1180 5:21=1240 6:16=1190 7:25=1280 8:21=1240 |
| 7 | Maskutchi | 0x50 | 0:2E=1310 1:2F=1320 2:2F=1320 3:2E=1310 4:AEm=1310 5:15=1180 6:2E=1310 7:2E=1310 8:2E=1310 9:2E=1310 |
| 8 | (?) Tarakotchi - Kuchitamatchi-style head + Mametchi legs | 0x5A | 0:21=1240 1:25=1280 2:25=1280 3:15=1180 4:21=1240 5:21=1240 6:A3m=1260 7:23=1260 8:16=1190 9:25=1280 |
| 9 | Nyorotchi | 0x64 | 0:39=13C0 1:3A=13D0 2:39=13C0 3:39=13C0 4:3B=13E0 5:3B=13E0 6:3A=13D0 7:3B=13E0 8:3B=13E0 9:3A=13D0 10:BAm=13D0 11:3B=13E0 |
| 10 | (?) Kuchipatchi | 0x70 | 0:AEm=1310 1:2E=1310 2:AEm=1310 3:15=1180 4:2E=1310 5:2E=1310 6:2F=1320 |
| 11 | Bill/Oyajitchi (stage 4) | 0x77 | 0:3D=1400 1:3F=1420 2:2F=1320 3:15=1180 4:2E=1310 5:3D=1400 |

Legend: `idx:id=asset`; `id` with suffix `m` = bit7 (mirrored); `asset = 0x1000+0x10*((id&0x7F)+3)`; `1820`/`7F` = nothing drawn.
Egg frames: 0=egg position 1, 1=egg position 2, 2=cracking, 3=sparkle, 4=angel (dead) (?).

## 6. Evolution

### 6.1 `label_346` — 0xDC6–0xDD1 `load_evolution_rules`
Input: A = `M[50]` (current branch/stage, 0..6), called from 0x357 (`label_112`, event M5C=5 "evolve"), `pset 0xD`.
`X=0x90; B=0; A+=0xB; B=0xC+carry; jpba` -> PC=0xDCB+A: table of 7 `jp` (0xDCB–0xDD1) to `label_339..345`.
Each target writes to M[0x90..] a list of 4-nibble rules `(min_A, min_B, newM50, newM5D)` and returns (`retd` at the end).
It can go beyond 0x9F (the rule for M50=2 has 6 entries = 24 nibbles; it reaches 0xA7).

| current M50 | label (addr.) | rules (minA, minB -> M50', M5D') |
|---|---|---|
| 0 (baby) | 339 (0xD9C) | (0,0 -> 1, 2) |
| 1 (Marutchi) | 340 (0xD9E) | (3,3 -> 5,4) (3,0 -> 4,4) (0,3 -> 3,3) (0,0 -> 2,3) |
| 2 (Tamatchi) | 341 (0xDA6) | (3,4 -> F,A) (3,2 -> F,9) (3,0 -> F,8) (0,2 -> F,7) (0,1 -> F,6) (0,0 -> F,5) |
| 3 | 342 (0xDB2) | (4,8 -> F,A) (4,0 -> F,9) (0,2 -> 6,7) (0,0 -> F,6) |
| 4 (Kuchitamatchi) | 343 (0xDBA) | (0,3 -> F,A) (0,2 -> F,9) (0,0 -> F,8) |
| 5 | 344 (0xDC0) | (0,6 -> F,A) (0,0 -> F,9) |
| 6 | 345 (0xDC4) | (0,0 -> F,B) |
Bytes (low nibble first): 339: `00 21`; 340: `33 45 03 44 30 33 00 32`; 341: `43 AF 23 9F 03 8F 20 7F 10 6F 00 5F`;
342: `84 AF 04 9F 20 76 00 6F`; 343: `30 AF 20 9F 00 8F`; 344: `60 AF 00 9F`; 345: `00 BF`.
Selection (in `label_112`, 0x35C–0x36C):
```c
a = M[0x42]; b = M[0x51]; load_evolution_rules(M[0x50]);   // rules in M[0x90..]
for (r = 0x90; ; r += 4)                                    // first rule that matches (the most demanding come first)
    if (a >= M[r] && b >= M[r+1]) { M[0x50] = M[r+2]; M[0x5D] = M[r+3]; break; }
```
Meanings (?): `M[50]` = evolution branch (F = final/adult), `M[42]`/`M[51]` = care scores (0 without interaction: fell into (0,0) in the trace).
M5D=2 and M50=1 after the baby (confirmed).

### 6.2 `label_347` — 0xDD2–0xDFF `post_evolution_setup` (continues: `pset 6; jp label_214` at 0xDFE/DFF)
Entry points: `jp` from 0x373 (end of `label_112`, evolution) and from 0xFFE (hatching, `jp label_347`). Effects:
```c
M[0x49] = 0;                                   // (?) count associated with poop/cleaning
// renders the species icon: m0 = (M5D+8)&0xF, m1 = 5 + carry, m2 = 5 -> gfx 0x1500 + (m1:m0)
//   = jump table at 0x1558+M5D (jp label_397..406) for the species' portrait, drawn at M[0x230..0x23F]
render_asset(dest=0x230);
a = M[0x048];                                   // BEFORE touching the timers
M[0x210..0x212] = M[0x240..0x242];              // stage time (copy_3_mx_my_ret; 0x07D=0 during the copy)
if (!(a & 8)) M[0x20D..0x20F] = M[0x238..0x23A];// poop timer is only reloaded if there is no poop
M[0x213] = M[0x243];  M[0x043] = M[0x244];      // (?) stage parameters (species table loaded earlier by label_142/271 into RAM 0x240..)
if (M[0x50] == 2 || M[0x50] == 4) M[0x043] = 8;
goto label_214;                                  // 0x638 (back to the main loop)
```
The data 0x238..0x244 comes from a per-species table (jp_table_0xE00 / `label_142`, outside this range).

### 6.3 `label_336` — 0xD7E–0xD93 (target #5 of the 0x700 jump table, `label_164` dispatches via `pset 7; jpba` with B:A=2*index)
Menu screen/handler (probably the toilet/cleaning icon (?)); if sleeping (`M[4A]` bit3) goes back to the loop (`pset 5; jp label_160`).
Otherwise: xp=1, `label_338(0x140)` and `label_338(0x1C0)` write a fixed pattern into the video buffer (page 0x100), calls `label_305` (pset A, A=4,B=4),
and if `M[4D]!=0`: `M[4D]=0` and draws `A=7,B=5,Y=6` via `label_273` (pset 8). Coverage obtained in the trace only by pressing L 8x + M (could not see the screen).
`label_338` (0xD94–0xD9B) = 8 bytes `44 EE BB 55 AA 11 00 00` written to M[X..X+15].

## 7. Address map of the range
| range | contents |
|---|---|
| 0xC00–0xC78 | table C (pointers + upper ids) |
| 0xC79 | `jp_table_0xC00` (`jpba`) |
| 0xC7A–0xCC7 | `minute_tick` (label_312..323) |
| 0xCC8–0xCE4 | `dec12_sat`, `dec8_sat`, `inc_nz_sat4`, `inc12` |
| 0xCE5–0xCF4 | `add_heart` |
| 0xCF5–0xCFC | `init_result_tiles` |
| 0xCFD–0xCFF | padding nop7 |
| 0xD00–0xD7C | table D |
| 0xD7D | `jp_table_0xD00` (`jpba`) |
| 0xD7E–0xD93 | menu handler `label_336` |
| 0xD94–0xD9B | pattern block (label_338) |
| 0xD9C–0xDC5 | evolution rules (label_339..345) |
| 0xDC6–0xDD1 | `load_evolution_rules` (label_346 + 7 `jp`) |
| 0xDD2–0xDFF | `post_evolution_setup` (label_347) |
