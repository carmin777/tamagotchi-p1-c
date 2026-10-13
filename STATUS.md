# Status (2026-10-12)

## Done
- **Complete rewrite of the game in C** (`src/`), without emulating the CPU: boot, clock setting,
  egg/hatching, pet screen with animation and overlays, the 7 icons (food, light, game, medicine,
  toilet, status, discipline), events (sleep, calls, poop, sickness), evolution, death/tombstone,
  reset that keeps the clock, factory LCD test, sound.
- **Playable**: `./tama-nativo` (SDL2; ← ↓ → or A S D; F = 10x; saves on exit to
  `~/.local/share/tamagotchi-p1/save.bin`; `-n` starts from scratch). While the app is closed,
  time stands still.
- **Validation against the emulator** (`tools/validate.sh`): see the table below for the exact
  results per script. Short version: boot/clock and the factory test match the emulator frame for
  frame; in the longer scripts the game state matches except for the timing-dependent cases listed
  below, but the frame sequences drift apart (animation phase) and the tolerant frame comparison
  fails in most of them.
- **Builds** (GitHub Actions): Windows (.exe), Android (APK), WebAssembly. Released as `v0.1.0`
  (pre-release). None of them has been tested on a real Windows machine, a real device or a
  browser yet.

## Validation results (run of 2026-10-12, `tools/validate.sh`)
`ram` compares the mapped RAM dump by dump, `sound` the buzzer events, `frames` the LCD frames.
"ram: only rng" means the only difference is `0x05A rng`, which is a dump artifact (a dump that
lands exactly on a prog timer tick).

| script | ram | sound | frames, strict | frames, tolerant (±15 s) |
|---|---|---|---|---|
| `t_boot` | equal | equal | equal (29 frames) | ok |
| `t_life_factory` | equal | equal | equal (15 frames) | ok |
| `t_care_food`, `t_care_medicine`, `t_care_toilet`, `t_info_status` | only rng | equal | diverges at hatching | fails (few frames) |
| `t_hatch_dump`, `t_info_game_sick`, `t_life_evolve`, `t_life_death` | only rng | equal (`t_life_death`: differs) | diverges at hatching | fails (animation phase after the first poop) |
| `t_care_discipline`, `t_info_game_sleep` | differs: `0x04A` bits 0-2 (phase of the sleep "Z") | equal | diverges at hatching | fails |
| `t_care_light` | differs: light state and saved cursor (button-press race) | differs | diverges at hatching | fails |
| `t_info_game` | differs: the guessing game's draws (`0x041`, `0x083`, `0x084`) | differs | diverges at hatching | fails |
| `t_hatch` | (no dump) | equal | diverges at hatching | fails |

Notes:
- "Diverges at hatching": the egg hatches ~1.8 s later in the rewrite (t = 310.7 s vs 308.9 s in the
  emulator, ~0.6%), so every strict comparison breaks there. Frames before that point match.
- In the long scripts the tolerant comparison fails from the first poop (t ≈ 1219 s) on: the phase
  of the 2-frame poop animation is inverted, so those frames never find a match on the other side.
  In the shorter ones it fails from t ≈ 325–356 s (a few frames); that cause was not investigated.
- The sound difference in `t_life_death` was not investigated.
- The earlier claim that the tombstone→reset sequence matches frame for frame was **not** reproduced
  in this run (the strict comparison breaks at hatching, long before it).

## Known differences (believed to be timing, not logic — but see the table)
- The pace of the screens is that of the original CPU; the rewrite uses per-routine measured
  costs and gets within ~1–2%. Consequences:
  - the phase of 2-frame animations (poop, sleep "Z" = 0x4A bits 0-2, counter 0x07E) may come
    out inverted after animations that end by timeout;
  - the **guessing game** uses counter 0x05A (+1 per tick); the outcome depends on the exact tick
    at which the button is read, so the draws differ from the emulator (the mechanism is the same);
  - button presses less than 1 s apart may fall into different iterations of the loop (e.g.
    `t_care_light`); the 0.2 s blink of the attention icon at the turn of the minute is a race.
- Not tested: death of old age (angel).

## Errata of the RE notes (re/notes) — already fixed in the notes on 2026-10-12
- Buttons in RAM: bit0=R, bit1=M, bit2=L (notes 0x000 and 0x800 said L=bit0).
- 0x5D = species, 0x50 = stage; 0x4A bit3 = sleeping (not "dead"); event 4 = poop.
- The prog timer runs free at 256/7 Hz (27.34 ms); the 1 Hz ISR does not restart its phase.
- The init's clearing loop (0x047) zeroes 16 nibbles per iteration: init clears 0x000–0x0DF,
  0x100–0x26F and the whole LCD (notes 0x200 §2.4 were wrong).
- Factory test, 1st step: XP is still 2, so the icons do not light up (a bug in the original).

## Cross-checks against Epson's documents
- I/O registers used by the ROM were checked against the *E0C6S46 Technical Hardware* manual
  (see "I/O registers used" in `re/notes/0x000-0x1FF.md`); one error found and fixed
  (F77 is the stopwatch, not the clock).
- TamaLIB's per-instruction cycle counts match the *E0C6200/6200A Core CPU Manual*.

## Ideas
- Exact per-instruction timing (for identical draws) would require counting cycles along the real path.
- A pet that keeps living while the app is closed; basis for a pet with biology/Needle/Nostr.
- Other ROMs of the same family (P2, Angel…): see the discussion about a compatibility
  (emulator) mode versus per-game rewrites. The ROMs are not publicly available.
