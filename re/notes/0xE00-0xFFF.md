# ROM 0xE00-0xFFF + banco 1 (0x1000-0x17FF, só código/jump tables/formato dos sprites)

Convenções (iguais às de 0xC00-0xDFF.md): `M[a]` = RAM nibble; `Q[a]` = `M[0x200+a]` (página 2, YP=2);
`P[a]` = `M[a]` página 0. Endereços de ROM = índice de palavra (como no tama.asm). Palavras de 12 bits.
`retd l` / `lbpx mx,l`: `M(X)=l&F; M(X+1)=l>>4; X+=2` (retd também retorna). `jpba`: `PC=(np<<8)|(B<<4)|A`, `np` = última
`pset` (ou a própria página, após call/jp). Logo, em todo este trecho **"tabelas" são sequências de `lbpx...retd`
(o desmontador as mostra como código) cujo "valor" é o conjunto de bytes escritos em RAM em X**; uma entrada termina no `retd`.
Evidência geral: tama.asm + `tamatool/src/lib/cpu.c` (op_lbpx_cb/op_retd_cb/op_jpba_cb); ROM lida direto de `rom/tama.b`
(palavra = `((b[2a]<<8)|b[2a+1]) & 0xFFF`).

Mapa da faixa:

| faixa | nome | tipo |
|---|---|---|
| 0xE00-0xE0C | `anim_index_table` | 13 `retd` (índice->offset) |
| 0xE0D | `jp_table_0xE00` | `jpba` (dispatcher das duas tabelas acima/abaixo) |
| 0xE0E-0xE59 | `anim_script_records` | dados (scripts de animação dos ícones laterais) |
| 0xE5A-0xE7B | `menu_choose_option` (label_352) | rotina |
| 0xE7C | `jp_table_0xE7D_buzzer` | `jpba` |
| 0xE7D-0xEFC | `sound_script` | dados (música/bipes) |
| 0xEFD-0xEFF | padding `nop7` | |
| 0xF00-0xFD3 | `check_events` (label_357) | rotina (núcleo das regras de cuidado) |
| 0xFD4-0xFD8 | `set_event` / `event_pending` (set_0x5C_to_a, label_385) | helpers |
| 0xF3D-0xF49 | `shrink_interval_25pct` (label_363) | helper |
| 0xFD9-0xFDE | `cmp8_hour` (label_386) | helper |
| 0xFDF-0xFEB | `add_care_mistake` (label_388) / `sat_inc4` (label_389) | helpers |
| 0xFEC-0xFFE | `hatch_egg` (label_391) | rotina |
| 0x1000-0x102F | `gfx_dispatch` | jump table de gráficos |
| 0x1030-0x11DF, 0x1230-0x14FF | sprites 16x8 | dados lbpx |
| 0x11E0-0x1221 | `set_init_mem_and_int` (+`_cont_set_mem`) | rotina (banco 1) |
| 0x1222-0x122E | `copy_6_mx_my` | rotina |
| 0x1500-0x1557 | glifos pequenos (dígitos 4x8 e 3x8, setas) | dados |
| 0x1558-0x1563 | `stage_param_jump_table` | 12 `jp` |
| 0x1564-0x15E5 | `stage_param_blocks` (10x13 bytes) | dados de jogo |
| 0x15E6-0x15FD | `hour24_to_12_table` | 24 `retd` |
| 0x1600-0x17FF | glifos 8x8 | dados lbpx |

---------------------------------------------------------------------------------------------------

## 1. Animações dos ícones laterais: `anim_index_table` 0xE00 + `anim_script_records` 0xE0E

Uso: `play_anim` (label_272/273, 0x8A8-0x8FF; fora da faixa, notas 0x400-0x5FF) com `Y = anim_id` (0..12), A=pose -> M[5E],
B=segundos -> M[57]. 0x8B1-0x8B3: `X=0xA6; call jp_table_0xE00` com B:A = anim_id grava em `M[A6..A7]` o offset (retd 1 byte) do
primeiro registro. A cada quadro (0x8C1-0x8D4) lê-se o registro em `0xE00+offset` com outro `call jp_table_0xE00` (X=0xA2)
e `M[A6..A7] += 2` (próximo registro consecutivo; um registro de 3 bytes sobrescreve A6/A7 com o `next`).

**Índice** (offset em 0xE00 -> registro inicial): `[0]=0x0E [1]=0x11 [2]=0x16 [3]=0x1F [4]=0x24 [5]=0x24 [6]=0x29 [7]=0x2E
[8]=0x31 [9]=0x3C [10]=0x45 [11]=0x4E [12]=0x55`.

**Registro** (escrito em X=0xA2): 2 bytes (`lbpx; retd`) = `[b0,b1]`, ou 3 bytes (`lbpx; lbpx; retd`) = `[b0,b1,next]`
(`next` = offset de registro, em 0xE00+, para onde o laço volta). Campos:
- `b0 & 0x3F` = índice de glifo 8x8 `g` (endereço ROM `0x1600 + 8*g`; ver §8). `g=0x3F` (b0=0xFF/0x7F) = "não desenhar"
  (0x8D5-0x8DA testa M[A8]==F && M[A9]==3). Cópias: `M[A8]=b0 low nibble`, `M[A9]=(b0>>4)&3`.
- `b0 >> 6` = contador de espera do quadro: `M[AA] = (b0>>6)&3` (0x8CD-0x8D4); em 0x8BF `M[AA]` é decrementado e,
  enquanto não estourar, o quadro é redesenhado sem avançar de registro (nº de quadros extras de permanência) (?).
