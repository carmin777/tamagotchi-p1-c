# Status (2026-10-11)

## Feito
- Harness `trace` + tools (asm.py, cmpframes.py). Commitado.
- RE estático das 8 faixas 0x000–0xFFF em re/notes/ (pseudo-C, RAM, tabelas de evolução, check_events, sprites, som).
- Explorador dinâmico foi INTERROMPIDO (limite de uso): o que ele deixou está em re/ e scripts/ (pode estar incompleto).

## Errata / decisões
- Botões em RAM: bit0=R, bit1=M, bit2=L (hw.c: K00=R, K01=M, K02=L). As notas 0x000 e 0x800 dizem L=bit0: ERRADO.
- 0x5D = espécie (0 ovo, 1 Babytchi, ...), 0x50 = estágio de evolução.
- 0x4A bit3: notas divergem (dormindo vs morto); 0x200/0xF00 indicam DORMINDO — confirmar no trace.
- Tick do prog timer medido: 27,247 ms. Clock: 1 Hz.

## Próximo
1. Consolidar re/ram-map.md a partir das notas (+ terminar fluxos/scripts dinâmicos).
2. Arquitetura da reescrita: código sequencial como o original; `yield()` avança 1 prog tick
   e chama on_prog_tick()/on_clock_tick() (ISRs em C). Estado em struct com nomes.
   Dados volumosos (sprites, som, scripts de animação, blocos por espécie) extraídos da ROM no build;
   regras (evolução) escritas à mão em C.
3. Front-ends: SDL2 e headless (mesmo formato de script do harness) → validar com tools/cmpframes.py.
