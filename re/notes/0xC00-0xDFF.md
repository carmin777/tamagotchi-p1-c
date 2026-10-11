# ROM 0xC00-0xDFF (palavras de 12 bits; endereços = índice de palavra, como no tama.asm)

Resumo: a faixa tem (1) a rotina do tick de 1 minuto (`label_312`), (2) helpers de contadores,
(3) duas tabelas de sprites do bicho (C = metade superior, D = metade inferior), (4) as regras
de evolução, (5) o fim da rotina de pós-evolução/eclosão (`label_347`, continua em 0xE00 -> `label_214` no 0x638).
Convenção: `M[xp:addr]`; `0x2xx` = RAM página 2 (xp=2). Nibbles de 4 bits. "BCD/decimal" nunca é usado aqui
(todas as rotinas começam com D=0: `rst f,0xB`).

Nota sobre `retd`: `retd l` faz `M(X)=l&0xF; M(X+1)=l>>4; X+=2` e retorna. `lbpx mx,l` faz o mesmo sem retornar.
Por isso "tabelas" aqui são sequências de `retd` (1 byte por entrada, acessadas por `call` + `jpba`), e
os trechos `lbpx...retd` são **blocos de 8 bytes/nibbles escritos na RAM em X** (disassembler os mostra como código).
`jpba` = `PC = (NPP<<8) | B<<4 | A` (NPP vem do último `pset`). Por isso `pset C; call jp_table_0xC00` com B:A=índice
executa o `retd` da posição `0xC00 + índice`.

---

## 1. `minute_tick` (label_312) — 0xC7A–0xCC7 (+ cai em 0xCC8–0xCD1)
Chamada: `clock_timer_int` (0x148, `pset 0xC; call label_312`), uma vez por minuto (quando segundos 0x10/0x11 estouram 60 e
minutos 0x12/0x13 incrementam; só roda se 0x03C != 0, i.e. relógio acertado). Entra com xp=0, D=0. Retorna com xp possivelmente 2
(o chamador reajusta xp).

Faz a contabilidade de "tempo de jogo" por minuto: contagem regressiva do ovo, e (se acordado) todos os timers do bicho.

RAM:
- `M[7C]`: estado do ovo/jogo. 0 = nada a fazer neste tick; 1..E = contagem regressiva em minutos (valor inicial 5 em reset, 0x1215;
  é o tempo de eclosão: 5 minutos); F = jogo ativo (bicho nascido; `label_391` em 0xFEC seta F ao eclodir). Confirmado no trace: 5->0 em 5 min, depois F.
- `M[4A]` bit3 = dormindo (set F em `label_102` 0x30C, zerado em 0xF20 ao acordar).
- `M[5D]` = espécie/estágio (ver §6); `==1` = Babytchi.
- `M[48]` bit3 = (?) "cocô presente" (setado F em 0xF8A quando `M[20D..20F]==0`, ver abaixo).
- `M[40]`,`M[41]` = corações de fome/felicidade em 1/4 de coração (0..F, 4 = 1 coração). Decaem 4 em 0xF99/0xFAE quando os timers 0x200/0x202 zeram.
- Timers (xp=2): `0x200:0x201` (8 bits, countdown fome; reload=3 em 0xF95), `0x202:0x203` (8 bits, countdown felicidade; reload=4 em 0xFAA),
  `0x206:0x207` (8 bits, 3º countdown, reload em 0x337/0x33C), `0x208` e `0x209` (nibbles, contam p/ cima até F só se !=0; 0xFC9 zera 0x208 ao chegar a F e dispara evento),
  `0x20A:0x20B:0x20C` (12 bits, "minutos com um coração vazio"), `0x20D:0x20E:0x20F` (12 bits, countdown do cocô (?) — decrementa;
  se `M[48]` bit3 setado, incrementa), `0x210:0x211:0x212` (12 bits, tempo restante do estágio; decrementa; inicial copiado de 0x240.. em `label_347`),
  `0x215` (nibble, Babytchi dormindo; init 5), `0x216:0x217` (8 bits, Babytchi acordado; init 0x28), `0x204:0x205` (8 bits, minutos dormidos (?); `label_102` inicia em 0x00 se baby, senão 0x3D).

