# ROM 0xA00–0xBFF

Fontes: `tools/asm.py 0xA00 0xBFF`, `tamatool/src/lib/cpu.c`, caminhos exercitados em `out/a00` (harness, só a fase do relógio/ovo; cov.bin
confirmou A01/ABD/B01/BC0/BCA/BCB/BCC no boot). (?) = hipótese.

A faixa tem só 3 rotinas de código reais (0xABE–0xAFF), um stub de lookup (0xABD) e o resto é dado: duas tabelas `retd`
(0xA00–0xABC, 0xB00–0xBBF), uma tabela de ponteiros + scripts de animação (0xBC0–0xBE7) e três "strings" de tiles
(0xBE8–0xBFB). 0xBFC–0xBFF = nop7 (padding).

## Convenção de tabelas `retd` (vale para A00, B00, BC0, 98x, C00 …)
`pset P; call X` onde X é um stub `jpba` → PC = P<<8 | (B<<4 | A). A palavra apontada é `retd v`: retorna ao chamador e grava
`M(X)=v & 0xF`, `M(X+1)=v>>4`, e `X += 2` (mantém XP). Ou seja: **B:A = índice de 8 bits, resultado = byte em M(X),M(X+1)**
(nibble baixo primeiro). Uma palavra `lbpx mx,v` na posição indexada faz o mesmo e **continua** executando a próxima
palavra (que será outro lbpx/retd): grava vários bytes seguidos. É assim que os "scripts" abaixo trocam o ponteiro.

---

## 0xA00–0xABC  `pose_sprite_table`  (agg23: "Jump table for jp_table_0xA00")
Tabela 12 linhas x 16 colunas, índice = `(M5D << 4) | M5E` (M5D = id do personagem/estágio 0..B, M5E = "modo de pose"
1..A; col 0 e cols > A são 0/não usadas). Valor `v`: **M6E = v & 0xF, M6F = v >> 4** = ids dos sprites do frame A e do frame B da
pose (o desenho escolhe M6E ou M6F conforme bits do script, ver 0xBCA). (?) significado exato dos ids: indexam a tabela de
sprites em C00/D00 via `label_288/289` (0x928/0x939), fora da faixa.
Linha 0 (ovo) só tem cols 0–4.

```
M5D\M5E  0  1  2  3  4  5  6  7  8  9  A  B
 0      00 10 21 33 44
 1      00 15 12 33 43 45 00 41 61 13 33
 2      00 15 03 87 B9 25 A9 B1 47 78 88
 3      00 13 02 44 65 87 99 A1 CB BB BB
 4      00 10 03 65 47 22 77 42 87 78 88
 5      00 11 02 33 24 51 76 81 76 66 66
 6      00 21 03 44 35 11 85 60 75 75 55
 7      00 10 32 55 26 77 98 47 26 66 66
 8      00 10 02 33 24 76 54 80 94 94 44
 9      00 21 43 55 36 87 96 3A BB 5B BB
 A      00 01 12 33 24 22 54 22 64 54 44
 B      00 10 02 33 21 44 55 44 51 11 11 11   (única linha com col B; termina em 0xABB; 0xABC = retd 0)
```
(valores em hex, byte `v`; ex. M5D=1,M5E=1 -> 0x15 -> M6E=5, M6F=1.)

Leitores: `label_142` 0x46F (X=0x6E, B:A = M5D:M5E -> M6E/M6F) e `label_125` 0x3DF (A=4, B=M6 = id do personagem,
X=0 -> M0/M1; usa M1 = sprite do frame B como retrato estático; usado na animação de evolução, 0x392–0x39B).

### 0xABD  `lookup_pose_sprites` (agg23 `jp_table_0xA00`)
`jpba` stub. Chamar com `pset 0xA; call 0xABD`. Entrada B:A, X=destino; saída M(X),M(X+1). Callers: 0x3DF, 0x46F.

---

