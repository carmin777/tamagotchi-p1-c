/*
 * Sequenciador do buzzer (0x1A0 configure_buzzer), executado a cada tick do prog timer.
 *
 * Os efeitos sonoros são tabelas na página E da ROM (0xE00 + ptr). Cada passo é:
 *   lbpx dur ; retd (env<<4 | freq)            -> passo linear (o ponteiro segue +2)
 *   lbpx dur ; lbpx (env<<4 | freq) ; retd ptr -> passo com salto (laço ou fim)
 * freq com bit3 = silêncio. ptr 0x7D = ocioso (silêncio que aponta para si mesmo).
 */
#include "tama.h"

/* 0x0BF: dispara um som; repeat = nº de voltas nos saltos (hi=F => infinito) */
void play_sound(u8 ptr, u8 repeat_lo, u8 repeat_hi)
{
	if (!g.sound_on) { cost_ms(1.5); return; }
	cost_ms(9.0);
	g.snd_dur = 0;
	g.snd_ptr = ptr;
	g.snd_rep_lo = repeat_lo;
	g.snd_rep_hi = repeat_hi;
}

void play_beep(void) { play_sound(SND_BEEP, 0, 0xF); }  /* 0x0BB (A=0) */

/* Devolve o custo de CPU (ms) do caminho percorrido, que varia bastante (medido com -P):
 * contando um passo mudo 3,5; contando um tom 1,9; buscando o próximo passo ~7,4. */
double sound_step(void)
{
	u8 step[3];
	int n;

	if (g.snd_dur != 0) {
		g.snd_dur--;
		if (g.snd_freq & 8) { front_set_buzzer(false, 0); return 3.48; }
		return 1.92;
	}
	g.snd_dur--;                                  /* vira FF; será sobrescrito */
	for (;;) {
		u8 p = g.snd_ptr;
		g.snd_ptr = p + 2;
		n = rom_table(0xE00 + p, step, 3);
		g.snd_dur = step[0];
		g.snd_freq = step[1] & 0xF;
		g.snd_env = step[1] >> 4;
		if (n == 3) g.snd_ptr = step[2];
		if (n == 2 || g.snd_rep_hi == 0xF) break;
		{
			u8 cnt = g.snd_rep_lo | g.snd_rep_hi << 4;
			bool borrow = (cnt == 0);
			cnt--;
			g.snd_rep_lo = cnt & 0xF;
			g.snd_rep_hi = cnt >> 4;
			if (!borrow) break;
		}
		g.snd_ptr = SND_IDLE;                     /* acabaram as repetições */
	}
	front_set_buzzer(!(g.snd_freq & 8), g.snd_freq);
	return 7.4;
}
