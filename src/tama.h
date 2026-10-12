/*
 * Tamagotchi P1 — reescrita nativa em C.
 *
 * O jogo original roda num Epson E0C6S46 (4 bits). Esta reescrita reproduz a lógica
 * rotina por rotina, mas com nomes: cada função e campo traz, em comentário, o endereço
 * da ROM / RAM original de onde veio (ver re/notes/).
 *
 * Modelo de execução: o código do jogo é sequencial e "bloqueante", como o original.
 * As duas interrupções do hardware viram funções C (machine.c) chamadas sempre que o
 * código do jogo consome tempo via cost_ms() ou espera ticks.
 */
#ifndef TAMA_H
#define TAMA_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;

/* ---------------------------------------------------------------- ROM (rom.c) */
/* Os dados (sprites, scripts de animação, sons, parâmetros por espécie) são lidos da
 * ROM original em tempo de execução: tabelas do E0C6S46 são sequências de instruções
 * `lbpx mx,b` (grava o byte b e segue) terminadas por `retd b` (grava b e retorna). */
bool rom_load(const char *path);
u16 rom_word(u16 addr);
/* Lê os bytes da tabela que começa em addr até o retd (inclusive). Devolve quantos. */
int rom_table(u16 addr, u8 *out, int max);
/* Byte de uma única entrada de tabela (lbpx ou retd). */
u8 rom_byte(u16 addr);

/* ---------------------------------------------------------------- botões */
enum { BTN_R = 1, BTN_M = 2, BTN_L = 4 };   /* bits como na RAM do jogo (K00=R, K01=M, K02=L) */

/* ---------------------------------------------------------------- máquina (machine.c) */
void machine_reset(void);
void cost_ms(double ms);          /* consome tempo de CPU (dispara as "interrupções") */
void wait_ticks(u8 n);            /* 0x03C: espera n ticks do prog timer (27,25 ms) */
void read_buttons(void);          /* 0x021: snapshot em g.btn_held / g.btn_pressed */
double machine_now(void);         /* tempo emulado em segundos */
bool machine_save(const char *path);  /* estado do jogo + vram + relógio emulado */
bool machine_load(const char *path);

/* Sons (ponteiros na tabela de passos da página E; ver sound.c) */
enum {
	SND_IDLE = 0x7D, SND_BOOT = 0x80, SND_BEEP = 0x94, SND_TOGGLE = 0x97,
	SND_CALL = 0xA5, SND_EVOLVE = 0xAE, SND_DEATH1 = 0xBF, SND_DEATH2 = 0xC4,
	SND_DEATH3 = 0xC9, SND_GRAVE = 0xCE, SND_HATCH = 0xD1,
};
void play_sound(u8 ptr, u8 repeat_lo, u8 repeat_hi); /* 0x0BF: só se g.sound_on */
void play_beep(void);                                 /* 0x0BB */
void sound_step(void);                                /* 0x1A0, chamado a cada tick */

/* ---------------------------------------------------------------- vídeo (video.c) */
/* Buffer de vídeo = RAM 0x100–0x1FF (256 nibbles): 64 colunas x 16 linhas.
 * vram[i]: nibble i; coluna x da metade de cima ocupa vram[2x] (linhas 0-3) e vram[2x+1]
 * (linhas 4-7); a metade de baixo fica em +0x80. O LCD mostra uma janela de 32 colunas
 * começando no deslocamento g.view (0x02A/0x02B). Endereços de desenho são índices de
 * nibble de 8 bits (com "wrap" como no original). */
extern u8 vram[256];
extern u8 lcd[16][32];            /* o que está no LCD agora (1 = aceso) */
extern u8 lcd_icons;              /* bit i = ícone i aceso */

void vram_clear(void);                    /* 0x040 clear_page_0x100 (zera tudo, view=0) */
void vram_clear_range(u8 at, int n);      /* zera n nibbles a partir de at */
u8 draw_rom_bitmap(u8 at, u16 rom_addr);  /* copia a tabela lbpx/retd para a vram; devolve at final */
u8 draw_asset(u8 at, u8 page, u8 idx);    /* 0x000 render_asset: ROM 0x1000+0x100*page+0x10*idx */
u8 draw_tile(u8 at, u8 tile);             /* 0x7A5 misc_render: tile 8x8 (0x1600+8n); >=64 apaga */
void lcd_present(void);                   /* 0x056 copy_video_buf_to_vram */
void set_menu_icon(u8 icon);              /* 0x7C0: 0 nenhum, 1..8 */
void write_icons(void);                   /* 0x7E1 */
void view_to_right(void);                 /* 0x0CE label_30: view = 0x40, cursor de desenho = 0x40 */
void scroll_view_left(u8 step);           /* 0x500 */
void scroll_view_right(u8 limit_hi, u8 step); /* 0xAF0 */
void render_tile_list(u8 at, const u8 *tiles); /* 0x792: 8 tiles (2 faixas de 4) */