- `b1` = posição de desenho no buffer de vídeo: `X = 0x100 + b1` (XL=b1 baixo, XH=b1 alto; `b1>=0x80` = metade inferior da tela;
  `b1=0x50` = fora da tela/nulo). Coluna = `(b1&0x7F)/2`, linhas 0-7 se `b1<0x80` senão 8-15. Para M[5D]==1 (baby) há ajuste de XH em
  0x8DF-0x8EF (`XH |= 8` se XH≠0, depois `X += 0xF8`...) (?).
Os glifos são desenhados por `fetch_and_misc_render` (§8) em (Y=0xA8).

Registros decodificados (offset: b0 b1 [next] -> glifo):
```
0E: FF 50 ->0E            anim 0 : vazio, repete
11: 60 30 ; 13: 61 30 ->11  anim 1 : Z grande (0x1700) / z pequeno (0x1708) em x=24,linhas 0-7  = bicho DORMINDO
16: FF 50 ; 18: FF 50 ; 1A: 5E A8 ; 1C: 5F A8 ->1A   anim 2 : 2 quadros vazios, depois fedor (0x16F0/0x16F8) em x=20,linhas 8-15 = COCÔ
1F: 65 30 ; 21: 64 30 ->1F   anim 3 : raivinhas (0x1728/0x1720) = CHAMADA/ATENÇÃO
24: 62 30 ; 26: 7F 50 ->24   anim 4 e 5 (mesmo registro) : caveira (0x1710) alternando com vazio = DOENTE
29: 7F 50 ; 2B: 63 30 ->29   anim 6 : vazio / brilho (0x1718) = FELIZ
2E: D7 B0 ->2E               anim 7 : glifo 0x16B8 ("FF") em x=24, linhas 8-15 (mantido, espera=3)
31: 10 00 ; 33: 10 80 ; 35: D1 80 ; 37: D2 80 ; 39: D3 80 ->39   anim 8  : pão (0x1680) + 3 mordidas (0x1688,0x1690,0x1698) = COMER REFEIÇÃO
3C: 10 00 ; 3E: 10 80 ; 40: 52 80 ; 42: 53 80 ->42                anim 9  : variante do pão (g=0x12,0x13)
45: 14 00 ; 47: 14 80 ; 49: D5 80 ; 4B: D6 80 ->0E                anim 10 : lanche (0x16A0) + mordidas (0x16A8,0x16B0), termina em anim 0
4E: 14 00 ; 50: 14 80 ; 52: 56 80 ->0E                            anim 11 : lanche, variante
55: 10 00 ; 57: D0 80 ->57                                        anim 12 : pão e mantém
```
Identificação dos glifos por `g`: 0x10 pão, 0x11/0x12/0x13 mordidas do pão (0x1688/0x1690/0x1698), 0x14 lanche, 0x15/0x16 mordidas do
lanche, 0x17 "FF", 0x1E/0x1F = fedor (0x16F0/0x16F8), 0x20/0x21 = Z grande/pequeno, 0x22 caveira, 0x23 brilho, 0x24/0x25 raivinhas
(nomes dos glifos de "Useful Addresses.md", conferidos pelos endereços 0x16F0-0x1728).
Confiança nos nomes semânticos das animações: média (deduzidos dos glifos; ids chamados em outras notas: 2=comida? ver 0x200-0x3FF).

Pseudo-C do registro:
```c
typedef struct { uint8_t glyph:6, hold:2; uint8_t pos; uint8_t next; bool has_next; } AnimRec;
// anim_ids[13] = {0x0E,0x11,0x16,0x1F,0x24,0x24,0x29,0x2E,0x31,0x3C,0x45,0x4E,0x55}
```
(os 13 registros são extraídos da ROM em build: ler palavras de 0xE0E; `lbpx`=0x9II, `retd`=0x1II; registro termina no retd).

---------------------------------------------------------------------------------------------------

## 2. `menu_choose_option` — 0xE5A-0xE7B (agg23 `label_352`)
Menu de 2 opções (luz sim/não; também usado em 0xAC8). Entradas: B = timeout em s (callers passam 0xA), M[78] = opção inicial
(0/1). Saída: **C=0,Z=1: confirmou (M); C=1,Z=1: cancelou (botão R); C=1,Z=0: timeout ou evento pendente**; A = cursor (M[75]).
Chamadores: 0x71C (luz), 0xACA (`label_297`). Chama: `label_66` (0x1EC: M[75]=0 cursor, M[76]=A=2 nº de itens, M[77]=B timeout,
M[57]=B), `label_30` (janela do buffer em 0x140), `label_94` (0x2C3, leitura de botões/cursor), `render` 0x792 `label_233`,
`copy_video_buf_to_vram`, `check_events`.
```c
int menu_choose_option(uint8_t timeout /*B*/) {
    M[75]=0; M[76]=2; M[77]=timeout; M[57]=timeout; copy_0x026_7_to_8_9();
    M[2A]=0; M[2B]=4; M[3A]=0; M[3B]=4;               // janela e cursor de desenho = 0x140
    saved = M[78]; M[75] = saved;                      // cursor inicial
    for (;;) {
        flags = menu_poll_input();                     // label_94: L(bit2 de M[29]) => M[75]=(M[75]+1)%M[76] + beep 0x94;
                                                       //           M => C=0,Z=1; R => C=1,Z=1; timeout (M[57]==0 && M[77]!=0) => C=1,Z=0
        if (C || Z) break;
        M[90..91]=0xFF; M[98..99]=0xFF;                // slots de glifos: FF = branco (g=0x3F)
        slot = (cursor==0) ? 0x90 : 0x98;              // linha 0 ou linha 1 da lista de 8 glifos
        M[slot]=7; M[slot+1]=2;                        // g=0x27 -> glifo 0x1738 = seta cheia "->"
        render_8_glyphs();                             // label_233 (Y=0x90..)
        copy_video_buf_to_vram();
        if (check_events() /*NZ*/) { A=0; C=1; /*Z=0*/ break; }
    }
    M[78] = A;                                         // cursor final
    return;
}
```
(Mapeamento dos botões em M[29]: bit0 R/C, bit1 M/B, bit2 L/A, conforme 0x2CE-0x2E4.)

