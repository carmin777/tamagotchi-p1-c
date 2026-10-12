/* Video buffer, sprites/tiles from the ROM, LCD and icons. */
#include <string.h>
#include "tama.h"

u8 vram[256];
u8 lcd[16][32];
u8 lcd_icons;

void vram_clear(void)                 /* 0x040 */
{
	cost_ms(28.0);
	memset(vram, 0, sizeof(vram));
	g.view = 0;
}

void vram_clear_range(u8 at, int n)
{
	while (n--) vram[at++] = 0;
}

u8 draw_rom_bitmap(u8 at, u16 rom_addr)
{
	u8 bytes[64];
	int i, n = rom_table(rom_addr, bytes, sizeof(bytes));

	cost_ms(10.0 + 0.5 * n);
	for (i = 0; i < n; i++) {
		vram[at++] = bytes[i] & 0xF;
		vram[at++] = bytes[i] >> 4;
	}
	return at;
}

u8 draw_asset(u8 at, u8 page, u8 idx) { return draw_rom_bitmap(at, 0x1000 + 0x100 * page + 0x10 * idx); }

u8 draw_tile(u8 at, u8 tile)          /* 0x7A5 misc_render */
{
	if (tile >= 64) {                 /* 0x7B8 clear_16_addrs */
		cost_ms(3.7);
		vram_clear_range(at, 16);
		return at + 16;
	}
	return draw_rom_bitmap(at, 0x1600 + 8 * tile);
}

/* 0x792 render_tile_list: 8 tiles, 4 in the upper strip and 4 in the lower one (+0x40 nibbles) */
void render_tile_list(u8 at, const u8 *tiles)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (i == 4) at += 0x40;
		at = draw_tile(at, tiles[i]);
	}
}

/* 0x056: copies the 32-column window (starting at g.view) to the LCD */
void lcd_present(void)
{
	int x, y;

	cost_ms(56.2);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 32; x++) {
			u8 i = g.view + 2 * x + ((y >> 2) & 1) + (y >= 8 ? 0x80 : 0);
			lcd[y][x] = (vram[i] >> (y & 3)) & 1;
		}
	front_lcd_changed();
}

/* 0x7E1 write_icons */
void write_icons(void)
{
	lcd_icons = g.icon_mask & 0xFF;
	front_lcd_changed();
}

/* 0x7C0 set_menu_icon: lights the cursor icon (1..8) and the "attention" one */
void set_menu_icon(u8 icon)
{
	cost_ms(10.4);
	g.icon_mask = icon ? 1 << (icon - 1) : 0;
	if (!(g.sleep & 8)) {
		if (g.call_hunger || g.call_discipline) g.icon_mask |= 0x80;
	} else {
		if (NIB(g.sleep_minutes, 1) < 4) g.icon_mask |= 0x80;
	}
	write_icons();
}

void view_to_right(void) { g.view = 0x40; }   /* 0x0CE label_30 */

/* 0x500: scrolls the window to the left, in steps of (16-step) nibbles, until it passes 0 */
void scroll_view_left(u8 step)
{
	unsigned lo, hi;

	do {
		lcd_present();
		cost_ms(4);
		lo = (g.view & 0xF) + step;
		hi = (g.view >> 4) + 0xF + (lo >> 4);
		g.view = ((hi & 0xF) << 4) | (lo & 0xF);
	} while (hi > 15);
}

/* 0xAF0 blit_buffer_slide: scrolls to the right from 0 until the high nibble reaches limit_hi */
void scroll_view_right(u8 limit_hi, u8 step)
{
	g.view = 0;
	for (;;) {
		lcd_present();
		if ((g.view >> 4) >= limit_hi) return;
		cost_ms(2.6);
		g.view += step;
	}
}