/* ---------------------------------------------------------------- estado do jogo */
/* Parâmetros por espécie (bloco de 13 bytes da ROM 0x1564+, carregado em RAM 0x230). */
struct species_params {
	u8 wake_hour, bed_hour;            /* 0x230, 0x232 (hora binária; FF = n/a) */
	u8 hunger_interval, happy_interval;/* 0x234, 0x236: minutos por 1/4 de coração */
	u16 sick_reload;                   /* 0x238-0x23A (12 bits) */
	u8 medicine_doses;                 /* 0x23B (?) */
	u8 weight_min, weight_max;         /* 0x23C, 0x23E (BCD) */
	u16 stage_minutes;                 /* 0x240-0x242 (12 bits) */
	u8 call_reload;                    /* 0x243 (recarga de q213) */
	u8 game_win_chance;                /* 0x244 (?) nibble baixo do byte 10 */
	u8 game_param_245;                 /* 0x245 */
	u8 weight_start;                   /* 0x246 (?) */
	u8 flag_248, param_249;            /* 0x248, 0x249 */
	u8 raw[26];                        /* nibbles 0x230..0x249 */
};

struct game {
	/* relógio (0x010-0x015): segundos e minutos em BCD, hora em binário 0..23 */
	u8 sec, min, hour;
	u8 clock_running;      /* 0x03C: !=0 => relógio anda */
	u8 clock_dirty;        /* 0x02E: 1 a cada segundo; zerado ao redesenhar */
	u8 seconds_counter_lo; /* 0x02F: contador livre de segundos (só p/ bateria) */
	u8 timeout_s;          /* 0x057: segundos restantes (−1/s) */

	/* entrada (0x026-0x029, 0x03D-0x03F) */
	u8 btn_down;           /* 0x026: segurados agora */
	u8 btn_edges;          /* 0x027: apertos acumulados desde a última leitura */
	u8 btn_held;           /* 0x028: snapshot de 0x026 após read_buttons */
	u8 btn_pressed;        /* 0x029: snapshot de 0x027 após read_buttons */
	u8 rep_last, rep_count, rep_mask; /* 0x03D, 0x03E, 0x03F: auto-repeat */
	u8 delay;              /* 0x022/0x023: contador de wait_ticks */
	u8 rng;                /* 0x05A: contador livre 4 bits (+1 por tick) */

	/* som (0x032-0x039, 0x058/0x059, 0x07B) */
	u8 snd_dur, snd_freq, snd_env, snd_ptr, snd_rep_lo, snd_rep_hi, snd_hlp;
	u8 sound_on;           /* 0x07B */

	/* vídeo (0x02A-0x02D, 0x03A/0x03B) */
	u8 view;               /* 0x02A/0x02B: deslocamento da janela */
	u16 icon_mask;         /* 0x02C/0x02D */
	u8 draw_at;            /* 0x03A/0x03B: cursor de desenho (nibble na vram) */

	/* pet (página 0) */
	u8 hunger, happy;      /* 0x040, 0x041: 1/4 de coração (4 = 1 coração), 0..F */
	u8 care_mistakes;      /* 0x042 */
	u8 discipline;         /* 0x043 */
	u8 weight;             /* 0x046/0x047 (BCD) */
	u8 sick;               /* 0x048: bit3 = doente */
	u8 sick_count;         /* 0x049 */
	u8 sleep;              /* 0x04A: bit3 = dormindo, bits 0-2 = fase do "Z" */
	u8 light;              /* 0x04B: 0 = luz apagada */
	u8 m4c;                /* 0x04C (?) */
	u8 poop;               /* 0x04D */
	u8 lifespan_over;      /* 0x04E */
	u8 lifespan_strikes;   /* 0x04F */
	u8 stage;              /* 0x050: ramo de evolução (F = forma final) */
	u8 discipline_misses;  /* 0x051 */
	u8 age;                /* 0x054/0x055 (BCD, dias) */
	u8 event;              /* 0x05C: evento pendente */
	u8 species;            /* 0x05D: 0 ovo, 1 Babytchi, 2 Marutchi, ... */
	u8 anim_set;           /* 0x05E */
	u8 food_choice;        /* 0x073 */
	u8 skippable;          /* 0x074: botão interrompe animação */
	u8 cursor, cursor_limit, timeout_reload; /* 0x075-0x077 */
	u8 saved_cursor78;     /* 0x078 */
	u8 egg;                /* 0x07C: 5..1 contagem do ovo, 0 = chocar, F = chocado */
	u8 frame_counter;      /* 0x07E */
	u8 menu_saved;         /* 0x0A0 */
	u8 m80;                /* 0x080: temporário de telas */

