# Tamagotchi P1 — reescrita nativa em C (opção B)

Objetivo: reescrever a lógica do Tamagotchi P1 (Bandai, 1996) em C legível, com nomes e
estruturas, rodando nativo (SDL2), **sem emular o CPU**. Fidelidade é medida contra o
emulador TamaLIB: o mesmo script de input tem que produzir a mesma sequência de frames do LCD.

## Layout
- `rom/tama.b` — ROM P1 (md5 3fce172403c27274e59723bbb9f6d8e9). Não versionar.
- `tamagotchi-disassembled/tama.asm` — desmontagem parcial do agg23 (comentada em parte). Não versionar.
  `Useful Addresses.md` lá dentro tem o que ele já descobriu (gráficos 0x1000–0x17FF, buzzer, 0x07D).
- `tamatool/` — emulador de referência (jcrona), com GUI. `~/Projects/tamagotchi/tama` abre.
- `harness/` — `trace`: emulador headless, tempo virtual, input por script. Ver cabeçalho de `harness/trace.c`.
  `make -C harness`; `harness/trace -s script.txt -o out/x` grava frames.txt, writes.tsv, cov.bin, ram.txt.
- `tools/asm.py 0xLO 0xHI` ou `tools/asm.py <label>` — imprime trecho do tama.asm.
- `re/` — conhecimento de engenharia reversa (fonte da verdade para a reescrita):
  - `re/ram-map.md` — mapa da RAM (endereço → significado, evidência).
  - `re/notes/<faixa>.md` — notas por faixa de código (rotinas, pseudo-C).
  - `re/flows.md` — fluxos de menu/botões e regras de jogo.
- `scripts/` — scripts de input reutilizáveis (também são os testes de regressão).
- `src/` — a reescrita.

## Hardware (E0C6S46 / P1)
- CPU 4 bits, palavras de 12 bits; ROM 6144 palavras. 0x000–0xFFF código (com muitas tabelas
  `retd`), 0x1000–0x17FF gráficos (`lbpx`) + jump tables.
- RAM 0x000–0x27F (nibbles). Display 0xE00–0xE4F / 0xE80–0xECF. I/O 0xF00–0xF7F.
- Semântica exata de cada instrução: `tamatool/src/lib/cpu.c` (tabela `ops[]`). Registradores I/O: `cpu.h`.
- LCD 32x16 + 8 ícones (comida, luz, jogo, remédio, banheiro, status, disciplina, atenção).
- Botões: L (A, seleciona), M (B, confirma), R (C, cancela).

## Convenções de RE
- Toda afirmação em `re/` vem com evidência: endereço de código, ou script + saída do harness.
  Marque `(?)` o que for hipótese.
- Nomes: snake_case descritivo (`hunger_hearts`, `care_mistakes`), mantendo o endereço ao lado.
- Cada agente escreve só no(s) arquivo(s) que lhe foram atribuídos.
