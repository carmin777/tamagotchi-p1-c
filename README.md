# Tamagotchi P1 — a native C rewrite

*(Versão em português: [README.pt-BR.md](README.pt-BR.md))*

A **readable** rewrite of the original Tamagotchi (Bandai, 1996, "P1") game logic in C, running
natively on SDL2 — **without emulating the CPU**. Every function carries the ROM address it came
from in a comment; every field of the game state carries its original RAM address. Fidelity is
measured against the [TamaLIB](https://github.com/jcrona/tamalib) emulator: the same button
scripts must produce the same game state, the same sound and the same screens.

*Requires your own ROM dump; none is included.*

## The ROM is not included

Sprites, animations, sounds and tables are read at run time from the P1 ROM, which belongs to
Bandai and is **not in this repository**. You need your own dump (`tama.b`, 12288 bytes,
md5 `3fce172403c27274e59723bbb9f6d8e9`) in `rom/tama.b`.

## Build and play

```sh
make -C src            # needs SDL2 (SDL2_image is optional, for the icons)
./tama-nativo          # or: src/tama -r rom/tama.b
```

- **Windows:** download the zip from Releases, open the `.exe` and drag `tama.b` onto the window
  (first run only; it is remembered afterwards). Not yet tested on a real Windows machine.
- **Android:** install the APK from Releases (allow "unknown sources"), open the app and pick
  `tama.b` in the file picker. Not yet tested on a real device.
- Keys: ← ↓ → (or A S D) are buttons A (select), B (confirm), C (cancel). F toggles 1x/10x speed.
- The game saves on exit to `~/.local/share/tamagotchi-p1/save.bin`
  (Windows: `%APPDATA%\tamagotchi-p1\save.bin`; `-n` starts a new game).
  While the program is closed, the pet's time stands still.
- The shell icons come from `tamatool/res/p1-icons.png` ([TamaTool](https://github.com/jcrona/tamatool),
  cloned next to this repo) or from `-i file.png`; without them, plain squares are drawn.

## How it was done

- `re/notes/` — reverse engineering of the whole ROM, by address range (routines, RAM,
  pseudo-C), starting from [agg23's partial disassembly](https://github.com/agg23/tamagotchi-disassembled).
  Known mistakes in those notes are listed in `STATUS.md`.
- `harness/` — TamaLIB without a window, with exact virtual time, button scripts, frame/RAM/sound
  logs and a routine profiler (`-P`).
- `src/` — the rewrite. The code is sequential like the original; the two hardware interrupts
  become C functions. The pacing of the P1's screens comes from the slowness of its own CPU, so
  each routine declares its CPU cost (measured on the emulator).
- `tools/validate.sh` — runs every `scripts/t_*.txt` on both sides and compares state
  (`cmpram.py`), sound (`cmpsnd.py`) and frames (`cmpframes.py`).

## Fidelity

The game state matches the emulator in every script, except where the outcome depends on exact
timing; boot, clock, factory test and tombstone match frame for frame. The animation pace
differs by ~1–2%, which can flip the phase of 2-frame animations, change the draws in the
guessing game and change how buttons pressed less than 1 s apart are read. Details in
`STATUS.md`.

## License

GPL-2.0-or-later (the harness includes TamaLIB, which is GPL). Tamagotchi is a Bandai trademark;
this project is not affiliated with Bandai and does not include the ROM (the notes under `re/`
quote some table values as documentation).
