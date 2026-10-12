/* Information icons: status (0x814) and the guessing game (0x624). */
#include "tama.h"

/* ---------------------------------------------------------------- 0x814 status */

/* 0x867 label_258: frame of the discipline meter in the lower strip (vram 0xC2..0xFF)
 * and g.discipline−1 filled segments (at most 14) */
static void draw_discipline_gauge(void)
{
	u8 at = 0xC2, n;
	int i;

	cost_ms(18.8);
	vram[at++] = 0xD; vram[at++] = 0x3;                       /* left border */
	for (i = 0; i < 14; i++) { vram[at++] = 2; vram[at++] = 4; }
	vram[at++] = 0x3; vram[at++] = 0x4;                       /* middle joint (0xE0) */
	for (i = 0; i < 14; i++) { vram[at++] = 2; vram[at++] = 4; }
	vram[at++] = 0xD; vram[at++] = 0x3;                       /* right border */

	n = g.discipline ? g.discipline : 1;
	for (at = 0xC6; --n; at += 4) {                           /* segment = 2 filled columns */
		cost_ms(1.1);
		vram[at] = 0xA; vram[at + 1] = 0x5;
	}
}

/* 0x88C label_266: 4 hearts (0x1C full / 0x1D empty) for hunger/happiness v (0..F, in
 * 1/4 hearts): 0-2 → 0, 3-6 → 1, 7-10 → 2, 11-14 → 3, F → 4 */
static void hearts_tiles(u8 *t, u8 v)
{
	u8 n = (v >= 0xF) ? 4 : ((v + 1) >> 2) & 3;
	int i;

	for (i = 0; i < 4; i++) {
		t[i] = n ? 0x1C : 0x1D;
		if (n) n--;
	}
}

/* 0x836 label_253: the 8 tiles of page g.m80 (0x080); returns whether the page has the
 * discipline meter (0x081) */
static bool status_build_page(u8 *t)
{
	cost_ms(4.5);
	switch (g.m80) {
	case 0:                                                   /* 0x843: age and weight */
		t[0] = 0x18;                                          /* baby */
		t[1] = NIB(g.age, 1) ? NIB(g.age, 1) : 0xF0;          /* tens digit (0 = blank) */
		t[2] = NIB(g.age, 0);
		t[3] = 0x19;                                          /* "yr" */
		t[4] = 0x1A;                                          /* scale */
		t[5] = NIB(g.weight, 1) ? NIB(g.weight, 1) : 0xF0;
		t[6] = NIB(g.weight, 0);
		t[7] = 0x1B;                                          /* "oz" */
		cost_ms(1.7);
		return false;
	case 1:                                                   /* 0x85C: "Discipline" + meter */
		t[0] = 0x34; t[1] = 0x35; t[2] = 0x36; t[3] = 0x37;
		t[4] = t[5] = t[6] = t[7] = 0xFF;
		return true;
	case 2:                                                   /* 0x883: "Hungry" */
		t[0] = 0x2E; t[1] = 0x2F; t[2] = 0x30; t[3] = 0xFF;
		hearts_tiles(t + 4, g.hunger);
		cost_ms(5.1);
		return false;
	default:                                                  /* 0x889 (0xBF8): "Happy" */
		t[0] = 0x31; t[1] = 0x32; t[2] = 0x33; t[3] = 0xFF;
		hearts_tiles(t + 4, g.happy);
		cost_ms(5.5);
		return false;
	}
}

/* 0x0DE label_34: draws the 8 tiles on the right page of the vram and points the window at it */
static void draw_tile_page(const u8 *t)
{
	cost_ms(2);
	view_to_right();
	g.draw_at = 0x40;
	render_tile_list(0x40, t);
}

/* 0x814 label_247: 4 pages (age/weight, discipline, hunger, happiness). L or M advance,
 * R or 15 s without a button exit; a pending event also exits (on turning the page). */
void menu_status(void)
{
	g.m80 = 0;
	for (;;) {
		u8 t[8];
		bool gauge = status_build_page(t);

		draw_tile_page(t);
		if (gauge) draw_discipline_gauge();
		lcd_present();
		read_buttons();
		g.timeout_s = 15;
		for (;;) {
			read_buttons();
			if (g.timeout_s == 0 || (g.btn_pressed & BTN_R)) goto out;
			if (g.btn_pressed & (BTN_M | BTN_L)) break;
		}
		play_beep();
		g.m80 = (g.m80 + 1 < 4) ? g.m80 + 1 : 0;
		if (check_events()) break;
	}
out:
	play_beep();                                             /* 0x835 -> 0x51A: back to the menu */
}

/* ---------------------------------------------------------------- 0x624 game */

/* 0x900 label_282: draw using the free-running counter 0x05A (+1 per prog timer tick):
 * adds chance+1 and yields a carry with probability (chance+1)/16 */
static bool rand_chance(u8 chance)
{
	unsigned s;

	if (chance == 0) return false;
	s = g.rng + chance + 1;
	g.rng = s & 0xF;
	return s > 0xF;
}

/* 0xCE5 label_331 (add_heart), here only for happiness */
static void add_happy_heart(void)
{
	cost_ms(2);
	g.happy = (g.happy + 4 > 0xF) ? 0xF : g.happy + 4;
	if (g.hunger && g.happy) g.call_hunger = 0;
}