Pseudo-C:
```c
void minute_tick(void) {           // D=0
    if (M[0x7C] == 0) return;
    if (M[0x7C] != 0xF) { M[0x7C]--; return; }        // contagem do ovo
    if (M[0x04A] & 8) {                                // dormindo
        if (M[0x05D] == 1) { if (M[0x215]) M[0x215]--; }          // baby: 0x215 = max(0x215-1,0)
        else { sat_inc8(0x204); }                                 // adulto: 0x204:5 += 1, trava em 0xFF
        return;
    }
    if (M[0x05D] == 1) dec8_sat(0x216);                // baby acordado (dec 8 bits, trava em 0)
    dec8_sat(0x200); dec8_sat(0x202);
    inc_nz_sat4(0x208); inc_nz_sat4(0x209);            // +1 se !=0, trava em F
    dec8_sat(0x206);
    if (M[0x40] == 0 || M[0x41] == 0) inc12(0x20A);    // algum coração vazio
    else { M[0x20A] = M[0x20B] = M[0x20C] = 0; }
    if (!(M[0x048] & 8)) dec12_sat(0x20D); else inc12(0x20D);
    dec12_sat(0x210);
}
```
(No ramo "dormindo/!baby" o código faz `M204++; M205 += carry; se overflow M204=M205=0xF`.)
Evidência: cov.bin/trace (decrementos a cada minuto, 0x7C 5->0, 0x216 0x28->0, 0x20D vs M48).

## 2. Helpers de contador (todos esperam xp correto; usam X como ponteiro, nibble baixo primeiro)
| endereço | label | nome proposto | efeito |
|---|---|---|---|
| 0xCC7–0xCD1 | label_323/label_324 | `dec12_sat` (323 = com X=0x210) | decrementa 12 bits em M[X..X+2], satura em 0 (usa `adc xl,0xE` para voltar X e `lbpx mx,0; ld mx,0`). Retorna C=1 se não saturou. |
| 0xCD2–0xCD8 | label_326 | `dec8_sat` | idem para 8 bits M[X],M[X+1] (`adc xl,0xF`). |
| 0xCD9–0xCDE | label_328 | `inc_nz_sat4` | `if (M[X] != 0 && M[X] != 0xF) M[X]++;` |
| 0xCDF–0xCE4 | label_330 | `inc12` | M[X..X+2] += 1 (12 bits, sem saturar), retorna (C da última soma). |
Chamadores: label_326 só em minute_tick (X=0,2,6 com xp=2); label_324 também em 0x743 (alimentar com lanche: `dec12_sat(0x20D)`, i.e. comer adianta o cocô (?)); label_330 só em minute_tick (X=0x20A e 0x20D).

## 3. `add_heart` (label_331) — 0xCE5–0xCF4
Entrada: X = 0x40 (fome) ou 0x41 (felicidade), xp=0. `M[X] += 4; if (carry) M[X]=0xF;` (+1 coração, satura). Depois, se `M[40]!=0 && M[41]!=0`
zera `M[0x208]` (xp=2) e restaura xp=0.
Chamadores: 0x6B2 (fim do minigame: `M[83]>=3` => +felicidade em 0x41), 0x734 (refeição: X=0x40, só se `M[40]!=F`), 0x73B (lanche: X=0x41).
```c
void add_heart(int X) { M[X] += 4; if (M[X] > 15) M[X] = 15;
    if (M[0x40] && M[0x41]) M[0x208] = 0; }
```

## 4. `init_result_tiles` (label_334) — 0xCF5–0xCFC
Escreve 16 nibbles em M[0x90..0x9F] (chamado com X=0x90, xp=0, de 0x69D): pares (a,b) = índices de tile 8x8 para `fetch_and_misc_render`
(blocos 0x16xx/0x17xx, rotina 0x792 `label_233` desenha 8 tiles de M90..9F): `18 FF 1D FF 00 0F 00 FF`
-> [ícone bichinho 0x16C0][branco][coração vazio 0x16E8][branco][dígito 0x1600+8d][VS 0x1678][dígito][branco].
O chamador (0x6A0) então faz `M[98]=M[83]` (acertos) e `M[9C]=5-M[83]` (erros): tela de resultado do minigame "X VS Y" (?).
0xCFD–0xCFF: `nop7` (padding).

## 5. Tabelas de sprites do bicho: C (0xC00–0xC78) e D (0xD00–0xD7C); despachos 0xC79 e 0xD7D
- `jp_table_0xC00` (0xC79) e `jp_table_0xD00` (0xD7D) são só `jpba`. Uso: `pset C|D; call jp_table_0x?00` com B:A = índice -> grava
  em `M[X]=lo nibble, M[X+1]=hi nibble` do byte da tabela.
- Chamador único: `label_288` (0x928, metade superior, tabela C) e `label_289` (0x939, metade inferior, tabela D); ambos terminam em
  `label_290/label_291` (0x949–0x97E, fora desta faixa). Protocolo:
