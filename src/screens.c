/* Partida, acerto do relógio, tela do relógio e tela do pet. */
#include <string.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "tama.h"

void prog_timer_start(void);
jmp_buf soft_reset_point;

/* 0x5E3 hour24_to_12h: tabela 0x15E6 (BCD; bit3 do nibble alto = PM). Hora inválida => 0 */
static u8 hour_12h(u8 hour) { return hour < 24 ? rom_byte(0x15E6 + hour) : 0; }

/* 0x5F3 / 0x5EF: dígitos grande e pequeno (página 0x15, 4 colunas cada) */
static u8 glyph_big(u8 at, u8 d) { return draw_rom_bitmap(at, 0x1500 + 4 * d); }
static u8 glyph_small(u8 at, u8 d) { return draw_rom_bitmap(at, 0x1500 + 4 * (d + 10)); }

static u8 put(u8 at, u8 nib) { vram[at] = nib; return at + 1; }

/* 0x5A6: hh:mm ss na vram a partir de g.draw_at (normalmente 0xC4). Devolve os segundos
 * do retrato (M[4]/M[5]), que a tela do relógio usa para a barra */
u8 draw_clock(void)
{
	u8 sec, min, h12, at;

	cost_ms(3);
	g.clock_dirty = 0;
	sec = g.sec; min = g.min;              /* retrato atômico (interrupções desligadas) */
	h12 = hour_12h(g.hour);
	cost_ms(14);

	at = draw_tile(g.draw_at, 0xA + ((h12 >> 7) & 1));   /* "AM"/"PM" vertical */
	vram_clear_range(at, 16); at += 16;
	at += 0x60;                                             /* metade de cima */
	if (NIB(h12, 1) & 3) at = glyph_big(at, NIB(h12, 1) & 3);
	else { vram_clear_range(at, 8); at += 8; }
	at = put(at, 0); at = put(at, 0);
	at = glyph_big(at, NIB(h12, 0));
	at = put(at, 0); at = put(at, 0);                       /* ':' */
	at = put(at, 2); at = put(at, 2);
	at = put(at, 0); at = put(at, 0);
	at = glyph_big(at, NIB(min, 1));
	at = put(at, 0); at = put(at, 0);
	at = glyph_big(at, NIB(min, 0));
	at = put(at, 0); at = put(at, 0); at = put(at, 0); at = put(at, 0);
	at = glyph_small(at, NIB(sec, 1));
	at = put(at, 0); at = put(at, 0);
	glyph_small(at, NIB(sec, 0));
	return sec;
}

/* 0x591 */
static void draw_clock_screen(void)
{
	view_to_right();
	g.draw_at = 0xC4;
	draw_clock();
	lcd_present();
}

/* ---------------------------------------------------------------- partida */

/* 0x11E0 set_init_mem_and_int: zera a RAM e põe os valores de fábrica */
static void init_mem_and_int(void)
{
	memset(&g, 0, sizeof(g));
	memset(vram, 0, sizeof(vram));
	g.hour = 0x20;                 /* >= 24: relógio não acertado */
	g.snd_ptr = SND_IDLE;
	g.snd_rep_lo = g.snd_rep_hi = 0xF;
	g.sound_on = 0xF;
	g.clock_running = 0;
	g.skippable = 0;
	g.egg = 5;                     /* ovo: 5 minutos */
	prog_timer_start();
}

static enum screen set_clock_screen(void);

/* Telas ligadas por "jp" no original: cada uma devolve a próxima */
static enum screen run_screen(enum screen s)
{
	switch (s) {
	case SCR_BOOT_WAIT: return boot_wait_for_start();
	case SCR_SET_CLOCK: return set_clock_screen();
	case SCR_CLOCK:     return clock_screen();
	case SCR_PET:       return pet_screen();
	}
	return SCR_BOOT_WAIT;
}

/* 0x110 reset -> 0x22A */
void game_main(void)
{
	enum screen s;

	cost_ms(30);
	init_mem_and_int();
	cost_ms(60);
	play_sound(SND_BOOT, 0, 0xF);
	/* TODO 0x256: teste de LCD de fábrica (L segurado no reset) */
	s = setjmp(soft_reset_point) ? SCR_PET : SCR_BOOT_WAIT;
	for (;;) s = run_screen(s);
}

/* 0x97F: ovo parado esperando o botão M para acertar o relógio */
enum screen boot_wait_for_start(void)
{
	init_new_game_state();
	reset_pet_animation();
	set_menu_icon(0);
	for (;;) {
		vram_clear();
		render_pet_frame();
		read_buttons();
		if (g.btn_pressed & BTN_M) return SCR_SET_CLOCK;
	}
}

