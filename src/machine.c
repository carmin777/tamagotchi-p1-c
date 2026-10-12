/*
 * "Hardware": time, the two interrupts and button reading.
 *
 * Time: the original runs at 32768 Hz and the pace of the screens is that of the CPU itself
 * (one turn of the pet screen loop takes ~0.45 s). Here the game code declares the CPU cost of
 * each routine with cost_ms(); the interrupts steal their own CPU time (the prog timer one
 * spends ~12 ms out of every 27 ms!). Costs measured on the reference emulator with
 * `harness/trace -P` (wall time x 0.53 = pure time, interrupts discounted).
 *
 * Interrupts (as on the P1's E0C6S46):
 *  - 1 Hz clock timer (0x11D) at t = 1, 2, 3 ... s;
 *  - prog timer (0x16F): 256 Hz / 7, free-running => a tick every 7/256 s (27.34 ms; 36.57/s).
 *    (The 1 Hz ISR writes F78=1, but that only keeps it on: it does not restart the phase.)
 */
#include <stdio.h>
#include <stdlib.h>
#include "tama.h"

#define PROG_PERIOD (7.0 / 256.0)
#define PROG_ISR_BASE 0.00894     /* CPU of the prog timer ISR, excluding the sound sequencer */
#define CLOCK_ISR_COST 0.0075
#define MINUTE_TICK_COST 0.0060

struct game g;

static double now;          /* emulated time (s) */
double cpu_total_ms;        /* debugging: CPU consumed by the main code */
unsigned long isr_count;
static double next_clock;   /* next 1 Hz interrupt */
static double next_prog;    /* next prog timer tick */

double machine_now(void) { return now; }

void machine_reset(void)
{
	now = 0;
	next_clock = 1.0;
	next_prog = 1e30;        /* the prog timer only starts when the game turns it on (init) */
}

void prog_timer_start(void) { next_prog = now + PROG_PERIOD; }

/* ---- save / load (only the SDL front end uses them) ---- */
#include <stdio.h>
#include <string.h>

#define SAVE_MAGIC "TAMAP1R"
#define SAVE_VERSION 1

bool machine_save(const char *path)
{
	FILE *f = fopen(path, "wb");
	int v = SAVE_VERSION * 100000 + (int) sizeof(g);   /* changes if the struct changes */
	bool ok;

	if (!f) return false;
	ok = fwrite(SAVE_MAGIC, 8, 1, f) == 1 && fwrite(&v, sizeof(v), 1, f) == 1 &&
	     fwrite(&g, sizeof(g), 1, f) == 1 && fwrite(vram, sizeof(vram), 1, f) == 1 &&
	     fwrite(&now, sizeof(now), 1, f) == 1 && fwrite(&next_clock, sizeof(next_clock), 1, f) == 1 &&
	     fwrite(&next_prog, sizeof(next_prog), 1, f) == 1;
	return fclose(f) == 0 && ok;
}

bool machine_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	char magic[8];
	int v;
	bool ok;

	if (!f) return false;
	ok = fread(magic, 8, 1, f) == 1 && !memcmp(magic, SAVE_MAGIC, 8) &&
	     fread(&v, sizeof(v), 1, f) == 1 && v == SAVE_VERSION * 100000 + (int) sizeof(g) &&
	     fread(&g, sizeof(g), 1, f) == 1 && fread(vram, sizeof(vram), 1, f) == 1 &&
	     fread(&now, sizeof(now), 1, f) == 1 && fread(&next_clock, sizeof(next_clock), 1, f) == 1 &&
	     fread(&next_prog, sizeof(next_prog), 1, f) == 1;
	fclose(f);
	return ok;
}

/* ---- 0x16F prog_timer_int: sound, random, wait, buttons. Returns the CPU cost (s) ---- */
static double prog_timer_int(void)
{
	u8 down, prev;
	double snd = sound_step() / 1000.0;   /* 0x1A0 configure_buzzer comes first */

	front_pump(now + snd);                /* the buttons (0x17C) are read after it */
	g.rng = (g.rng + 1) & 0xF;            /* 0x05A */
	if (g.delay) g.delay--;               /* 0x022/0x023 */

	down = front_buttons & 7;             /* active-low buttons on port K0 */
	prev = g.btn_down;
	g.btn_down = down;
	g.btn_edges |= (prev ^ down) & down;  /* presses (rising edges) */

	/* auto-repeat: same state for 16 ticks, then every 8 */
	if (g.rep_last != down) {
		g.rep_last = down;
		g.rep_count = 0;
	} else if (++g.rep_count == 16) {
		g.rep_count = 8;
		g.btn_edges |= down & g.rep_mask;
	}
	g.rep_count &= 0xF;
	return PROG_ISR_BASE + snd;
}

/* ---- 0x11D clock_timer_int: clock and minute tick ---- */
static void bcd_inc(u8 *v) { *v = ((*v & 0xF) == 9) ? (*v & 0xF0) + 0x10 : *v + 1; }

static void clock_timer_int(void)
{
	if (g.timeout_s) g.timeout_s--;
	if (g.clock_running) {
		g.clock_dirty = 1;
		bcd_inc(&g.sec);
		if (g.sec >= 0x60) {
			g.sec = 0;
			bcd_inc(&g.min);                    /* label_36: BCD minutes, wraps at 60 */
			if (g.min >= 0x60) {
				g.min = 0;
				g.hour = (g.hour + 1 >= 24) ? 0 : g.hour + 1;   /* label_67 */
			}
			minute_tick();                      /* 0xC7A */
			now += MINUTE_TICK_COST;
		}
	}
	g.seconds_counter_lo++;
}

/* Runs the interrupts that are due (each one consumes its CPU time). An interrupt
 * that falls due during another waits for it to finish; if both are pending, the prog timer
 * one has priority (as on the E0C6S46/TamaLIB). */
static void service_interrupts(void)
{
	for (;;) {
		bool prog_due = next_prog <= now, clock_due = next_clock <= now;
		if (!prog_due && !clock_due) return;
		front_pump(now);
		if (!prog_due) {
			next_clock += 1.0;
			clock_timer_int();
			now += CLOCK_ISR_COST;
		} else {
			next_prog += PROG_PERIOD;
			isr_count++;
			now += prog_timer_int();
		}
	}
}

/* CPU flag I (rst f / set f / ei): when off, due interrupts stay pending
 * until irq_enable(). Only the soft reset (0x206-0x219, life.c) uses it. */
static bool irq_off;
void irq_disable(void) { irq_off = true; }
void irq_enable(void) { irq_off = false; service_interrupts(); }

/* The main code needs `ms` of CPU; the interrupts interleave */
void cost_ms(double ms)
{
	double left = ms / 1000.0;

	cpu_total_ms += ms;
	if (irq_off) { now += left; front_pump(now); return; }

	for (;;) {
		double next = next_clock < next_prog ? next_clock : next_prog;
		if (now + left < next) { now += left; break; }
		left -= next > now ? next - now : 0;
		if (next > now) now = next;
		service_interrupts();
	}
	front_pump(now);
}

/* 0x03C: loads the counter and waits for the prog timer to zero it */
void wait_ticks(u8 n)
{
	g.delay = n;
	while (g.delay) cost_ms(1.0);
}

/* 0x021 copy_0x026_7_to_8_9 */
void read_buttons(void)
{
	cost_ms(7.4);
	g.btn_held = g.btn_down;
	g.btn_pressed = g.btn_edges;
	if (g.btn_pressed && getenv("TAMA_TRACE")) fprintf(stderr, "%.3f 029=%X\n", now, g.btn_pressed);
	g.btn_edges = 0;
}