```c
// entrada: A=frame (-> M0), B=flag espelho (bit3 -> m3), M[5D]=espécie
uint8_t base  = tblX[M[0x5D]];                 // X=1: M1=lo, M2=hi        (primeiras 12 entradas = ponteiros)
uint8_t id    = tblX[base + frame];            // X=0x24: M24=lo, M25=hi
id ^= (m3 & 8) << 4;                           // bit7 = espelhar horizontalmente; B&8 inverte
uint8_t spr   = (id & 0x7F) + 3;               // (M25&7):(M24)+3
if (spr >= 0x80) return;                       // id 0x7F => não desenha (Babytchi superior)
asset_addr = 0x1000 + 0x10*spr;                // render_asset: m2=spr>>4 (página), m1=spr&F, m0=0
render_asset -> buffer de vídeo (xp=1) em X=(M3A,M3B)   // 32 nibbles = 16 colunas x 8 linhas
if (id & 0x80) mirror16columns();              // troca pares de nibbles (X+2k) <-> (X+30-2k), 8 iterações (0x973-0x97D)
```
- As duas tabelas têm a mesma estrutura: 12 ponteiros (bytes em 0x00–0x0B) para o início da lista de cada M5D (0..11), depois listas de ids.
  Número de frames por espécie (igual em C e D): `[5,7,12,13,9,9,9,10,10,12,7,6]`. O índice de frame é o mesmo para as duas metades.
  Tabela C: 12 ponteiros em 0x00–0x0B, listas em 0x0C–0x78 (0x79 é o `jpba`). Tabela D: ponteiros em 0x00–0x0B, bytes 0x0C–0x0F = `7D` (preenchimento, não usados),
  listas em 0x10–0x7C (0x7D é o `jpba`). A lista 1 de C (Babytchi) é só `7F` (sem metade superior; o Babytchi é desenhado inteiro pela tabela D).
- Ponteiros C: `0C 11 18 24 31 3A 43 4C 56 60 6C 73`; D: `10 15 1C 28 35 3E 47 50 5A 64 70 77`.
- Sprites = dados gráficos 0x1030–0x14F0 (ver "Useful Addresses.md"); `m` = flag de espelhamento (bit7 do id).
  Frame -> pose não está resolvido (depende de quem chama; provavelmente 0=parado, depois comer/feliz/dormir...). Mapa espécie M5D (?): 0 ovo, 1 Babytchi, 2 Marutchi, 3 Tamatchi, 4 Kuchitamatchi, 5 Mametchi, 6 Ginjirotchi, 7 Maskutchi, 8 (?)Tarakotchi, 9 Nyorotchi, 10 (?)Kuchipatchi, 11 Bill.
  Observado: M5D=0 ovo, 1 após eclodir, 2 após evoluir do baby (trace).

#### Tabela C (metade superior): lista por espécie

| M5D | espécie (?) | início idx | frames: idx -> id(hex) -> asset |
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

#### Tabela D (metade inferior): lista por espécie

| M5D | espécie (?) | início idx | frames: idx -> id(hex) -> asset |
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

Legenda: `idx:id=asset`; `id` com sufixo `m` = bit7 (espelhado); `asset = 0x1000+0x10*((id&0x7F)+3)`; `1820`/`7F` = nada desenhado.
Frames de ovo: 0=ovo posição 1, 1=ovo posição 2, 2=rachando, 3=brilho (sparkle), 4=anjo (morto) (?).

## 6. Evolução

### 6.1 `label_346` — 0xDC6–0xDD1 `load_evolution_rules`
Entrada: A = `M[50]` (ramo/estágio atual, 0..6), chamada de 0x357 (`label_112`, evento M5C=5 "evoluir"), `pset 0xD`.
`X=0x90; B=0; A+=0xB; B=0xC+carry; jpba` -> PC=0xDCB+A: tabela de 7 `jp` (0xDCB–0xDD1) para `label_339..345`.
Cada alvo escreve em M[0x90..] uma lista de regras de 4 nibbles `(min_A, min_B, novoM50, novoM5D)` e retorna (`retd` no fim).
Pode passar de 0x9F (a regra para M50=2 tem 6 entradas = 24 nibbles; vai até 0xA7).

