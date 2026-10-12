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

## A reescrita (src/) — como funciona e como validar
- `make -C src` → `src/tama-headless -s script.txt -o out/x` (mesmo formato de script do harness;
  grava `frames.txt` e, com `dump`, `ram.txt`). `save`/`load` NÃO funcionam na reescrita: scripts
  de teste começam do boot (ver `scripts/t_hatch.txt`: boot → 08:00 → ovo → choca em ~310 s).
- Estilo: código sequencial/bloqueante como o original; cada função tem o endereço da ROM em
  comentário; estado em `struct game g` (tama.h) com o endereço de RAM ao lado de cada campo.
  Dados (sprites, scripts, sons, tabelas) lidos da ROM com `rom_table`/`rom_byte`.
- **Tempo**: `cost_ms(x)` = x ms de CPU *pura* (as ISRs roubam tempo à parte). Meça cada rotina
  no original com `harness/trace -W -s script -o out/p -P 0xADDR` → use o campo `puro=`.
  Distribua o custo na ordem em que o original lê/escreve estado (uma ISR pode cair no meio).
- **Validação** (oráculo = emulador):
  1. `harness/trace -C -W -s S -o out/ref` e `src/tama-headless -s S -o out/nat`
  2. `tools/cmpframes.py --min 0 out/ref/frames.txt out/nat/frames.txt` (estrito, frame a frame)
     ou `--tol 15` (tolerante: execuções longas, o ritmo das animações deriva ~1-2%).
  3. Com `dump` no script: `tools/cmpram.py out/ref/ram.txt out/nat/ram.txt` (estado do jogo).
     Campos novos em `struct game` → acrescentar em `dump_ram()` (front_headless.c) e em NAMES (cmpram.py).
  4. `tools/frames.py arq T0 T1 [--full]` lista/mostra frames.
- Corridas: input exatamente na virada do segundo pode dar resultado diferente; nos scripts,
  evite apertar botões perto de x,0 s do relógio do jogo.
- O ritmo do laço do pet deriva ~1% do original. Duas pressões que caem na mesma volta do laço
  (0,35–0,47 s) viram uma só leitura; por isso, nos scripts de teste, espace as pressões ≥ 1 s
  (`press X` + `wait 0.6`), senão o resultado depende da fase do laço e não da lógica.
- `tools/validate.sh [padrão]` roda todos os scripts nos dois lados e resume ram/som/frames.
  `0x05A rng` pode diferir em 1 num dump que caia exatamente num tick (artefato do dump).