---------------------------------------------------------------------------------------------------

## 3. `sound_script` — 0xE7C (`jpba`) / 0xE7D-0xEFC
Lido por `configure_buzzer` (0x1A0; roda no começo de cada interrupção do prog timer, ~256 Hz (?) segundo o init 0x11F4).
Estado: `M[32..33]` contador de duração (8 bits, decrementado a cada tick; ao estourar (0x00 -> 0xFF) carrega o próximo passo),
`M[34]` freq (bits 0-2 = `F74` índice de freq do buzzer; **bit3=1 => silêncio/pausa**), `M[35]` = envelope (`F75`, sempre 0 aqui),
`M[36..37]` ponteiro (offset em 0xE00+, 0x7D..0xFC) para o próximo passo, `M[58..59]` nº de repetições (`M[59]==F` = infinito).
Passo = bytes escritos em X=0x32 (e dispara `jpba` com B:A = `M[37]:M[36]` já somado de 2 palavras):
- `lbpx;retd` (2 palavras, 4 nibbles): `[dur, ctrl]` -> segue no passo seguinte (ponteiro += 2 palavras já feito).
- `lbpx;lbpx;retd` (3 palavras, 6 nibbles): `[dur, ctrl, next]` -> `M[36..37]=next` (salto/laço). Se `M[59]!=F`, o salto é feito
  `M[58]` vezes; esgotado (`M[58:59]` borrow) o ponteiro vai a 0x7D (silêncio).
Passos (offset: dur ctrl [next]; "dur" em ticks-1 → duração = dur+1 ticks; ctrl: bit3=pausa):
```
7D: dur=255 ctrl=8(pausa) ->7D                 (parado/idle; ponteiro padrão)
80: 48 ctrl3 ->7D                              tune 0x80: bipe longo (ligar/reset)
83..91: dur=0, ctrl 7,6,5,4,3,2,1,0 ->7D       sweep descendente (tune 0x83)
94: 1 c4 ->7D                                  bipe de botão (default, M[7B]!=0)
97: 1 c5 ; 99: 1 pausa ; 9B: 1 c3 ->7D          tune 0x97 (liga/desliga som)
9E: 5 c7 ; A0: 3 pausa ; A2: 11 c7 ->7D          tune 0x9E (erro/negativo)
A5: 1 c2 ; A7: 1 pausa ; A9: 7 c3 ; AB: 7 pausa ->A5     tune 0xA5 (bedtime/chamada, laço com repetições M[58])
AE: 2 c1 ; B0: 2 c4 ; B2: 2 c2 ; B4: 2 c5 ; B6: 2 c3 ; B8: 2 c6 ; BA: 2 c4 ; BC: 2 c7 ->AE       tune 0xAE (evolução, laço)
BF: 18 c3 ; C1: 25 pausa ->BF    C4: 24 c3 ; C6: 40 pausa ->C4    C9: 28 c3 ; CB: 68 pausa ->C9   tunes 0xBF/0xC4/0xC9 (morte, 3 tons)
CE: 80 c3 ->7D                                  tune 0xCE (lápide)
D1: 3 c4 ; D3: 2 c2 ->D1                         tune 0xD1 (eclosão, 6 repetições)
D6: 12 c2 ; D8: 12 c4 ; DA: 12 c6 ; DC: 12 c4 ; DE: 12 c2 ; E0: 12 pausa ; E2: 16 c1 ->7D     tune 0xD6 (jingle)
E5: 12 c4 ; E7: 6 c2 ; E9: 12 c3 ; EB: 6 c4 ; ED: 16 c5 ->7D                                  tune 0xE5
F0: 6 c1 ; F2: 7 c2 ; F4: 8 c3 ; F6: 10 c4 ; F8: 12 c5 ; FA: 14 c6 ->7D                        tune 0xF0 (subida)
```
Quem inicia (ponteiro em Y, repetições em A, B=0 ou F): ver `grep` nas chamadas a `if_0x7B_set_clear_0x32_store_*` (`M[7B]=0` desliga
som): 0x80 (reset, 0x21B/0x23A), 0x94 (bipe padrão: 0x27F,0x2D0,...,0x82A), 0xA5 (0x31A), 0xAE (0x34D), 0xBF/0xC4/0xC9/0xCE (0x381-0x39F),
0xD6 (0x641), 0x97 (0x668/0x67D), 0xE5 (0x6A7), 0x83 (0x6C9), 0x9E (0x6CE), 0xF0 (0xAE8), 0xD1 (0xFF1, eclosão).
Os valores de freq `ctrl&7` mapeiam para `hw_set_buzzer_freq` (4096, 3277, 2731, 2341, 2048, 1638, 1365, 1170 Hz (?) — ver hal do tamalib).

Pseudo-C (`jp_table_0xE7D_buzzer` = lookup do passo):
```c
typedef struct { uint8_t dur, ctrl; int next; /* -1 = seguinte */ } SoundStep;   // extraídos da ROM em build (0xE7D..0xEFC)
```

---------------------------------------------------------------------------------------------------

