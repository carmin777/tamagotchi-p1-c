/* Ícones de cuidado: comida (0x710), luz (0xABE), remédio (0x600), banheiro (0xD7E),
 * disciplina (0xADA) e o seletor de 2 opções (0xE5A).
 *
 * Saídas dos tratadores (como no original):
 *  - label_160 (0x51A): volta ao menu com o mesmo ícone aceso -> só retornar;
 *  - label_159 (0x517): zera M[0x0A0] (deseleciona o ícone) -> g.menu_saved = 0.
 */
#include "tama.h"

/* Animações sobrepostas (play_overlay_anim: conjunto de animação do pet, segundos, script) */
enum {
	ANIM_EAT = 2,         /* pet comendo */
	ANIM_REFUSE = 6,      /* balança a cabeça */
	ANIM_HAPPY = 7,
	ANIM_SCOLDED = 8,     /* careta (remédio, bronca) */
};

/* 0x6E5 label_214: mantém o peso dentro de [mín, máx) da espécie (cópia da de events.c) */
static void clamp_weight(void)
{
	if (g.weight < g.p.weight_min) g.weight = g.p.weight_min;
	else if (g.weight >= g.p.weight_max) g.weight = g.p.weight_max;
}

/* 0x756 label_230: peso += n (BCD, satura em 99) e limita à faixa da espécie */
static void add_weight(u8 n)
{
	unsigned lo = (g.weight & 0xF) + n, hi = g.weight >> 4;

	if (lo > 9) { lo -= 10; hi++; }         /* add mx,a / adc mx,0 com a flag D */
	g.weight = hi > 9 ? 0x99 : (hi << 4) | lo;
	clamp_weight();
}

/* 0xCE5 label_331: +1 coração (4 quartos, satura em F). Com os dois medidores
 * não vazios, o chamado de fome/felicidade (0x208) é atendido. */
static void add_heart(u8 *meter)
{
	*meter = (*meter + 4 > 0xF) ? 0xF : *meter + 4;
	if (g.hunger != 0 && g.happy != 0) g.call_hunger = 0;
}

/*
 * 0xE5A label_352: menu de 2 opções (seta 0x27 na linha de cima ou na de baixo).
 * tiles = os 8 tiles da tela (2 faixas de 4); o cursor parte de g.saved_cursor78 (0x078).
 * L alterna, M confirma (devolve 0/1), R cancela (CHOICE_CANCEL); timeout ou evento
 * pendente devolvem CHOICE_IDLE. Ao sair, 0x078 = cursor (0 se foi um evento).
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
		tiles[0] = tiles[4] = 0xFF;               /* 0x90 / 0x98: apaga a seta */
		tiles[g.cursor ? 4 : 0] = 0x27;
		render_tile_list(g.draw_at, tiles);       /* 0x792 */
		lcd_present();
		if (check_events()) {                     /* 0xF00: evento interrompe o menu */
			g.saved_cursor78 = 0;
			return CHOICE_IDLE;
		}
	}
}

/* ---------------------------------------------------------------- comida */

/* 0x710 label_220: "refeição" / "lanche"; repete até R, timeout ou evento */
void menu_food(void)
{
	u8 tiles[8];

	if (g.sleep & 8) return;                      /* dormindo: nada */
	for (;;) {
		u8 script;
		int c;

		g.saved_cursor78 = g.food_choice;          /* cursor parte da última escolha */
		rom_table(0xBE8, tiles, 8);                /* label_308: rótulos 0x28-0x2A / 0x2B-0x2D */
		c = menu_choose_option(tiles, 0xA);
		if (c == CHOICE_IDLE) { g.menu_saved = 0; return; }
		if (c == CHOICE_CANCEL) return;
		g.food_choice = c;                         /* 0x073 */
		if (c == 0) {                              /* refeição */
			if ((g.sick & 8) || g.call_discipline || g.hunger == 0xF) {
				play_overlay_anim(ANIM_REFUSE, 5, 0xC);   /* doente, malcriado ou cheio */
				continue;
			}
			add_heart(&g.hunger);
			add_weight(1);
			script = 8;
		} else {                                   /* lanche */
			add_heart(&g.happy);
			if (!(g.sick & 8) && g.sick_timer) g.sick_timer--;   /* 0xCC8 label_324 */
			add_weight(2);
			script = 0xA;
		}
		if (g.p.flag_248 & 1) script++;            /* variante da animação (0x248 bit0) */
		play_overlay_anim(ANIM_EAT, 5, script);
	}
}

/* ---------------------------------------------------------------- luz */

/* 0xABE label_297: "ON" / "OFF". Apagar a luz marca 0x205 = F (sem erro de cuidado
 * pendente pela luz acesa) */
void menu_light(void)
{
	u8 tiles[8];
	int c;

	g.saved_cursor78 = (g.light & 1) ^ 1;          /* cursor na opção atual */
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

/* ---------------------------------------------------------------- remédio */

/* 0x600 label_191: cada dose soma M[0x23B] ao nibble de doença; cura quando o bit3 cai */
void menu_medicine(void)
{
	if (g.sleep & 8) return;
	if (!(g.sick & 8)) {                           /* label_194: recusa */
		play_overlay_anim(ANIM_REFUSE, 5, 0);
		return;
	}
	g.sick &= 7;
	play_overlay_anim(ANIM_SCOLDED, 5, 3);
	g.sick = ((g.sick | 8) + g.p.medicine_doses) & 0xF;
	if (!(g.sick & 8)) g.sick_timer = g.p.sick_reload;   /* curado: recarrega 0x20D */
}

/* ---------------------------------------------------------------- banheiro */

/* 0xD7E label_336: desenha a "onda" (ROM 0xD94) logo à direita da tela e rola a janela
 * até ela; se havia cocô, limpa e o pet fica feliz */
void menu_toilet(void)
{
	if (g.sleep & 8) return;
	draw_rom_bitmap(0x40, 0xD94);                  /* label_338, metade de cima */
	draw_rom_bitmap(0xC0, 0xD94);                  /* ... e de baixo */
	scroll_view_right(4, 4);                       /* 0xAF0 */
	if (g.poop == 0) return;
	g.poop = 0;
	play_overlay_anim(ANIM_HAPPY, 5, 6);
}

/* ---------------------------------------------------------------- disciplina */

/* 0xADA label_301: só tem efeito se há chamado de disciplina (0x209) */
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
