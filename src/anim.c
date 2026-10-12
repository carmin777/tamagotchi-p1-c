/*
 * Pet animation engine (0x465 / 0x486) and pet screen overlays (0x400).
 *
 * Each (species, animation set) points to two scripts in the ROM:
 *  - POSE script (page B): bytes {flags<<4 | delay}; chooses which of the two sprites
 *    (sprite_a / sprite_b) goes in each half and whether it is mirrored;
 *  - MOVEMENT script (page 9): {flags<<4 | delay, arg}; arg with bit0 = relative
 *    offset, otherwise absolute position (in vram nibbles).
 * Entries with one extra byte are jumps (the extra byte is the new pointer).
 */
#include "tama.h"

/* 0x465 */
void reset_pet_animation(void)
{
	u8 idx = (g.species << 4) | g.anim_set;
	u8 sprites = rom_byte(0xA00 + idx);    /* pose->sprites table */
	u8 scripts = rom_byte(0xB00 + idx);    /* lo = pose script, hi = movement script */

	cost_ms(8.1);
	g.pose_timer = 0;
	g.move_count = 0;
	g.sprite_a = sprites & 0xF;
	g.sprite_b = sprites >> 4;
	g.move_ptr = rom_byte(0x98B + (scripts >> 4));
	g.pose_ptr = rom_byte(0xBC0 + (scripts & 0xF));
}

/* 0x928 / 0x939: upper half (table C) or lower half (table D) of the pet at g.draw_at */
void draw_pet_half(u8 frame, u8 flip, bool lower)
{
	u16 table = lower ? 0xD00 : 0xC00;
	u8 base = rom_byte(table + g.species);
	u8 id = rom_byte(table + (u8) (base + frame));
	u8 sprite;

	cost_ms(3.5);
	id ^= (flip & 8) << 4;                 /* bit7 = mirror */
	sprite = (id & 0x7F) + 3;
	if (sprite >= 0x80) return;            /* 0x7F = empty half */
	draw_rom_bitmap(g.draw_at, 0x1000 + 0x10 * sprite);
	if (id & 0x80) {                       /* mirrors the 16 columns */
		u8 x = g.draw_at, y = g.draw_at + 0x1E, t;
		int i;
		cost_ms(10);
		for (i = 0; i < 8; i++, x += 2, y -= 2) {
			t = vram[x]; vram[x] = vram[y]; vram[y] = t;
			t = vram[(u8) (x + 1)]; vram[(u8) (x + 1)] = vram[(u8) (y + 1)]; vram[(u8) (y + 1)] = t;
		}
	}
}

/* 0x486: one frame of the pet */
void step_pet_animation(void)
{
	u8 b[3];
	int n;

	cost_ms(14);
	/* pose script */
	if (g.pose_timer == 0) {
		u8 p = g.pose_ptr++;
		n = rom_table(0xB00 + p, b, 2);
		g.pose_timer = b[0] & 0xF;
		g.pose_flags = b[0] >> 4;
		if (n >= 2) g.pose_ptr = b[1];
	} else {
		g.pose_timer--;
	}
	/* movement script */
	if (g.move_count == 0) {
		u8 p = g.move_ptr;
		g.move_ptr = p + 2;
		n = rom_table(0x900 + p, b, 3);
		g.move_count = b[0] & 0xF;
		g.move_flags = b[0] >> 4;
		g.move_arg = b[1];
		if (n >= 3) g.move_ptr = b[2];
	} else {
		g.move_count--;
	}
	/* position (every frame) */
	if (g.move_arg & 1) g.pet_pos += g.move_arg & 0xFE;
	else g.pet_pos = g.move_arg;
	g.draw_at = g.pet_pos;

	if (g.move_flags & 8) {
		u8 frame = (g.pose_flags & 2) ? g.sprite_b : g.sprite_a;
		u8 flip = ((g.move_flags << 2) ^ g.pose_flags) & 8;
		draw_pet_half(frame, flip, false);
	}
	g.draw_at += 0x80;
	if (g.move_flags & 4) {
		u8 frame = (g.pose_flags & 1) ? g.sprite_b : g.sprite_a;
		u8 flip = (((g.move_flags << 2) ^ g.pose_flags) << 1) & 8;
		draw_pet_half(frame, flip, true);
	}
}