## 4. `check_events` — 0xF00-0xFD8 (agg23 `label_357`)
Chamada "de espera" a partir de todos os laços de UI (0x2AF tela do relógio, 0x52D loop do bicho, 0x6C3, 0x831, 0xE75 menu).
Calcula o **evento pendente M[5C]** (0=nenhum) e retorna **NZ se M[5C]≠0** (o chamador despacha em 0x307, notas 0x200-0x3FF).
Só trabalha se `M[7C]==0xF` (bicho já eclodiu) e `M[5C]==0`. XP=0 para `P`, YP=2 para `Q`. As variáveis de contagem (Q0-Q17)
são decrementadas por `minute_tick` (0xC7A, notas 0xC00-0xDFF); aqui só se testam limiares.
Eventos: 1 hora de dormir; 2 coração de fome/felicidade zerou; 3 chamada "disciplina" (?); 4 temporizador de saúde (Q6/7=0);
5 evoluir; 6 morte.

RAM (todas confirmadas por trace baby0.sav, exceto as marcadas (?)):
| endereço | significado |
|---|---|
| P7C | fase do ovo (5->0 em 5 min; F = eclodido) |
| P5C | evento pendente |
| P5D | forma/personagem (0 ovo,1 baby,...) — índice da tabela de parâmetros §6 |
| P50 | ramo de evolução (F = forma final, sem mais evolução) |
| P14,P15 | **hora do dia, binária** (0..23); relógio: P10/11 seg, P12/13 min, P14/15 hora |
| P4A bit3 | dormindo; P4B=F "acabou de acordar / novo dia" |
| P48 bit3 | doente; P49 nº de doenças (>=3 => evento 6) |
| P40,P41 | corações de fome/felicidade em 1/4 (4 = 1 coração) |
| P42 | erros de cuidado (sat F); P51 contador de "disciplina" (sat F); P4F, P4E vida útil/idade (F=esgotada) |
| P43 | passo do acumulador de chamada (init 0; =8 se P50 in {2,4}) (?) |
| P54,P55 | idade em dias, BCD, satura em 99 |
| Q0:1 / Q2:3 | contagem (min) até perder 1/4... de coração (fome / felicidade); recarga de Q34:35 / Q36:37 |
| Q4:5 | minutos dormindo (sono) — Q5>=4 e !=F => erro de cuidado (luz acesa?) (?) |
| Q6:7 | temporizador do evento 4 |
| Q8 / Q9 | contadores de chamada 1 e 2 (+1/min, F => penalidade) |
| QB,QC | parte do contador "minutos com coração vazio" Q[A..C]; >=0x2D0 (12 h) e !=baby => morte |
| QD,QE,QF | contador doença/cocô Q[D..F]: 0 e saudável => doente; doente e >=0x160 => morte |
| Q10..12 | tempo restante da forma atual (min); 0 => evolui/vida útil |
| Q13,Q14 | Q13 conta perdas de coração (recarga Q243); Q14 acumulador de chamada |
| Q15 | baby: duração do cochilo (min); Q16:17 baby: tempo acordado (init 0x28) |
| Q30..Q33 | hora de acordar (Q31:30) e de dormir (Q33:32) — vêm do bloco da forma §6 |

Pseudo-C fiel:
```c
bool check_events(void) {                      // retorna M[5C] != 0
    if (P[7C] != 0xF) goto done;
    if (P[5C] != 0)   goto done;
    uint8_t hour = atomic(P[14] | P[15]<<4);   // clear_0x07D; ldpx a,mx; ld b,mx; set_f_0x07D
    uint8_t wake = Q8(0x30), bed = Q8(0x32);   // Q[31]:Q[30], Q[33]:Q[32]
    if (P[4A] & 8) {                           // ---- dormindo
        bool wake_now;
        if (P[5D] == 1) wake_now = (Q[15] == 0);                  // baby: cochilo acabou
        else            wake_now = !(hour < wake) && (hour < bed); // wake <= hour < bed
        if (wake_now) {
            P[4B] = 0xF; P[4A] = 0;                               // acordou
            age_bcd_inc();                                        // D=1: P54 += 1; P55 += carry; se estourar P54=P55=9; D=0
            if (P[50] == 0xF && is_zero3(Q[0x10])) {              // forma final, vida útil esgotada
                Q8_set(0x34, Q8(0x34) - (Q8(0x34) >> 2));         // shrink_interval_25pct: intervalo *= 0.75 ao dia
                Q8_set(0x36, Q8(0x36) - (Q8(0x36) >> 2));
            }
            goto done;                                            // (F2C/F2F: se P50!=F ou Q10..12!=0 também sai)
        }
        if (Q[5] != 0xF && Q[5] >= 4) { Q[5] = 0xF; add_care_mistake(); }  // ainda é noite; 0xF35-0xF3B
        goto done;
    }
    // ---- acordado: hora de dormir?
    if (P[5D] == 1) { if (Q[15] != 0 && is_zero2(Q[0x16])) { P[5C] = 1; goto done; } }   // baby: tempo acordado Q16:17 esgotou
    else            { if (hour >= bed || hour < wake)        { P[5C] = 1; goto done; } }
    // ---- condições de morte (evento 6)
    bool dead = (P[49] >= 3);
    if (P[5D] != 1) {                                         // baby é imune às duas seguintes
        if (Q[0xC] >= 2 && Q[0xB] >= 0xD) dead = true;        // >= 0x2D0 min com coração vazio (Q[A..C])
        if ((P[48] & 8) && Q[0xF] >= 1 && Q[0xE] >= 6) dead = true;  // doente por >= 0x160 min
    }
    if (P[4F] >= 5) dead = true;
    if (dead) { P[5C] = 6; goto done; }
    // ---- evolução / vida útil
    if (is_zero3(Q[0x10])) {                                  // contador da forma chegou a 0
        if (P[50] < 0xF) { P[5C] = 5; goto done; }            // evoluir
        P[4E] = 0xF;                                          // forma final: vida útil esgotada
    }
    if (is_zero2(Q[6])) { P[5C] = 4; goto done; }
    if (!(P[48] & 8) && is_zero3(Q[0xD])) { P[48] = 0xF; P[49]++; }        // adoece
    if (is_zero2(Q[0])) {                                     // fome: contador zerou
        Q8_set(0, Q8(0x34));                                  // recarrega (atômico)
        if (P[40] >= 4) { P[40] -= 4; if (Q[13]) Q[13]--; }   // add mx,0xC / carry
        else            { P[40] = 0; P[5C] = 2; goto done; }
    }
    if (is_zero2(Q[2])) {                                     // felicidade
        Q8_set(2, Q8(0x36));
        if (P[41] >= 4) { P[41] -= 4; if (Q[13]) Q[13]--; }
        else            { P[41] = 0; P[5C] = 2; goto done; }
    }
    if (Q[13] == 0) {                                         // 0xFB7-0xFC5
        Q[13] = Q[0x43];
        unsigned s = Q[14] + P[43] + 1; Q[14] = s & 0xF;
        if (!(s > 0xF) && P[5D] != 1) { P[5C] = 3; goto done; }   // evento 3 só se NÃO houve carry e não é baby
    }
    if (Q[8] == 0xF) { Q[8] = 0; if (P[5D] != 1) add_care_mistake(); }   // chamada 1 ignorada
    if (Q[9] == 0xF) { Q[9] = 0; P[51] = sat_inc4(P[51]); }              // chamada 2 ignorada
done:
    return P[5C] != 0;                                        // label_385: cp P5C,0 ; NZ => há evento
}
```
Observações:
- Ordem do código: mortes (`label_367`-`label_371`) antes de evolução; 0xF71 usa `P[4F]<5`. Ordem de checagem confirmada pelas
  saídas: cada `set_0x5C_to_a` (0xFD4) é um `return`.
