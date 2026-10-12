/* Care icons: food (0x710), light (0xABE), medicine (0x600), toilet (0xD7E),
 * discipline (0xADA) and the 2-option selector (0xE5A).
 *
 * Handler exits (as in the original):
 *  - label_160 (0x51A): back to the menu with the same icon lit -> just return;
 *  - label_159 (0x517): clears M[0x0A0] (deselects the icon) -> g.menu_saved = 0.
 */
#include "tama.h"

/* Overlay animations (play_overlay_anim: pet animation set, seconds, script) */
enum {
	ANIM_EAT = 2,         /* pet eating */
	ANIM_REFUSE = 6,      /* shakes its head */
	ANIM_HAPPY = 7,
	ANIM_SCOLDED = 8,     /* grimace (medicine, scolding) */
};

/* 0x6E5 label_214: keeps the weight within the species' [min, max) (copy of the one in events.c) */
static void clamp_weight(void)
{
	if (g.weight < g.p.weight_min) g.weight = g.p.weight_min;
	else if (g.weight >= g.p.weight_max) g.weight = g.p.weight_max;
}

/* 0x756 label_230: weight += n (BCD, saturates at 99) and clamps to the species' range */
static void add_weight(u8 n)
{
	unsigned lo = (g.weight & 0xF) + n, hi = g.weight >> 4;

	if (lo > 9) { lo -= 10; hi++; }         /* add mx,a / adc mx,0 with the D flag */
	g.weight = hi > 9 ? 0x99 : (hi << 4) | lo;
	clamp_weight();
}

/* 0xCE5 label_331: +1 heart (4 quarters, saturates at F). With both meters
 * non-empty, the hunger/happiness call (0x208) is answered. */
static void add_heart(u8 *meter)
{
	*meter = (*meter + 4 > 0xF) ? 0xF : *meter + 4;
	if (g.hunger != 0 && g.happy != 0) g.call_hunger = 0;
}

/*
 * 0xE5A label_352: 2-option menu (arrow 0x27 on the upper or lower row).
 * tiles = the screen's 8 tiles (2 strips of 4); the cursor starts at g.saved_cursor78 (0x078).
 * L toggles, M confirms (returns 0/1), R cancels (CHOICE_CANCEL); timeout or a pending
 * event return CHOICE_IDLE. On exit, 0x078 = cursor (0 if it was an event).
 */
int menu_choose_option(const u8 *template, u8 timeout)
{
	u8 tiles[8];
	int i, r;

	for (i = 0; i < 8; i++) tiles[i] = template[i];
	menu_init(2, timeout);
	view_to_right();
	g.draw_at = 0x40;
	g.cursor = g.saved_cursor78;
	for (;;) {
		r = menu_poll();                          /* 0x2C3 label_94 */
		if (r == MENU_CONFIRM || r == MENU_CANCEL || r == MENU_TIMEOUT) {
			g.saved_cursor78 = g.cursor;
			return r == MENU_CONFIRM ? g.cursor : r == MENU_CANCEL ? CHOICE_CANCEL : CHOICE_IDLE;
		}
		cost_ms(1.0);
		tiles[0] = tiles[4] = 0xFF;               /* 0x90 / 0x98: erases the arrow */
		tiles[g.cursor ? 4 : 0] = 0x27;
		render_tile_list(g.draw_at, tiles);       /* 0x792 */
		lcd_present();
		if (check_events()) {                     /* 0xF00: an event interrupts the menu */
			g.saved_cursor78 = 0;
			return CHOICE_IDLE;
		}
	}
}

/* ---------------------------------------------------------------- food */

/* 0x710 label_220: "meal" / "snack"; repeats until R, timeout or event */
void menu_food(void)
{
	u8 tiles[8];

	if (g.sleep & 8) return;                      /* sleeping: nothing */
	for (;;) {
		u8 script;
		int c;

		g.saved_cursor78 = g.food_choice;          /* cursor starts at the last choice */
		rom_table(0xBE8, tiles, 8);                /* label_308: labels 0x28-0x2A / 0x2B-0x2D */
		c = menu_choose_option(tiles, 0xA);
		if (c == CHOICE_IDLE) { g.menu_saved = 0; return; }
		if (c == CHOICE_CANCEL) return;
		g.food_choice = c;                         /* 0x073 */
		if (c == 0) {                              /* meal */
			if ((g.sick & 8) || g.call_discipline || g.hunger == 0xF) {
				play_overlay_anim(ANIM_REFUSE, 5, 0xC);   /* sick, naughty or full */
				continue;
			}
			add_heart(&g.hunger);
			add_weight(1);
			script = 8;
		} else {                                   /* snack */
			add_heart(&g.happy);
			if (!(g.sick & 8) && g.sick_timer) g.sick_timer--;   /* 0xCC8 label_324 */
			add_weight(2);
			script = 0xA;
		}
		if (g.p.flag_248 & 1) script++;            /* animation variant (0x248 bit0) */
		play_overlay_anim(ANIM_EAT, 5, script);
	}
}

/* ---------------------------------------------------------------- light */

/* 0xABE label_297: "ON" / "OFF". Turning the light off sets 0x205 = F (no care mistake
 * pending for the light left on) */
void menu_light(void)
{
	u8 tiles[8];
	int c;

	g.saved_cursor78 = (g.light & 1) ^ 1;          /* cursor on the current option */
	rom_table(0xBF0, tiles, 8);                    /* label_309 */
	c = menu_choose_option(tiles, 0xA);
	if (c == CHOICE_IDLE) { g.menu_saved = 0; return; }
	if (c == CHOICE_CANCEL) return;
	if (c == 0) {
		g.light = 0xF;
	} else {
		g.light = 0;
		g.sleep_minutes |= 0xF0;
	}
}

/* ---------------------------------------------------------------- medicine */

/* 0x600 label_191: each dose adds M[0x23B] to the sickness nibble; cured when bit3 drops */
void menu_medicine(void)
{
	if (g.sleep & 8) return;
	if (!(g.sick & 8)) {                           /* label_194: refusal */
		play_overlay_anim(ANIM_REFUSE, 5, 0);
		return;
	}
	g.sick &= 7;
	play_overlay_anim(ANIM_SCOLDED, 5, 3);
	g.sick = ((g.sick | 8) + g.p.medicine_doses) & 0xF;
	if (!(g.sick & 8)) g.sick_timer = g.p.sick_reload;   /* cured: reloads 0x20D */
}

/* ---------------------------------------------------------------- toilet */

/* 0xD7E label_336: draws the "wave" (ROM 0xD94) just right of the screen and scrolls the window
 * to it; if there was poop, cleans it and the pet gets happy */
void menu_toilet(void)
{
	if (g.sleep & 8) return;
	draw_rom_bitmap(0x40, 0xD94);                  /* label_338, upper half */
	draw_rom_bitmap(0xC0, 0xD94);                  /* ... and lower */
	scroll_view_right(4, 4);                       /* 0xAF0 */
	if (g.poop == 0) return;
	g.poop = 0;
	play_overlay_anim(ANIM_HAPPY, 5, 6);
}

/* ---------------------------------------------------------------- discipline */

/* 0xADA label_301: only has an effect if there is a discipline call (0x209) */
void menu_discipline(void)
{
	if (g.sleep & 8) return;
	if (g.call_discipline) {
		g.call_discipline = 0;
		g.discipline = g.discipline + 4 > 0xF ? 0xF : g.discipline + 4;
		play_sound(0xF0, 0, 0xF);
	}
	play_overlay_anim(ANIM_SCOLDED, 5, 3);
}