/* 0x553: acerto do relógio. L = +1 h, M = +1 min, R = confirma */
static enum screen set_clock_screen(void)
{
	play_beep();
	set_menu_icon(0);
	vram_clear();
	draw_tile(draw_tile(0xE0, 0x0C), 0x0D);   /* "SET" */
	g.clock_running = 0;
	g.sec = 0;
	g.rep_mask = BTN_M | BTN_L;               /* M e L repetem se segurados */
	draw_clock_screen();
	do read_buttons(); while (g.btn_held);    /* espera soltar tudo */
	for (;;) {
		draw_clock_screen();
		for (;;) {
			read_buttons();
			if (g.btn_pressed & BTN_R) {
				play_beep();
				g.rep_mask = 0;
				if (g.hour >= 0x20) return SCR_BOOT_WAIT;
				g.clock_running = 0xF;
				return SCR_CLOCK;
			}
			if (g.btn_pressed & BTN_M) {      /* +1 minuto (sem levar para a hora) */
				play_beep();
				g.min = ((g.min & 0xF) == 9) ? (g.min & 0xF0) + 0x10 : g.min + 1;
				if (g.min >= 0x60) g.min = 0;
				g.clock_dirty = 0xF;
			}
			if (g.btn_pressed & BTN_L) {      /* +1 hora */
				u8 h = g.hour & 0x1F;
				play_beep();
				g.hour = (h + 1 >= 24) ? 0 : h + 1;
				break;
			}
			if (g.clock_dirty) break;
		}
	}
}

/* ---------------------------------------------------------------- relógio */

/* 0x293: tela do relógio. M = vai ao pet, L+R = acertar */
enum screen clock_screen(void)
{
	for (;;) {
		set_menu_icon(0);
		vram_clear();
		read_buttons();
		for (;;) {
			u8 s, i;
			view_to_right();
			g.draw_at = 0xC4;
			s = NIB(draw_clock(), 0);
			/* barra de 5 setas que avança com os segundos */
			if (s >= 6) s += 3;
			for (i = 0, g.draw_at = 0xE2; i < 5; i++) {
				s = (s - 1) & 0xF;
				g.draw_at = glyph_small(g.draw_at, 0xA | ((s >> 3) & 1));
			}
			lcd_present();
			if (check_events()) {
				dispatch_event();
				break;
			}
			for (;;) {
				wait_ticks(2);
				read_buttons();
				if (g.btn_pressed & BTN_M) return SCR_PET;
				if (g.btn_held == (BTN_L | BTN_R)) return SCR_SET_CLOCK;
				if (g.egg == 0) return SCR_PET;
				if (g.clock_dirty) break;
			}
		}
	}
}

/* ---------------------------------------------------------------- pet */

/* 0x4E7: botão M sem ícone: rola para o relógio */
static enum screen enter_clock_screen_from_pet(void)
{
	vram_clear_range(0x40, 64);
	vram_clear_range(0xC0, 64);
	g.draw_at = 0xC4;
	draw_clock();
	vram_clear_range(0x70, 16);               /* sem os segundos durante a rolagem */
	scroll_view_right(4, 4);
	return SCR_CLOCK;
}

/* 0x508: monta a tela do pet e rola até ela; depois o laço do menu (0x512) */
enum screen pet_screen(void)
{
	play_beep();
	set_menu_icon(0);
	view_to_right();
	g.draw_at = 0x40;
	select_pet_anim_set();
	step_pet_animation();
	draw_pet_overlays();
	scroll_view_left(0xC);

	for (;;) {
		if (g.egg == 0) hatch_egg();
		g.menu_saved = 0;
menu:
		menu_init(g.egg == 0xF ? 8 : 1, 0xA);
		g.cursor = g.menu_saved;
		set_menu_icon(g.menu_saved);
		for (;;) {
			int r;
			cost_ms(13);                                 /* instruções do próprio laço */
			if (g.egg == 0) break;
			if (check_events()) {
				dispatch_event();
				g.menu_saved = 0;
				goto menu;
			}
			r = menu_poll();
			if (g.btn_held == (BTN_L | BTN_R) && (g.btn_pressed & (BTN_L | BTN_R))) {
				g.sound_on ^= 0xF;                       /* 0x54D: liga/desliga o som */
				g.snd_dur = 0; g.snd_ptr = SND_TOGGLE;
				g.snd_rep_lo = g.cursor; g.snd_rep_hi = 0xF;
				g.menu_saved = 0;
				goto menu;
			}
			if (r == MENU_CANCEL || r == MENU_TIMEOUT) { g.menu_saved = 0; goto menu; }
			if (r == MENU_CONFIRM) {
				g.menu_saved = g.cursor;
				if (g.cursor == 0) return enter_clock_screen_from_pet();
				run_menu_item(g.cursor);
				goto menu;
			}
			set_menu_icon(g.cursor);
			vram_clear();
			select_pet_anim_set();
			render_pet_frame();
			if (getenv("TAMA_DEBUG")) {
				extern double cpu_total_ms; extern unsigned long isr_count;
				static double t0, c0; static unsigned long i0;
				fprintf(stderr, "iter wall=%.1f cpu=%.1f isr=%lu\n", (machine_now() - t0) * 1000, cpu_total_ms - c0, isr_count - i0);
				t0 = machine_now(); c0 = cpu_total_ms; i0 = isr_count;
			}
		}
	}
}