- O ramo "dormindo" não verifica hora de acordar para baby via limiares (usa Q15). Ramo "acordado, baby": só dorme se Q15≠0 e Q16:17==0
  (0xF4A-0xF52; `calz label_31` = testa 2 nibbles).
- Evento 3 (P43/Q13/Q14): lógica de "acumulador" literalmente como no código; significado de jogo = chamada de disciplina (?).
- `P[4B]=F` ao acordar é consumido fora desta faixa (idade/"novo dia") (?).
Evidência: trace baby0.sav (3600 s): P5C=2 (0xFD5) a cada perda de coração, P5C=4 em t=864 s; Q0x208 reseta em 0xFC9; P40 `0->C->0` (0xF99/F9B)
confirma `add mx,0xC`; hora 0x08->0x09 em P14/P15.

### Helpers
| endereço | nome | efeito |
|---|---|---|
| 0xFD4-0xFD5 `set_0x5C_to_a` | `set_event(a)` | `P[5C]=A` e cai em 0xFD6 |
| 0xFD6-0xFD8 `label_385` | `event_pending()` | `cp P[5C],0; ret` (NZ = evento) |
| 0xFD9-0xFDE `label_386` | `cmp8_hour(Y)` | compara `B:A` (8 bits, hora) com `Q[Y]:Q[Y-1]` (Y aponta o nibble ALTO): `cp b,my`; se igual `Y--` e `cp a,my`. C=1 se hora < valor. Usada com Y=0x31 (acordar), 0x33 (dormir) |
| 0xF3D-0xF49 `label_363` | `shrink_interval_25pct(Y)` | `v=Q8(Y); Q8(Y)=v-(v>>2)` (rrc/rrc/and b,3 + sub/sbc); em Y=0x34 e 0x36 |
| 0xFDF-0xFE7 `label_388` | `add_care_mistake()` | `P[42]=sat_inc4`; se `P[50]==F && Q[10..12]==0` também `P[4F]=sat_inc4` |
| 0xFE8-0xFEB `label_389` | `sat_inc4(X)` | `M[X]++`, trava em F |
| 0xFEC-0xFFE `label_391` | `hatch_egg()` | ver abaixo |

`label_31` (0xD4) / `label_32` (0xD7), usados acima: `is_zero2(Y)` / `is_zero3(Y)` = Z se `Q[Y]|Q[Y+1](|Q[Y+2])==0` (com interrupção desligada).

## 5. `hatch_egg` — 0xFEC-0xFFE (`label_391`)
Chamada em 0x516 quando `P[7C]==0` (o ovo nasceu). Faz: `P[7C]=F`; toca tune 0xD1 (6 repetições, B=0) se `P[7B]`; `play_anim(A=2,B=4 s, id=0)`;
`P[74]=F`; `P[5D]=1,P[5E]=1` (Babytchi); `label_142` (0x465, prepara tabelas/sprite) e `jp label_347` (0xDD2 `load_form_params`: carrega o
bloco de §6 para Q[0x30..0x49] e copia Q[0x240..242]->Q[0x210..212], Q[0x238..23A]->Q[0x20D..20F]).
```c
void hatch_egg(void){ P[7C]=0xF; play_tune(0xD1, /*rep*/6, 0); play_anim(2,4,/*id*/0);
    P[74]=0xF; P[5D]=1; P[5E]=1; render_pet(); load_form_params(); }
```