/* 0x6E5 label_214 (same as the one in events.c) */
static void clamp_weight(void)
{
	cost_ms(4);
	if (g.weight < g.p.weight_min) g.weight = g.p.weight_min;
	else if (g.weight >= g.p.weight_max) g.weight = g.p.weight_max;
}

/* 0x6D4 label_211: guess arrow (L = ◄ left, M = ► right) */
static void draw_guess_arrow(void)
{
	cost_ms(1.5);
	if (g.game_guess == 0) return;
	if (g.game_guess & BTN_L) draw_tile(0x80, 0x26);
	else draw_tile(0xB0, 0x27);
}

/* 0x6C8 / 0x6CD label_208 / label_209: fanfare and happy / sad pet */
static void win_feedback(void)  { play_sound(0x83, 0, 0xF); play_overlay_anim(7, 5, 6); }
static void lose_feedback(void) { play_sound(0x9E, 0, 0xF); play_overlay_anim(8, 5, 3); }

/* 0x064C label_198: one round. Returns false if the player left (R or timeout) */
static bool game_round(void)
{
	/* the result is drawn before the guess; the guess only decides which side the pet looks at */
	cost_ms(2.5);
	g.game_guess = 0;
	g.game_wait = g.p.raw[0x16];                             /* 0x246: frames after the guess (win) */
	g.game_result = 8;
	if (!rand_chance(g.p.raw[0x15])) {                       /* 0x245: chance of winning */
		g.game_wait = g.p.raw[0x17];                         /* 0x247: ... (loss) */
		g.game_result = 0;
	}
	cost_ms(1);
	g.anim_set = 5;
	reset_pet_animation();
	read_buttons();
	g.timeout_s = 15;
	for (;;) {                                               /* 0x667 label_200 */
		play_sound(0x97, g.btn_held, 0xF);                   /* "waiting for a guess" */
		vram_clear();
		step_pet_animation();
		draw_guess_arrow();
		draw_pet_overlays();
		lcd_present();
		read_buttons();
		cost_ms(1);
		if ((g.btn_pressed & BTN_R) || g.timeout_s == 0) return false;
		if (g.game_guess == 0) {
			if (!(g.btn_pressed & (BTN_M | BTN_L))) continue;
			g.game_guess = g.btn_pressed;
		}
		if (--g.game_wait == 0) break;                       /* 0x67A label_201 */
	}
	/* resolution: pet facing the drawn side */
	play_beep();
	vram_clear();
	g.draw_at = 0x10;
	{
		u8 flip = g.game_result ^ ((g.game_guess & BTN_L) ? 8 : 0);
		draw_pet_half(0, flip, false);                       /* 0x3EE label_128 */
		g.draw_at += 0x80;
		draw_pet_half(0, flip, true);
	}
	draw_guess_arrow();
	draw_pet_overlays();
	lcd_present();
	wait_ticks(0x29);
	if (g.game_result) { g.game_wins++; win_feedback(); }
	else lose_feedback();
	return true;
}

/* 0x624 label_196: "which way will it look?" — 5 rounds; 3+ correct = +1 happiness
 * heart; each game costs 1 oz. Plays again until R or a pending event. */
void menu_game(void)
{
	if (g.sleep & 8) return;                                 /* sleeping: nothing (0x625 -> 0x51A) */
	for (;;) {                                               /* 0x626 label_197 */
		cost_ms(1.5);
		if ((g.sick & 8) || g.call_discipline) {             /* sick or asking for discipline: refuses */
			play_overlay_anim(6, 5, 0);                      /* 0x61E label_194 */
			return;                                          /* -> 0x51A (cursor kept) */
		}
		g.anim_set = 5;
		reset_pet_animation();
		vram_clear();
		step_pet_animation();
		draw_pet_overlays();
		{
			static const u8 hearts[8] = { 0xFF, 0x1C, 0xFF, 0x1C, 0x1D, 0xFF, 0x1D, 0xFF };
			play_sound(0xD6, 0, 0xF);                        /* opening */
			draw_tile_page(hearts);
		}
		lcd_present();
		wait_ticks(0x0D);
		scroll_view_left(0xC);                               /* 0x500: scrolls from the hearts screen to the pet */
		g.game_rounds = 5;
		g.game_wins = 0;
		do {
			if (!game_round()) goto out;
			cost_ms(0.5);
		} while (--g.game_rounds);

		/* result: "pet ♡  hits VS misses" (0xCF5 init_result_tiles) */
		{
			u8 t[8] = { 0x18, 0xFF, 0x1D, 0xFF, 0x00, 0x0F, 0x00, 0xFF };
			cost_ms(2);
			t[4] = g.game_wins;
			t[6] = 5 - g.game_wins;
			play_sound(0xE5, 0, 0xF);
			draw_tile_page(t);
		}
		lcd_present();
		wait_ticks(0x52);
		cost_ms(0.5);
		if (g.game_wins >= 3) { add_happy_heart(); win_feedback(); }
		else lose_feedback();

		/* 0x6B6 label_206: −1 oz (BCD), within the species' limits */
		g.weight = (g.weight & 0xF) ? g.weight - 1 : (g.weight ? g.weight - 0x07 : 0x99);
		clamp_weight();
		if (g.btn_pressed & BTN_R) goto out;
		if (check_events()) goto out;
	}
out:
	play_beep();
	g.menu_saved = 0;                                        /* 0x6C7 -> 0x517 label_159 */
}