| M50 atual | label (end.) | regras (minA, minB -> M50', M5D') |
|---|---|---|
| 0 (baby) | 339 (0xD9C) | (0,0 -> 1, 2) |
| 1 (Marutchi) | 340 (0xD9E) | (3,3 -> 5,4) (3,0 -> 4,4) (0,3 -> 3,3) (0,0 -> 2,3) |
| 2 (Tamatchi) | 341 (0xDA6) | (3,4 -> F,A) (3,2 -> F,9) (3,0 -> F,8) (0,2 -> F,7) (0,1 -> F,6) (0,0 -> F,5) |
| 3 | 342 (0xDB2) | (4,8 -> F,A) (4,0 -> F,9) (0,2 -> 6,7) (0,0 -> F,6) |
| 4 (Kuchitamatchi) | 343 (0xDBA) | (0,3 -> F,A) (0,2 -> F,9) (0,0 -> F,8) |
| 5 | 344 (0xDC0) | (0,6 -> F,A) (0,0 -> F,9) |
| 6 | 345 (0xDC4) | (0,0 -> F,B) |
Bytes (nibble baixo primeiro): 339: `00 21`; 340: `33 45 03 44 30 33 00 32`; 341: `43 AF 23 9F 03 8F 20 7F 10 6F 00 5F`;
342: `84 AF 04 9F 20 76 00 6F`; 343: `30 AF 20 9F 00 8F`; 344: `60 AF 00 9F`; 345: `00 BF`.
Seleção (em `label_112`, 0x35C–0x36C):
```c
a = M[0x42]; b = M[0x51]; load_evolution_rules(M[0x50]);   // regras em M[0x90..]
for (r = 0x90; ; r += 4)                                    // primeira regra que casa (as mais exigentes vêm antes)
    if (a >= M[r] && b >= M[r+1]) { M[0x50] = M[r+2]; M[0x5D] = M[r+3]; break; }
```
Significados (?): `M[50]` = ramo de evolução (F = final/adulto), `M[42]`/`M[51]` = pontuações de cuidado (0 sem interação: caiu em (0,0) no trace).
M5D=2 e M50=1 após o baby (confirmado).

### 6.2 `label_347` — 0xDD2–0xDFF `post_evolution_setup` (continua: `pset 6; jp label_214` em 0xDFE/DFF)
Entradas: `jp` de 0x373 (fim de `label_112`, evolução) e de 0xFFE (eclosão, `jp label_347`). Efeitos:
```c
M[0x49] = 0;                                   // (?) contagem associada ao cocô/limpeza
// renderiza o ícone da espécie: m0 = (M5D+8)&0xF, m1 = 5 + carry, m2 = 5 -> gfx 0x1500 + (m1:m0)
//   = jump table em 0x1558+M5D (jp label_397..406) para o retrato da espécie, desenhado em M[0x230..0x23F]
render_asset(dest=0x230);
a = M[0x048];                                   // ANTES de tocar nos timers
M[0x210..0x212] = M[0x240..0x242];              // tempo do estágio (copy_3_mx_my_ret; 0x07D=0 durante a cópia)
if (!(a & 8)) M[0x20D..0x20F] = M[0x238..0x23A];// timer de cocô só é recarregado se não há cocô
M[0x213] = M[0x243];  M[0x043] = M[0x244];      // (?) parâmetros do estágio (tabela de espécie carregada antes por label_142/271 em RAM 0x240..)
if (M[0x50] == 2 || M[0x50] == 4) M[0x043] = 8;
goto label_214;                                  // 0x638 (volta ao laço principal)
```
Os dados 0x238..0x244 vêm de uma tabela por espécie (jp_table_0xE00 / `label_142`, fora desta faixa).

### 6.3 `label_336` — 0xD7E–0xD93 (alvo nº5 da jump table 0x700, `label_164` despacha por `pset 7; jpba` com B:A=2*índice)
Tela/handler de menu (provável ícone banheiro/limpeza (?)); se dormindo (`M[4A]` bit3) volta ao laço (`pset 5; jp label_160`).
Senão: xp=1, `label_338(0x140)` e `label_338(0x1C0)` escrevem no buffer de vídeo (página 0x100) um padrão fixo, chama `label_305` (pset A, A=4,B=4),
e se `M[4D]!=0`: `M[4D]=0` e desenha `A=7,B=5,Y=6` via `label_273` (pset 8). Cobertura obtida no trace só ao apertar L 8x + M (não consegui ver a tela).
`label_338` (0xD94–0xD9B) = 8 bytes `44 EE BB 55 AA 11 00 00` gravados em M[X..X+15].

## 7. Mapa de endereços da faixa
| faixa | conteúdo |
|---|---|
| 0xC00–0xC78 | tabela C (ponteiros + ids superiores) |
| 0xC79 | `jp_table_0xC00` (`jpba`) |
| 0xC7A–0xCC7 | `minute_tick` (label_312..323) |
| 0xCC8–0xCE4 | `dec12_sat`, `dec8_sat`, `inc_nz_sat4`, `inc12` |
| 0xCE5–0xCF4 | `add_heart` |
| 0xCF5–0xCFC | `init_result_tiles` |
| 0xCFD–0xCFF | padding nop7 |
| 0xD00–0xD7C | tabela D |
| 0xD7D | `jp_table_0xD00` (`jpba`) |
| 0xD7E–0xD93 | handler de menu `label_336` |
| 0xD94–0xD9B | bloco de padrão (label_338) |
| 0xD9C–0xDC5 | regras de evolução (label_339..345) |
| 0xDC6–0xDD1 | `load_evolution_rules` (label_346 + 7 `jp`) |
| 0xDD2–0xDFF | `post_evolution_setup` (label_347) |
