# Status (2026-10-12)

## Done
- **Complete rewrite of the game in C** (`src/`), without emulating the CPU: boot, clock setting,
  egg/hatching, pet screen with animation and overlays, the 7 icons (food, light, game, medicine,
  toilet, status, discipline), events (sleep, calls, poop, sickness), evolution, death/tombstone,
  reset that keeps the clock, factory LCD test, sound.
- **Playable**: `./tama-nativo` (SDL2; ← ↓ → or A S D; F = 10x; saves on exit to
  `~/.local/share/tamagotchi-p1/save.bin`; `-n` starts from scratch). While the app is closed,
  time stands still.
- **Validation against the emulator** (`tools/validate.sh`): game state (mapped RAM) is identical
  in every script except the cases below; sound identical; frames identical one by one for
  boot/clock, factory test and tombstone→reset; elsewhere, the same screens/animations with a
  pace ~1% off.
- **Builds** (GitHub Actions): Windows (.exe), Android (APK), WebAssembly. Released as `v0.1.0`
  (pre-release). None of them has been tested on a real Windows machine, a real device or a
  browser yet.

## Known differences (all of *timing*, not of logic)
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