## 0xABE–0xAD9  `menu_light`  (labels 297–300)  — menu "luz"
Entrada: `jp` do despachante de menu (tabela de `pset;jp` em 0x700, entrada 0x704; ver 0x54B..0x549). Termina com `jp label_160`
(0x51A, laço do menu) ou `label_159` (0x517, sai do menu). Não retorna com `ret`.
Pseudo-C:
```c
void menu_light(void) {                    // 0xABE
    M[0x78] = (M[0x4B] & 1) ^ 1;           // cursor inicial: 0 se luz ligada, 1 se desligada
    copy_tiles(0x90, tiles_light);         // call 0xBF0, escreve 16 nibbles em M[0x90..0x9F]
    flags = choose_2(/*B=*/0xA);           // label_352 @0xE5A; A = opção (0/1), M[0x78] = cursor
    if (!C) {                              // M pressionado (NC,Z): confirma
        if (A == 0) M[0x4B] = 0xF;         // opção 0: luz LIGADA   (M4B: F = on, 0 = off)
        else { M[0x4B] = 0; M[0x205] = 0xF; } // opção 1: luz DESLIGADA; M205 = F (evento "apagou a luz"?) (?)
        goto menu_loop;                    // label_160
    }
    if (!Z) goto menu_exit;                // C,NZ = timeout/saída (label_159)
    goto menu_loop;                        // C,Z = R pressionado (cancelar)
}
```
RAM: 0x4B (estado da luz, bit0 usado), 0x78 (cursor/escolha, saída de choose_2), 0x205 (?, setado ao apagar).
Chama: label_309 (0xBF0), label_352 (0xE5A).

## 0xADA–0xAEF  `menu_discipline`  (labels 301–304) — ícone "disciplina"
Entrada: `jp` de 0x70F (slot 0x70E da tabela de menu). Termina em `label_160`.
```c
void menu_discipline(void) {               // 0xADA
    if (!(M[0x4A] & 8)) {                  // bit alto de 0x04A = (?) pet dormindo/sem poder ser disciplinado
        if (M[0x209] != 0) {               // (?) flag "pet fez birra / chamada de atenção pendente"
            M[0x209] = 0;
            M[0x43] = sat_add_nib(M[0x43], 4);   // +4, satura em 0xF  -> medidor de disciplina (4 unid. = 1 barra) (?)
            sound(0xF0);                   // if_0x7B_set_clear_0x32_store_yhl_set_0x059_to_0xF com Y=0xF0 (só toca se M[0x7B]!=0)
        }
        play_reaction(/*M5E*/8, /*M57*/5, /*Y*/3);  // label_273 @0x8A9: roda script da tabela E00 (Y=3) com modo de pose 8
    }
    goto menu_loop;                        // label_160
}
```
RAM: 0x4A bit3, 0x209, 0x43 (0x043: disciplina), 0x7B (som on?), 0x5E/0x57 (via label_273).

## 0xAF0–0xAFF  `blit_buffer_slide` (labels 305/306; chamada `label_305`)
Copia repetidamente o buffer de vídeo (página 0x100) para a VRAM, **deslizando a origem** da cópia; é a animação de
"scroll" (limpar/entrada da tela). Entrada: A = limite (nibble alto do offset final), B = passo (nibbles). Sai com `ret`.
```c
void blit_buffer_slide(u4 A, u4 B) {       // 0xAF0
    M[0] = A;                              // m0 = A (clobbera M[0])
    M[0x2A] = M[0x2B] = 0;                 // offset de origem = 0   ({M2B,M2A} é o byte baixo do ponteiro 0x1xx)
    for (;;) {
        copy_video_buf_to_vram();          // calz 0x56: lê de 0x100 + {M2B,M2A} -> 0xE00/0xE80...
        if (M[0x2B] >= M[0])  return;      // cp mx,a ; jp nc
        {M[0x2B],M[0x2A]} += B;            // add mx,b ; adc mx,0
    }
}
```
Sem espera explícita: o tempo vem do custo da cópia (CPU a 32 kHz). Equivalente a `label_156` (0x500) mas contando para cima.
Callers: 0x3C7 (A=2,B=2), 0x4FB (A=4,B=4), 0xD88 (A=4,B=4; toilet/limpar cocô: após `M[0x4D]=0`).
Efeitos: M0, M2A, M2B, VRAM. Chama calz `zero_a_xp`, `copy_video_buf_to_vram`.

---