---------------------------------------------------------------------------------------------------

## 6. Parâmetros por forma: `stage_param_jump_table` 0x1558 + blocos 0x1564-0x15E5
`load_form_params` (0xDD2): `A = P[5D]+8; B = 5` -> M0/M1; M2=5 -> `render_asset` (página 0x15, offset `0x58+P[5D]`) com X=0x230 (XP=2).
O destino é um dos 12 `jp` em 0x1558..0x1563 (`jp 0x64,0x64,0x71,0x7E,0x8B,0x98,0xA5,0xB2,0xBF,0xCC,0xD9,0x98` na página 0x15), que
leva a um bloco de **13 bytes** (`lbpx` x12 + `retd`) = 26 nibbles em **Q[0x30..0x49]**. Índice = P[5D] (0..11). **Dados de jogo**:

Layout (byte = par de nibbles, lo primeiro, endereço Q):
| byte | Q | campo |
|---|---|---|
| 0 | 30,31 | hora de acordar (0xFF = n/a: ovo/baby) |
| 1 | 32,33 | hora de dormir |
| 2 | 34,35 | intervalo (min) de perda de 1/4 de coração de **fome** (recarga de Q0:1) |
| 3 | 36,37 | idem **felicidade** (Q2:3) |
| 4-5 | 38-3A | 12 bits: recarga do contador Q[D..F] (doença/cocô) — copiado p/ Q0D em `load_form_params` |
| 5 hi | 3B | somado a P[48] em 0x612 (`add mx,my`) (?) |
| 6,7 | 3C:3D, 3E:3F | peso mín. e máx. (BCD) — grampo em 0x6E5 (`label_214`: se peso<mín -> mín; senão se peso>=máx -> máx) (?) |
| 8-9 | 40-42 | 12 bits: duração da forma (min) -> Q[10..12] |
| 9 hi | 43 | recarga de Q13 (nº de perdas de coração entre chamadas) |
| 10 | 44,45 | (?) Q45 usado em 0x657 (`label_282`) |
| 11 | 46,47 | peso inicial (BCD) (?) |
| 12 | 48,49 | flag em Q48 (bit3 só testado em 0xDEA, nunca setado nos dados), Q49 (?) |

Tabela (valores hex; idx = P[5D]):
```
idx bloco  acordar dormir fome felic Q0D-F(min) Q3B peso_mín peso_máx duração(Q10-12) Q43 Q44,45 peso0 Q48,49
0,1 1564    FF     FF     03   04    02D        C     05       05       03C(60 min)     F   0,8     11    0,0
2   1571    09     14     32   3C    3DE        C     10       99       564             6   0,8     11    0,0
3   157E    09     15     4B   55    678        C     20       99       8AC             6   0,8     11    1,0
4   158B    09     15     4B   55    294        C     20       99       564             6   0,8     11    0,0
5   1598    09     16     51   5B    F3C        8     30       99       FFF             F   F,8     11    1,0
6   15A5    09     16     51   5B    AF8        8     30       99       C30             7   8,8     11    1,0
7   15B2    0B     17     37   41    A20        8     30       99       B40             7   0,B     23    1,0
8   15BF    09     16     3C   46    492        C     20       99       618             F   F,5     43    1,0
9   15CC    09     16     3C   46    168        D     10       99       30C             7   8,8     11    0,0
10  15D9    0A     16     2D   32    294        C     20       99       5A0             7   0,8     11    1,0
11  1598 (igual a idx 5)
```
(hora de acordar/dormir em hora binária: 0x14=20h, 0x15=21h, 0x16=22h, 0x17=23h). 5 e 11 apontam para o mesmo bloco (`jp 0x98` nas
posições 0x155D e 0x1563). Duração 0xFFF para idx 5/11 (nunca expira). Nomes das formas: ver 0x200-0x3FF.md §8 (0=ovo,1=Babytchi,2=Marutchi,
3=Tamatchi, 4=Kuchitamatchi, 5..B adultos (?)).
Evidência: a RAM em 0x230 do `out/baby0.sav` (`FFFF 3040 D20C 5050 C30F 0811`) é exatamente o bloco de 0x1564; o par `09,14..17` casa com
os horários; 0x03C=60 min é o tempo do Babytchi até evoluir; Q0D=02D (45) casa com o valor inicial citado em 0x200-0x3FF.md.

## 7. `hour24_to_12_table` — 0x15E6-0x15FD (24 `retd`, indexada por hora 0..23)
`pset 0x15; call; jpba` com B:A = hora (0..23), grava em M[X],M[X+1] o byte (BCD): 
`12,01,02,03,04,05,06,07,08,09,10,11 | 92,81,82,83,84,85,86,87,88,89,90,91` (bit7 = PM). Os nibbles ficam: lo = unidades, hi = dezena (+8 = PM).
Usado pela tela do relógio (render_clock, 0x5xx). Pseudo-C:
```c
uint8_t hour12_bcd(uint8_t h){ uint8_t h12 = h%12 ? h%12 : 12; uint8_t bcd = (h12/10)<<4 | (h12%10); return bcd | (h>=12 ? 0x80:0); }
```

---------------------------------------------------------------------------------------------------

## 8. Banco 1: gráficos 0x1000-0x17FF

