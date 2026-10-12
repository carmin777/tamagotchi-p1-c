/* Life cycle: evolution (0x34B), death and tombstone (0x374), reset keeping the clock
 * (0x206), factory LCD test (0x256) and screen dissolve (0x906). */
#include <string.h>
#include <setjmp.h>
#include "tama.h"

extern jmp_buf soft_reset_point;   /* screens.c: back to the pet loop (0x517) */
void prog_timer_start(void);       /* machine.c */
void irq_disable(void), irq_enable(void);

/* ---------------------------------------------------------------- dissolve */

/* 0x913 or_dither_pattern: ORs a mask (table 0x800, index 0..7) into the whole vram,
 * in groups of 4 nibbles {lo, lo, hi, hi}. The 8 steps together light every pixel. */
static void or_dither_pattern(u8 step)
{
	u8 mask = rom_byte(0x800 + step), lo = mask & 0xF, hi = mask >> 4;
	int i;

	for (i = 0; i < 256; i += 4) {
		vram[i] |= lo; vram[i + 1] |= lo;
		vram[i + 2] |= hi; vram[i + 3] |= hi;
	}
	cost_ms(130);
}

/* 0x906 copy_video_buf_8x: darkens the screen in 8 steps (1 tick each) and clears the vram */
void screen_dissolve(void)
{
	u8 step;

	for (step = 0; step < 8; step++) {
		or_dither_pattern(step);
		lcd_present();
		wait_ticks(1);
	}
	vram_clear();
}

/* ---------------------------------------------------------------- evolution */

/* 0x89E label_271: "no glyph" animation (script 0) in the current animation set */
static void play_plain_anim(u8 seconds)
{
	select_pet_anim_set();
	play_overlay_anim(g.anim_set, seconds, 0);
}

/* 0x34B event 5: the pet evolves. The current stage's rules (0xD9C–0xDC5, loaded into
 * RAM 0x090 by 0xDC6) are entries {min_care_mistakes, min_discipline_misses,
 * new stage, new species}; the first one that matches wins. */
void ev_evolve(void)
{
	u8 rules[16], e;
	u16 jp;

	play_sound(SND_EVOLVE, 3, 0);
	screen_dissolve();
	play_plain_anim(3);
	screen_dissolve();

	jp = rom_word(0xDCB + g.stage);              /* 0xDC6 load_evolution_rules: jpba */
	rom_table(0xD00 | (jp & 0xFF), rules, sizeof(rules));
	cost_ms(3.5);
	for (e = 0;; e++) {
		u8 min_care = rules[e] & 0xF, min_disc = rules[e] >> 4;
		if (g.care_mistakes >= min_care && g.discipline_misses >= min_disc) break;
		e++;
	}
	/* and points at the byte with the criteria; the next one has {stage, species} */
	g.stage = rules[e + 1] & 0xF;
	g.species = rules[e + 1] >> 4;

	reset_pet_animation();
	play_plain_anim(4);
	load_species_params();                       /* 0xDD2 */
}

/* ---------------------------------------------------------------- reset keeping the clock */

/* 0x206 soft_reset_keep_clock (L+R held on the tombstone): new egg, same time.
 * Does not return: continues in the pet menu loop (0x517). */
void soft_reset(void)
{
	u8 sec = g.sec, min = g.min, hour = g.hour;  /* 0x20C: 0x010-0x015 -> 0x272-0x277 */

	irq_disable();                               /* 0x206 clear_0x07D: rst f */
	cost_ms(4);
	/* 0x11E0 set_init_mem_and_int: 16 nibbles per iteration of 0x047 => clears 0x000-0x0DF,
	 * 0x100-0x1FF, 0x200-0x26F and all the display RAM (pixels and icons) */
	memset(&g, 0, sizeof(g));
	memset(vram, 0, sizeof(vram));
	memset(lcd, 0, sizeof(lcd));
	lcd_icons = 0;
	g.clock_backup[0] = sec; g.clock_backup[1] = min; g.clock_backup[2] = hour;
	/* 0x1200: factory values */
	g.hour = 0x20;
	g.snd_ptr = SND_IDLE;
	g.snd_rep_lo = g.snd_rep_hi = 0xF;
	g.sound_on = 0xF;
	g.clock_running = 0;
	g.skippable = 0;
	g.egg = 5;
	cost_ms(67);                                 /* like init_mem_and_int in screens.c */
	prog_timer_start();                          /* F78: 2 (stops) and then 1 => new phase */
	cost_ms(8);
	g.sec = sec; g.min = min; g.hour = hour;     /* 0x218: restores the clock */
	irq_enable();                                /* 0x219 set_f_0x07D */
	play_sound(SND_BOOT, 0, 0xF);
	init_new_game_state();                       /* 0x761 */
	reset_pet_animation();                       /* 0x465 */
	g.clock_running = 0xF;
	do read_buttons(); while (g.btn_held & 7);   /* espera soltar tudo */
	longjmp(soft_reset_point, 1);                /* SP = 0xFF; jp 0x517 */
}

/* ---------------------------------------------------------------- death */

/* 0x200 poll_cancel_or_reset: presents, reads buttons; L+R held = soft reset.
 * Returns true if R was pressed. */