/* 0x400: light off, sleep Z, skull and poops (on the left page of the vram) */
void draw_pet_overlays(void)
{
	static const u8 poop_at[8] = { 0xB0, 0x30, 0xA0, 0x20, 0x90, 0x10, 0x80, 0x00 };
	u8 n, tile;
	int i;

	cost_ms(9.4);
	if (g.light == 0) {
		cost_ms(36);                      /* paint 128 nibbles black (measured: ~61 ms with the Z) */
		for (i = 0; i < 0x40; i++) vram[i] = vram[0x80 + i] = 0xF;
		if (g.sleep & 8) {
			g.sleep = ((g.sleep + 1) | 8) & 0xF;
			draw_tile(0x20, 0x20 + ((g.sleep >> 1) & 1));   /* "Z" on a black background */
		}
		return;
	}
	if (g.sleep & 8) {
		g.sleep = ((g.sleep + 1) | 8) & 0xF;
		draw_tile(0x30, 0x3A + ((g.sleep >> 1) & 1));       /* "Z" on a light background */
	} else if (g.sick & 8) {
		draw_tile(0x30, 0x22);                              /* skull */
	}

	g.frame_counter = (g.frame_counter + 1) & 0xF;
	tile = 0x10 + 0xE + ((g.frame_counter >> 1) & 1);      /* poop (2 frames) */
	n = g.poop > 8 ? 8 : g.poop;
	if (n >= 3) {                                           /* pushes the pet to the left */
		u8 k = ((n - 3) >> 1) + 1;
		for (i = 0; i < 0x40; i++) {
			vram[i] = vram[(u8) (i + 16 * k)];
			vram[0x80 + i] = vram[(u8) (0x80 + i + 16 * k)];
		}
	}
	for (i = 0; i < n; i++) draw_tile(poop_at[i], tile);
}

/* 0x052 label_13 */
void render_pet_frame(void)
{
	step_pet_animation();
	draw_pet_overlays();
	lcd_present();
}

/* 0x596 */
void select_pet_anim_set(void)
{
	u8 set = (g.sleep & 8) ? 3 : (g.sick & 8) ? 9 : 1;

	cost_ms(3.8);
	if (g.anim_set != set) {
		g.anim_set = set;
		reset_pet_animation();
	}
}

/*
 * 0x8A9 label_273: plays an animation with an overlaid glyph (food, Z, skull, sparkle...)
 * while the pet animates in the anim_set set. Lasts up to `seconds` seconds, or until a button
 * if g.skippable. Scripts in page E (index at 0xE00): steps {hold<<6 | glyph, position}.
 */
void play_overlay_anim(u8 anim_set, u8 seconds, u8 script)
{
	u8 ptr, glyph = 0x3F, pos = 0x50, hold = 0;

	g.anim_set = anim_set;
	g.timeout_s = seconds;
	ptr = rom_byte(0xE00 + script);
	reset_pet_animation();
	read_buttons();
	for (;;) {
		vram_clear();
		step_pet_animation();
		cost_ms(30);
		if (hold == 0) {
			u8 b[3];
			int n = rom_table(0xE00 + ptr, b, 3);
			ptr += 2;
			if (n >= 3) ptr = b[2];
			glyph = b[0] & 0x3F;
			hold = (b[0] >> 6) & 3;
			pos = b[1];
		} else {
			hold--;
		}
		if (glyph != 0x3F) {
			u8 xl = pos & 0xF, xh = pos >> 4;
			if (g.species == 1) {               /* Babytchi: repositions the glyph */
				unsigned c;
				if (xh) xh |= 8;
				c = (xl + 8) >> 4; xl = (xl + 8) & 0xF;
				xh = (xh + 0xF + c) & 0xF;
				if ((xh | 8) == 0xF) xh = (xh + 1) & 0xF;
			}
			draw_tile((xh << 4) | xl, glyph);
		}
		draw_pet_overlays();
		lcd_present();
		read_buttons();
		if (g.timeout_s == 0) return;
		if (g.skippable && (g.btn_pressed & 7)) { play_beep(); return; }
	}
}