## 0xB00–0xBBF  `pose_anim_table`  (agg23 "Jump table for jp_table_0xB00")
Mesmo formato/índice de A00: `(M5D<<4)|M5E`. Valor `v`: **M0 = v & 0xF = id do script de frames** (índice 0..7 na tabela 0xBC0),
**M1 = v >> 4 = id do script de movimento** (índice p/ tabela de página 9 em 0x98B+M1, que dá o ponteiro M6A/M6B — fora da faixa).
```
M5D\M5E  0  1  2  3  4  5  6  7  8  9  A  B
 0      00 00 A1 00 00
 1      00 20 00 30 11 02 05
 2      00 40 00 00 11 02 00 00 03
 3      00 00 07 51 11 02 05 00 06 05
 4      00 60 00 00 11 04 05
 5      00 05 07 00 11 02 00 00 00 05
 6      00 70 07 80 11 04
 7      00 70 07 50 11 04 00 00 00 05
 8      00 40 07 50 11 02
 9      00 60 00 80 11 02
 A      00 00 07 50 11 04 00 05 00 05
 B      00 90 07 50 11 04 05 05 00 05      (ids de script de frames usados: 0..7)
```
(células vazias = 0x00; linha 0 col 2 = 0xA1 -> script 1, movimento 0xA.) A col 3/4 da maioria é 0x11 (mesmo script/mov. nas poses 4).
Leitores: `label_142` 0x474 (X=0, B:A = M5D:M5E, tail para 0xBC0 depois) e, para os bytes do script, `label_145` via
`jp_table_0xB00_2` (0x4E3).

### 0xBC0–0xBC9  `anim_script_ptr_table`
10 entradas `retd`, índice = M0 (id do script). Valor = offset (na página B) do script: 
`CA, CD, D0, D3, D8, DB, DE, E3, E8, E8`. Entradas 8 e 9 (E8) apontam para dados de tiles e nunca são usadas (B00 só dá 0..7).
`label_142` (0x47A–0x481) faz `jpba` para 0xBC0+M0 com X=0x64 -> **M64/M65 = ponteiro do script** (M64 = nibble baixo).

### 0xBCA–0xBE7  `anim_scripts` (bytes de script lidos um a um por `label_145`, 0x486–0x495)
Execução por tick de animação (`label_145`):
```c
if (M5F-- != 0) goto movimento;            // add mx,0xF ; jp c : contador de frames M5F ainda > 0
p = {M65,M64}; {M65,M64}++;                // ponteiro 8 bits na página B
pset 0xB; call (0xB00 | p)  com X=0x62;    // retd v  -> M62=v&F, M63=v>>4  (e, se for lbpx+retd: M64/M65 = novo ponteiro)
M5F = M62;                                 // duração do passo = M62 + 1 ticks
```
Formato de um passo: **M62 (nibble baixo) = duração-1**; **M63 (nibble alto) = flags de desenho**:
bit1 = usa sprite M6F (frame B) na metade de cima, bit0 = idem na metade de baixo (senão M6E),
bit3 = espelhar metade de cima (XOR com M70.bit1), bit2 = espelhar metade de baixo (XOR com M70.bit0).
(derivado de 0x4A8–0x4E1, que chama label_288/289 em página 9; (?) qual metade é "cima/baixo" — o primeiro desenho usa `fan b,2`, o segundo `fan b,1`.)
Um par `lbpx v; retd p` escreve 4 nibbles = passo `v` **e** M64/M65=`p`: é o "goto p" (laço) do script.

Scripts (endereço = offset em página B; `;loop` = lbpx+retd que volta ao endereço indicado):
```
id  entrada  passos (v = hi:lo)                          efeito
0   BCA      01  ; 31 -> loop BCA                         2 ticks A (M63=0), 2 ticks B (M63=3): alterna frames A/B. (ovo)
1   BCD      07  ; 37 -> loop BCD                         8 ticks A, 8 ticks B (lento)
2   BD0      00  ; 30 -> loop BD0                         1 tick cada (rápido)
3   BD3      01 31 01 ; F1 -> loop BD3                    A,B,A, depois B espelhado (F1: flips + frame B)
4   BD8      00 ; F0 -> loop BD8                          1 tick A/ 1 tick espelhado (M63=F = ambos flips + B)
5   BDB      01 ; F1 -> loop BDB
6   BDE      01 31 01 ; B1 -> loop BDE                    B1 = flip só metade de cima + frame B
7   BE3      01 31 01 ; 31 -> loop BE6 (fica em 0x31)     A,B,A e fica em B
```
Palavras: BCA:retd 01, BCB:lbpx 31, BCC:retd CA | BCD:retd 07, BCE:lbpx 37, BCF:retd CE | BD0:retd 00, BD1:lbpx 30, BD2:retd D0 |
BD3:01 BD4:31 BD5:01 BD6:lbpx F1 BD7:retd D3 | BD8:00 BD9:lbpx F0 BDA:retd D8 | BDB:01 BDC:lbpx F1 BDD:retd DB |
BDE:01 BDF:31 BE0:01 BE1:lbpx B1 BE2:retd DE | BE3:01 BE4:31 BE5:01 BE6:lbpx 31 BE7:retd E6.
Atenção: o `lbpx v` do par vira passo (M62/M63) e o `retd p` vira M64/M65 (X avança 4 nibbles: 62,63,64,65).
Confirmado no boot (egg, M5D=0,M5E=1 -> script 0): cov mostra BCA 5x, BCB 4x, BCC 4x (alternância).

