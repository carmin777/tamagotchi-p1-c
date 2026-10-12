/* Icon menu (0x2C3) and the actions of each icon (table 0x700). */
#include "tama.h"

/* 0x1EC label_66: sets up cursor, limit and timeout, and discards pending presses */
void menu_init(u8 limit, u8 timeout)
{
	g.cursor = 0;
	g.cursor_limit = limit;
	g.timeout_reload = timeout;
	g.timeout_s = timeout;
	cost_ms(1.4);
	read_buttons();
}

/* 0x2C3 label_94: L advances the cursor, M confirms, R cancels; timeout cancels */
int menu_poll(void)
{
	cost_ms(3.7);
	read_buttons();
	if (((g.btn_held | g.btn_pressed) & 7) && g.timeout_reload) g.timeout_s = g.timeout_reload;
	if (g.btn_pressed & BTN_L) {
		play_beep();
		if (++g.cursor >= g.cursor_limit) g.cursor = 0;
		return MENU_MOVED;
	}
	if (g.btn_pressed & BTN_M) { play_beep(); return MENU_CONFIRM; }
	if (g.btn_pressed & BTN_R) { play_beep(); return MENU_CANCEL; }
	if (g.timeout_s != 0 || g.timeout_reload == 0) return MENU_NONE;
	return MENU_TIMEOUT;
}

/* 0x700: 1 food, 2 light, 3 game, 4 medicine, 5 toilet, 6 status, 7 discipline */
void run_menu_item(u8 cursor)
{
	switch (cursor) {
	case 1: menu_food(); break;
	case 2: menu_light(); break;
	case 3: menu_game(); break;
	case 4: menu_medicine(); break;
	case 5: menu_toilet(); break;
	case 6: menu_status(); break;
	case 7: menu_discipline(); break;
	}
}
