# Tamagotchi P1 — reescrita nativa em C

Uma reescrita **legível** da lógica do Tamagotchi original (Bandai, 1996, "P1") em C, rodando
nativo com SDL2 — **sem emular o processador**. Cada função traz no comentário o endereço da ROM
de onde veio; cada campo do estado, o endereço de RAM original. A fidelidade é medida contra o
emulador [TamaLIB](https://github.com/jcrona/tamalib): os mesmos roteiros de botões têm que
produzir o mesmo estado de jogo, o mesmo som e as mesmas telas.

*A readable, native C rewrite of the original Tamagotchi P1 logic (no CPU emulation),
validated against the TamaLIB emulator. Requires your own ROM dump; none is included.*

## A ROM não está incluída

Sprites, animações, sons e tabelas são lidos em tempo de execução da ROM do P1, que é da Bandai
e **não está neste repositório**. Você precisa do seu próprio dump (`tama.b`, 12288 bytes,
md5 `3fce172403c27274e59723bbb9f6d8e9`) em `rom/tama.b`.

## Compilar e jogar

```sh
make -C src            # precisa de SDL2 (SDL2_image é opcional, para os ícones)
./tama-nativo          # ou: src/tama -r rom/tama.b
```

- ← ↓ → (ou A S D): botões A (seleciona), B (confirma), C (cancela). F alterna 1x/10x.
- O jogo salva ao fechar em `~/.local/share/tamagotchi-p1/save.bin` (`-n` começa do zero).
  Com o programa fechado o tempo do pet para.
- Os ícones da carcaça vêm de `tamatool/res/p1-icons.png` ([TamaTool](https://github.com/jcrona/tamatool),
  clone ao lado) ou de `-i arquivo.png`; sem eles, aparecem quadrados.

## Como foi feito

- `re/notes/` — engenharia reversa da ROM inteira, por faixa de endereço (rotinas, RAM,
  pseudo-C), a partir da [desmontagem parcial do agg23](https://github.com/agg23/tamagotchi-disassembled).
  Erros conhecidos dessas notas estão em `STATUS.md`.
- `harness/` — o TamaLIB sem janela, com tempo virtual exato, roteiro de botões, log de
  frames/RAM/som e um profiler de rotinas (`-P`).
- `src/` — a reescrita. O código é sequencial como o original; as duas interrupções do
  hardware viram funções C. O ritmo das telas do P1 vem da lentidão do próprio CPU, então cada
  rotina declara seu custo de CPU (medido no emulador).
- `tools/validate.sh` — roda todos os `scripts/t_*.txt` nos dois lados e compara estado
  (`cmpram.py`), som (`cmpsnd.py`) e frames (`cmpframes.py`).

## Fidelidade

O estado do jogo bate com o emulador em todos os roteiros, exceto onde o resultado depende do
tempo exato; boot, relógio, teste de fábrica e lápide batem quadro a quadro. O ritmo das
animações difere ~1–2%, o que pode inverter a fase de animações de 2 quadros, mudar os sorteios do
jogo de adivinhar e a leitura de botões apertados a menos de 1 s um do outro. Detalhes em
`STATUS.md`.

## Licença

GPL-2.0-or-later (o harness inclui o TamaLIB, GPL). Tamagotchi é marca da Bandai; este projeto
não é afiliado à Bandai e não inclui a ROM (as notas de `re/` citam alguns valores de tabelas, como documentação).