---

## 0xBE8–0xBFB  strings de tiles (rotinas `call`áveis que só fazem `lbpx`; escrevem na RAM em X e retornam por `retd`)
Cada uma grava 16 nibbles (8 tile-ids de 8 bits) a partir de M(X) (o chamador põe X antes). Valores = ids de tile (0xFF = em branco).
| endereço | label | nome | bytes escritos | uso |
|---|---|---|---|---|
| 0xBE8–0xBEF | label_308 | `tiles_food_choice` | FF 28 29 2A FF 2B 2C 2D | menu de comida (duas opções de 3 tiles, [marcador+ícone]x2); caller 0x719 com X=0x90 |
| 0xBF0–0xBF7 | label_309 | `tiles_light_choice` | FF 38 39 FF FF 0E 17 FF | menu luz; caller 0xAC7 com X=0x90 |
| 0xBF8–0xBFB | label_310 | `tiles_happy_header` | 31 32 33 FF | cabeçalho da tela de status "feliz" (M41); caller 0x88A (ver 0x883 label_264 com 2E 2F 30 FF para "fome") |
Layout das duas primeiras: as 4 primeiras tiles = opção 0, as 4 seguintes = opção 1; `label_352` sobrescreve a 1ª tile da metade
selecionada com 0x27 (cursor) e põe FF na outra (0xE68–0xE70), depois redesenha (label_233). Significado visual dos ids (?).

---

## Rotinas externas relevantes que esta faixa usa (fora da faixa, só contrato)
- `choose_2` = `label_352` @0xE5A: seletor de 2 opções. Entrada: M90..M9F com tiles, M78 = cursor inicial, B = timeout (vai p/ M77/M57 via label_66).
  Saída: A = opção, M78 = cursor. Flags: **NC,Z = confirmou (M)**; **C,Z = cancelou (R)**; **C,NZ = timeout/acabou**. L alterna cursor (M75 mod M76, via label_94 @0x2C3).
- `copy_video_buf_to_vram` = calz 0x56 (0x056): copia a página 0x100 (a partir de `{1,M2B,M2A}`) para 0xE00/0xE80.
- `play_reaction` = `label_273` @0x8A9: M5E←A, M57←B, Y=id do script em tabela E00 que sequencia modos de pose.
- `anim_set_mode` = `label_142` @0x465 (usa as tabelas A00/B00/BC0); `anim_step` = `label_145` @0x486.

## Mapa resumido da RAM (endereços lidos/escritos nesta faixa)
| addr | significado |
|---|---|
| 0x5D | id do personagem/estágio (linha das tabelas A00/B00; 0=ovo, 1=baby, …) |
| 0x5E | modo de pose (coluna): 1 normal (label_176 @0x59x), 3 se M4A.bit3, 9 se M48.bit3, 4 retrato, 8 repreensão… (?) |
| 0x5F | contador de ticks restantes do passo do script de frames |
| 0x62/0x63 | passo atual do script: duração-1 / flags de desenho |
| 0x64/0x65 | ponteiro (offset) do script de frames, página B |
| 0x6A/0x6B | ponteiro do script de movimento (página 9), vem de 0x98B+M1 |
| 0x6E/0x6F | ids de sprite frame A / frame B da pose (tabela A00) |
| 0x0/0x1 | saída temporária das tabelas B00 (script, movimento) e A00 em label_125 |
| 0x2A/0x2B | offset da origem da cópia buffer→VRAM (blit_buffer_slide) |
| 0x4B | luz (F = ligada, 0 = desligada) |
| 0x205 | (?) flag setada ao apagar a luz |
| 0x4A bit3 | (?) pet dormindo/indisposto; bloqueia disciplina |
| 0x209 | (?) pendência de disciplina/atenção (zerada ao disciplinar) |
| 0x43 | medidor de disciplina (+4 por ação, satura em F) (?) |
| 0x75/76/77/78 | cursor, nº de opções, timeout, escolha do seletor de 2 opções |
| 0x90–0x9F | linha de tiles do menu (preenchida pelas strings BE8/BF0) |
| 0x7B | som habilitado (?) — usado por if_0x7B_set… antes de tocar buzzer 0xF0 |