	/* motor de animação do pet (0x05F-0x070) */
	u8 pose_timer;         /* 0x05F */
	u8 pose_flags;         /* 0x063 (0x062 = delay) */
	u8 pose_ptr;           /* 0x064/0x065 (página B) */
	u8 move_count;         /* 0x066/0x067 */
	u8 move_arg;           /* 0x068/0x069 */
	u8 move_ptr;           /* 0x06A/0x06B (página 9) */
	u8 pet_pos;            /* 0x06C/0x06D */
	u8 sprite_a, sprite_b; /* 0x06E, 0x06F */
	u8 move_flags;         /* 0x070 */

	/* contadores por minuto (página 2) */
	u8 hunger_timer;       /* 0x200/0x201 */
	u8 happy_timer;        /* 0x202/0x203 */
	u8 sleep_minutes;      /* 0x204/0x205 */
	u8 poop_timer;         /* 0x206/0x207: minutos até o próximo cocô */
	u8 call_hunger;        /* 0x208 */
	u8 call_discipline;    /* 0x209 */
	u16 empty_heart_min;   /* 0x20A-0x20C */
	u16 sick_timer;        /* 0x20D-0x20F */
	u16 stage_timer;       /* 0x210-0x212 */
	u8 call_q213, call_q214; /* 0x213, 0x214 */
	u8 nap;                /* 0x215 (Babytchi) */
	u8 baby_awake;         /* 0x216/0x217 (Babytchi) */
	struct species_params p; /* 0x230-0x249 */
	u8 clock_backup[3];    /* 0x272-0x277 */

	/* --- campos adicionados por menu_care.c --- */
	/* --- campos adicionados por menu_info.c --- */
	/* --- campos adicionados por life.c --- */
};
extern struct game g;

/* nibble i (0 = baixo) de um valor */
#define NIB(v, i) (((v) >> (4 * (i))) & 0xF)

/* ---------------------------------------------------------------- jogo */
void game_main(void);           /* 0x110 reset: não retorna */
void game_resume(void);         /* continua um jogo salvo (não existe no original) */

/* anim.c */
void reset_pet_animation(void); /* 0x465 */
void step_pet_animation(void);  /* 0x486 */
void draw_pet_overlays(void);   /* 0x400 */
void render_pet_frame(void);    /* 0x052: step + overlays + present */
void select_pet_anim_set(void); /* 0x596 */
void draw_pet_half(u8 frame, u8 flip, bool lower); /* 0x928 / 0x939 */

/* screens.c */
enum screen { SCR_BOOT_WAIT, SCR_SET_CLOCK, SCR_CLOCK, SCR_PET };
enum screen boot_wait_for_start(void); /* 0x97F */
enum screen clock_screen(void);        /* 0x293 */
enum screen pet_screen(void);          /* 0x508 */
void soft_reset(void);                 /* 0x206: novo ovo mantendo o relógio */
u8 draw_clock(void);            /* 0x5A6 */

/* events.c */
void minute_tick(void);         /* 0xC7A */
bool check_events(void);        /* 0xF00 */
void dispatch_event(void);      /* 0x307 */
void init_new_game_state(void); /* 0x761 */
void load_species_params(void); /* 0xDD2 (parte) */
void hatch_egg(void);           /* 0xFEC */

/* menu_care.c: comida, luz, remédio, banheiro, disciplina */
void menu_food(void);           /* 0x710 */
void menu_light(void);          /* 0xABE */
void menu_medicine(void);       /* 0x600 */
void menu_toilet(void);         /* 0xD7E */
void menu_discipline(void);     /* 0xADA */
u8 menu_choose_option(const u8 *tiles); /* 0xE5A (assinatura pode mudar) */

/* menu_info.c: status e jogo */
void menu_status(void);         /* 0x814 */
void menu_game(void);           /* 0x624 */

/* life.c: evolução, morte, reset, teste de fábrica, dissolve */
void ev_evolve(void);           /* 0x34B */
void ev_death(void);            /* 0x374 */
void factory_lcd_test(void);    /* 0x256 */
void screen_dissolve(void);     /* 0x906 */

void play_overlay_anim(u8 anim_set, u8 seconds, u8 script); /* 0x8A9 (anim.c) */

/* menus.c */
void menu_init(u8 limit, u8 timeout); /* 0x1EC */
int menu_poll(void);            /* 0x2C3: devolve MENU_* */
enum { MENU_NONE, MENU_MOVED, MENU_CONFIRM, MENU_CANCEL, MENU_TIMEOUT };
void run_menu_item(u8 cursor);  /* 0x700 */

/* front-end (front_*.c) */
void front_pump(double now_s);  /* entradas e tempo real */
void front_lcd_changed(void);   /* chamado quando o LCD muda */
void front_set_buzzer(bool on, u8 freq); /* saída do buzzer */
extern u8 front_buttons;          /* botões fisicamente apertados (bits BTN_*) */
#endif