static bool poll_cancel_or_reset(void)
{
	lcd_present();
	read_buttons();
	if (g.btn_held == (BTN_L | BTN_R)) soft_reset();
	return g.btn_pressed & BTN_R;
}

/* 0x3EE label_128: whole pet (upper half at draw_at, lower +0x80) */
static void draw_pet_pose(u8 pose, u8 flip)
{
	cost_ms(15);                                 /* 58 ms measured, minus that of draw_pet_half */
	draw_pet_half(pose, flip, false);
	g.draw_at += 0x80;
	draw_pet_half(pose, flip, true);
}

/* 0x3E4 label_126: the angel (species 0, pose 0) at 0x20 */
static void draw_angel(void)
{
	g.species = 0;
	g.draw_at = 0x20;
	draw_pet_pose(0, 0);
	lcd_present();
}

/* 0x3D7 label_125: the dead pet (pose 4 of table 0xA00, sprite b) at 0x00, and the angel */
static void draw_dying_pet(u8 species)
{
	vram_clear();
	g.species = species;
	g.draw_at = 0x00;
	draw_pet_pose(rom_byte(0xA00 + (species << 4) + 4) >> 4, 0);
	draw_angel();
}

/* 0x374 event 6: death, (angel if it died of old age) and the tombstone */
void ev_death(void)
{
	u8 dead_species;
	int i;

	set_menu_icon(0);
	screen_dissolve();
	g.skippable = 0;                             /* non-skippable animations */
	g.sick = 0; g.sick_count = 0;
	g.sleep = 0; g.light = 0xF;
	g.m4c = 0; g.poop = 0;
	dead_species = g.species;

	play_sound(SND_DEATH1, dead_species, 0xF);
	for (i = 0; i < 4; i++) play_overlay_anim(0xA, 0xA, 5);   /* 0x3D2 */
	play_sound(SND_DEATH2, 0, 0xF);
	play_overlay_anim(0xA, 0xA, 5);
	play_sound(SND_DEATH3, 0, 0xF);
	play_overlay_anim(0xA, 0xA, 5);
	g.species = 0;

	if (g.lifespan_over) {                       /* old age: the angel */
		draw_dying_pet(dead_species);
		wait_ticks(0x1B);
		g.delay = 0x48;
		do {
			draw_dying_pet(dead_species);
			vram_clear();
			draw_angel();
		} while (g.delay);
		wait_ticks(0x1B);
	}

	play_sound(SND_GRAVE, 0, 0xF);
	read_buttons();
	for (;;) {                                   /* tombstone (0x3A1) */
		vram_clear();
		g.m80 = (g.m80 + 8) & 0xF;               /* toggles the mirroring */
		g.draw_at = 0x00;
		draw_pet_pose(3, g.m80);
		g.draw_at = 0x20;
		draw_pet_pose(4, 0);
		if (!poll_cancel_or_reset()) continue;
		/* R: shows the age */
		play_beep();
		draw_tile(0xD0, 0x19);                   /* age glyph */
		draw_tile(0x50, g.age & 0xF);
		if (g.age >> 4) draw_tile(0x40, g.age >> 4);
		scroll_view_right(2, 2);
		while (!poll_cancel_or_reset());
		play_beep();
		scroll_view_left(0xE);
	}
}

/* ---------------------------------------------------------------- factory test */

/* 0x26B label_81 / 0x278 label_83: shows the pattern and the icons and waits for exactly
 * the `btn` button to be held */
static void lcd_test_show(u8 btn)
{
	write_icons();                               /* 0x7E1 */
	lcd_present();
	do read_buttons(); while (g.btn_held != btn);
	play_beep();
}

static void lcd_test_fill(int icons, u8 a, u8 b, u8 btn)
{
	int i;

	if (icons >= 0) g.icon_mask = icons;
	for (i = 0; i < 256; i += 4) {
		vram[i] = vram[i + 1] = a;
		vram[i + 2] = vram[i + 3] = b;
	}
	cost_ms(78);
	lcd_test_show(btn);
}

/* 0x256: L held at reset. Four patterns, each advanced by a button (M, R, L, M) */
void factory_lcd_test(void)
{
	int i;

	/* in the 1st step XP is still 2 (from 0x23C): "0xFF" goes to 0x22C, not to the icons */
	lcd_test_fill(-1, 0xF, 0xF, BTN_M);          /* everything lit */
	lcd_test_fill(0x5A, 0xA, 0x5, BTN_R);        /* checkerboard */
	lcd_test_fill(0xA5, 0x5, 0xA, BTN_L);        /* inverted checkerboard */
	g.icon_mask = 0;                             /* 0x281 label_85: frame */
	for (i = 0; i < 0x80; i += 2) { vram[i] = 1; vram[i + 1] = 0; }
	for (i = 0x80; i < 0x100; i += 2) { vram[i] = 0; vram[i + 1] = 8; }
	vram[0x00] = vram[0x01] = vram[0x3E] = vram[0x3F] = 0xF;
	vram[0x80] = vram[0x81] = vram[0xBE] = vram[0xBF] = 0xF;
	cost_ms(95);
	lcd_test_show(BTN_M);
}
