/*
 * Buzzer sequencer (0x1A0 configure_buzzer), run on every prog timer tick.
 *
 * The sound effects are tables in page E of the ROM (0xE00 + ptr). Each step is:
 *   lbpx dur ; retd (env<<4 | freq)            -> linear step (the pointer advances +2)
 *   lbpx dur ; lbpx (env<<4 | freq) ; retd ptr -> step with a jump (loop or end)
 * freq with bit3 = silence. ptr 0x7D = idle (silence that points to itself).
 */
#include "tama.h"

/* 0x0BF: triggers a sound; repeat = number of loops on jumps (hi=F => infinite) */
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

/* Returns the CPU cost (ms) of the path taken, which varies a lot (measured with -P):
 * counting down a silent step 3.5; counting down a tone 1.9; fetching the next step ~7.4. */
double sound_step(void)
{
	u8 step[3];
	int n;

	if (g.snd_dur != 0) {
		g.snd_dur--;
		if (g.snd_freq & 8) { front_set_buzzer(false, 0); return 3.48; }
		return 1.92;
	}
	g.snd_dur--;                                  /* wraps to FF; will be overwritten */
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
		g.snd_ptr = SND_IDLE;                     /* the repetitions are over */
	}
	front_set_buzzer(!(g.snd_freq & 8), g.snd_freq);
	return 7.4;
}