### 8.1 Como o sprite é endereçado
`render_asset` (0x000): `A=(M2&7)*4` com carry em B (b:a ∈ {0..0x1C}); `pset 0x10; jp gfx_jp_ret_zp` (0x1020) -> `call gfx_jp_table_jpba`
(0x1023 = `jpba`) cai em `0x1000 + 4*(M2&7)`. Cada uma das 8 entradas (0x1000,0x1004,...,0x101C) é
`ld a,m0 ; ld b,m1 ; pset 0x1k ; jpba`, isto é, **salta para a página 0x10+k, offset `M1:M0`**:

    endereço do sprite = 0x1000 + 0x100*(M2 & 7) + 0x10*M1 + M0         (M0,M1,M2 = RAM 0x000,0x001,0x002)

O código lá é uma fila de `lbpx mx,imm` terminada por `retd`; ele **escreve diretamente em RAM em X** (XP,XH,XL do chamador) e retorna ao
`call` em 0x1020, que vai para 0x1021 `pset 0; jp set_f_0x07D` (0x009), que religa interrupções e retorna ao chamador de render_asset.
Ao retornar, X aponta logo depois dos dados escritos (preservado por `set_f_0x07D`). Para a reescrita: não precisa emular; basta
extrair em build: `sprite_words(addr)`.

**Formato**: 1 palavra = 1 coluna de 8 pixels: `imm = 0x9II` (`lbpx`) ou `0x1II` (`retd`, última); byte `II`: **bit0 = linha de cima**, bit7 = linha de
baixo; nibble baixo = M[X] (linhas 0-3), nibble alto = M[X+1] (linhas 4-7). Comprimento = nº de palavras até o `retd` (inclusive).
```c
// extrai: devolve nº de colunas; cols[i] = byte (bit0 = topo)
int sprite_read(const uint16_t *rom, uint16_t addr, uint8_t *cols) {
    int n=0; for(;;){ uint16_t w = rom[addr++] & 0xFFF; cols[n++] = w & 0xFF; if ((w>>8)==0x1) return n; /* (w>>8)==0x9: continua */ }
}
```
(Verificado imprimindo 0x16C0/0x1030/0x16E0/0x1600 como ASCII: Babytchi, coração, "0": bit0 = topo.)

### 8.2 Tamanhos / regiões
| região | granularidade | tamanho | usos / fórmula |
|---|---|---|---|
| 0x1030-0x14FF (exceto 0x11E0-0x122F) | blocos de 16 palavras (`LLLLLLLLLLLLLLLR`) | 16 colunas x 8 linhas (metade superior ou inferior de um bicho 16x16) | `0x1000 + 0x10*spr`, `spr=(id&0x7F)+3` (0x949-0x95F: M25:M24+3 -> M2:M1, M0=0); o desenho dos bichos é feito em 2 metades (superior em linhas 0-7, inferior em 8-15). Lista em "Useful Addresses.md" |
| 0x1500-0x1527 | 4 palavras | 4 col. (dígitos grandes 0-9) | `0x1500 + 4*d` (`setup_addr_and_render_clock` 0x5F3, M0=4d, M1, M2=5) |
| 0x1528-0x154F | slots de 4 palavras com retd em posição variável | dígitos pequenos 3 col. (0x1528+4n, n=0..9), cada slot 3 colunas + `retd` extra | `render_clock_with_b_0` (0x5EF): `0x1500 + 4*(a+10)` |
| 0x1550-0x1557 | 4 palavras | seta preenchida (0x1550) e vazada (0x1554), 3 col. (+1 coluna vazia) | ícones de cursor |
| 0x1558-0x15FD | dados de jogo (ver §6, §7) | | |
| 0x1600-0x17FF | **8 palavras** (`LLLLLLLR`) | glifo 8 col. x 8 linhas | `0x1600 + 8*g`, `g = 16*B + A` (0..63); ver 8.3 |
| 0x17E0-0x17FF | glifos em branco | | `g=0x3C..0x3F` |
Os blocos estão alinhados em 16 (ou 8) palavras e o último é sempre `retd`; sprites podem ser lidos pelo mesmo `sprite_read` (comprimento natural).
Não-sprites dentro de 0x1000-0x17FF: 0x1000-0x102F (dispatcher), 0x11E0-0x122F (código), 0x1550-0x1563 (jp), 0x1564-0x15E5 (dados), 0x15E6-0x15FD (tabela).

### 8.3 Glifos 8x8 (0x1600-0x17FF) via `misc_render` (0x7A5, notas 0x400-0x5FF / 0x792 `label_233`)
Entrada A=g_lo, B=g_hi (par de nibbles lidos de `M[Y],M[Y+1]`): `g=B*16+A`. `B>=4` => `clear_16_addrs` (limpa 16 nibbles em X). Senão
`M0=(A&1)<<3; M1=(A>>1)|((B&1)<<3); M2=6+(B>>1)` => `0x1600 + 8*g` (conferido: A=1 -> 0x1608; B=1 -> 0x1680). A rotina 0x7A5 assume o carry
limpo ao entrar (`rrc a` sem `rst f` anterior) (?). `label_233` desenha 8 glifos de `M[0x90..0x9F]` (pares) em X crescente de 16
nibbles (8 colunas) cada, trocando para a 2ª linha depois de 4 (`adc xh,4`). Nome de cada glifo: tabela do agg23 ("Useful Addresses.md", 0x1600-0x17D8).

