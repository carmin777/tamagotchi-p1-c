# Tamagotchi P1 — native C rewrite (option B)

Goal: rewrite the Tamagotchi P1 (Bandai, 1996) logic in readable C, with names and
structures, running natively (SDL2), **without emulating the CPU**. Fidelity is measured against the
TamaLIB emulator: the same input script must produce the same sequence of LCD frames.

## Layout
- `rom/tama.b` — P1 ROM (md5 3fce172403c27274e59723bbb9f6d8e9). Do not version.
- `tamagotchi-disassembled/tama.asm` — agg23's partial disassembly (partly commented). Do not version.
  `Useful Addresses.md` inside has what he already found out (graphics 0x1000–0x17FF, buzzer, 0x07D).
- `tamatool/` — reference emulator (jcrona), with GUI. `~/Projects/tamagotchi/tama` opens it.
- `harness/` — `trace`: headless emulator, virtual time, script-driven input. See the header of `harness/trace.c`.
  `make -C harness`; `harness/trace -s script.txt -o out/x` writes frames.txt, writes.tsv, cov.bin, ram.txt.
- `tools/asm.py 0xLO 0xHI` or `tools/asm.py <label>` — prints a stretch of tama.asm.
- `re/` — reverse-engineering knowledge (source of truth for the rewrite):
  - `re/ram-map.md` — RAM map (address → meaning, evidence).
  - `re/notes/<range>.md` — notes per code range (routines, pseudo-C).
  - `re/flows.md` — menu/button flows and game rules.
- `scripts/` — reusable input scripts (they are also the regression tests).
- `src/` — the rewrite.

## Hardware (E0C6S46 / P1)
- 4-bit CPU, 12-bit words; ROM 6144 words. 0x000–0xFFF code (with many `retd` tables),
  0x1000–0x17FF graphics (`lbpx`) + jump tables.
- RAM 0x000–0x27F (nibbles). Display 0xE00–0xE4F / 0xE80–0xECF. I/O 0xF00–0xF7F.
- Exact semantics of each instruction: `tamatool/src/lib/cpu.c` (`ops[]` table). I/O registers: `cpu.h`.
- LCD 32x16 + 8 icons (food, light, game, medicine, toilet, status, discipline, attention).
- Buttons: L (A, select), M (B, confirm), R (C, cancel).

## RE conventions
- Every claim in `re/` comes with evidence: a code address, or script + harness output.
  Mark hypotheses with `(?)`.
- Names: descriptive snake_case (`hunger_hearts`, `care_mistakes`), keeping the address next to them.
- Each agent writes only to the file(s) assigned to it.

## The rewrite (src/) — how it works and how to validate it
- `make -C src` → `src/tama-headless -s script.txt -o out/x` (same script format as the harness;
  writes `frames.txt` and, with `dump`, `ram.txt`). `save`/`load` do NOT work in the rewrite: test
  scripts start from boot (see `scripts/t_hatch.txt`: boot → 08:00 → egg → hatches in ~310 s).
- Style: sequential/blocking code like the original; each function has the ROM address in a
  comment; state in `struct game g` (tama.h) with the RAM address next to each field.
  Data (sprites, scripts, sounds, tables) is read from the ROM with `rom_table`/`rom_byte`.
- **Timing**: `cost_ms(x)` = x ms of *pure* CPU (ISRs steal time separately). Measure each routine
  on the original with `harness/trace -W -s script -o out/p -P 0xADDR` → use the `pure=` field.
  Spread the cost in the order in which the original reads/writes state (an ISR may land in the middle).
- **Validation** (oracle = emulator):
  1. `harness/trace -C -W -s S -o out/ref` and `src/tama-headless -s S -o out/nat`
  2. `tools/cmpframes.py --min 0 out/ref/frames.txt out/nat/frames.txt` (strict, frame by frame)
     or `--tol 15` (tolerant: long runs, the animation pace drifts ~1-2%).
  3. With `dump` in the script: `tools/cmpram.py out/ref/ram.txt out/nat/ram.txt` (game state).
     New fields in `struct game` → add them to `dump_ram()` (front_headless.c) and to NAMES (cmpram.py).
  4. `tools/frames.py file T0 T1 [--full]` lists/shows frames.
- Races: input exactly at the turn of the second can give a different result; in scripts,
  avoid pressing buttons near x.0 s of the game clock.
- The pet loop's pace drifts ~1% from the original. Two presses that land in the same loop
  iteration (0.35–0.47 s) become a single read; so, in test scripts, space presses ≥ 1 s apart
  (`press X` + `wait 0.6`), otherwise the result depends on the loop phase and not on the logic.
- `tools/validate.sh [pattern]` runs all scripts on both sides and summarizes ram/sound/frames.
  `0x05A rng` may differ by 1 in a dump that lands exactly on a tick (dump artifact).