### 8.4 Buffer de vídeo e mapeamento para o LCD (para extrair/desenhar)
O LCD E0C6S46 mapeia o nibble `o` da VRAM (0xE00+o = linhas 0-7... banco 0; 0xE80+o = linhas 8-15) assim (cpu.c `set_lcd`): `seg=o>>1`,
`com0=bank*8 + (o&1)*4`, pixel bit `i` do nibble = `(seg_x, com0+i)`, com `seg_x` da tabela de hw.c:
`seg_x[40]={0,1,2,3,4,5,6,7,32,8,9,10,11,12,13,14,15,33,34,35,31,30,29,28,27,26,25,24,36,23,22,21,20,19,18,17,16,37,38,39}`
(valores >=32 são ícones; seg 8 = ícones, 17-19, 28, 37-39 também). Ou seja a coluna x (0..31): x<8: seg x; 8<=x<16: seg x+1; 16<=x<24: seg 52-x; 24<=x<32: seg 51-x.
O jogo desenha num **buffer RAM em página 1** e `copy_video_buf_to_vram` (0x056) o copia para a VRAM, **empacotado por colunas**: a coluna x do buffer
está em `B + 2x` (linhas 0-7) e `B + 0x80 + 2x` (linhas 8-15), onde `B = 0x100 + 0x10*M[2B] + M[2A]` (janela; `clear_page_0x100` põe M2A=0 => B=0x100;
`label_30` põe B=0x140). Logo uma palavra de sprite escrita em `X = B+2x` ocupa a coluna x, e uma imagem de 16 colunas largura cobre `B+2x .. B+2x+31`.
O buffer tem folga de 2 telas (0x100-0x13F+0x180-0x1BF e +0x40), usada para deslizar a janela (animação de entrada, 0xA00 notas). Os ícones (seg 8, 17-19,
28, 37-39) são copiados à parte (`label_242`, máscara em M2C:2D).
Quem escreve o sprite define XP=1 e `X = (M3B:M3A)` = cursor (x,y) (`label_233`, 0x792): `X = 0x100 + 0x10*M3B + M3A`.

---------------------------------------------------------------------------------------------------

## 9. Banco 1: código — inicialização
### 9.1 `set_init_mem_and_int` — 0x11E0-0x11F6 (+ continuação 0x244 e 0x1200-0x1221)
Chamada de `springboard_set_init_mem_and_int` (0x242 -> `pset 0x11; jp 0x1E0`), no reset (0x237) e no soft reset (0x211). Retorna ao banco 0
via `copy`/`set_init_mem_and_int_bank0_ret` (0x255). Seqüência:
1. I/O (XP=F): `F10..F14 = 8,0,1,0,0` (máscara clock timer: só 1 Hz; stopwatch off; **prog timer on**; serial/K0 off), `F26..27 = 07` (recarga prog timer),
   `F54 = F` (buzzer/saídas desligados), `F71 = 8,8` (LCD tudo apagado; contraste médio), `F76 = 3,2,2,2` (reset clock/stopwatch/prog timer; prog timer a 256 Hz).
2. (0x244, banco 0): zera RAM 0x000-0x06F, 0x100-0x17F, 0x200-0x237, VRAM 0xE50-0xE77 e 0xE80-0xEA7 (nibbles).
3. `set_init_mem_and_int_cont_set_mem` (0x1200): `P[24:25]=0x00` (quadro/sprite atual), `P[14:15]=0x02,0x00`->**hora=0x20 (>=24 = relógio não acertado)** (?),
   `P[32:33]=0`, `P[36:37]=0x7D` (música idle), `P[58:59]=FF`, `P[7B]=F` (som ligado), `P[7D]=F` (interrupções ok), `P[3C]=0` (relógio parado),
   `P[74]=0`, `P[7C]=5` (**ovo: 5 min**), `F71=0` (LCD normal), `F78=1` (inicia prog timer), lê F00-F02 (limpa fatores de interrupção).
```c
void init_mem_and_int(void){ /* I/O e limpezas acima */ M[0x024]=M[0x025]=0; M[0x014]=0; M[0x015]=2; M[0x032]=M[0x033]=0; M[0x036]=0xD; M[0x037]=7;
    M[0x058]=M[0x059]=0xF; M[0x07B]=0xF; M[0x07D]=0xF; M[0x03C]=0; M[0x074]=0; M[0x07C]=5; /* F71=0; F78=1; read F00..F02 */ }
```
### 9.2 `copy_6_mx_my` — 0x1222-0x122E
`ldpx my,mx` x6 intercalado com `ldpy a,a` (Y++): copia 6 nibbles de M[X..X+5] para M[Y..Y+5] (usado só para salvar/restaurar o relógio
0x010..0x015 <-> 0x272..0x277 no soft reset, 0x20C e 0x218); retorna ao banco 0 por 0x229. 0x122F `nop7`.

### 9.3 `gfx_dispatch` — 0x1000-0x102F
0x1000-0x101F: 8 entradas de 4 palavras (ver 8.1). 0x1020-0x1022 `gfx_jp_ret_zp`: `call 0x1023; pset 0; jp set_f_0x07D`. 0x1023 `jpba`.
0x1024-0x102F: `nop7` (padding). O primeiro sprite começa em 0x1030.

---------------------------------------------------------------------------------------------------

## 10. Pontos em aberto (?)
- Semântica fina dos eventos 3 e 4, de P43, Q13/Q14 e de Q4:5 (light/sleep) — descritos exatamente como no código; nomes de jogo hipotéticos.
- Ajuste de XH para baby em 0x8DF-0x8EF (anim script) e uso exato de `M[AA]` (hold) — ver código em 0x8C0-0x8F3.
- Campos Q3B, Q44/45, Q46/47, Q48/49 do bloco de forma (§6): origem/uso só parcialmente rastreado (0x612, 0x657, 0x6E5, 0xDEA).
- Carry-in de `rrc a` em `misc_render` (0x7A7).
- `rom` lida por script (python) e conferida por trace (baby0.sav) só nos pontos citados; as tunes e animações não foram reproduzidas em áudio/vídeo.
