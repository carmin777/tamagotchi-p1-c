/*
 * "Hardware": tempo, as duas interrupções e a leitura de botões.
 *
 * Tempo: o original roda a 32768 Hz e o ritmo das telas é o do próprio CPU (uma volta
 * do laço da tela do pet leva ~0,45 s). Aqui o código do jogo declara o custo de CPU de
 * cada rotina com cost_ms(); as interrupções roubam o seu próprio tempo de CPU (a do
 * prog timer gasta ~12 ms a cada 27 ms!). Custos medidos no emulador de referência com
 * `harness/trace -P` (tempo de parede x 0,53 = tempo puro, descontadas as interrupções).
 *
 * Interrupções (como no E0C6S46 do P1):
 *  - clock timer 1 Hz (0x11D) em t = 1, 2, 3 ... s;
 *  - prog timer (0x16F): 256 Hz / 7, livre => tick a cada 7/256 s (27,34 ms; 36,57/s).
 *    (A ISR de 1 Hz escreve F78=1, mas isso só o mantém ligado: não reinicia a fase.)
 */
#include <stdio.h>
#include <stdlib.h>
#include "tama.h"

#define PROG_PERIOD (7.0 / 256.0)
#define PROG_ISR_BASE 0.00894     /* CPU da ISR do prog timer, fora o sequenciador de som */
#define CLOCK_ISR_COST 0.0075
#define MINUTE_TICK_COST 0.0060

struct game g;

static double now;          /* tempo emulado (s) */
double cpu_total_ms;        /* depuração: CPU consumida pelo código principal */
unsigned long isr_count;
static double next_clock;   /* próxima interrupção de 1 Hz */
static double next_prog;    /* próximo tick do prog timer */

double machine_now(void) { return now; }

void machine_reset(void)
{
	now = 0;
	next_clock = 1.0;
	next_prog = 1e30;        /* o prog timer só começa quando o jogo o liga (init) */
}

void prog_timer_start(void) { next_prog = now + PROG_PERIOD; }

/* ---- salvar / carregar (só o front-end SDL usa) ---- */
#include <stdio.h>
#include <string.h>

#define SAVE_MAGIC "TAMAP1R"
#define SAVE_VERSION 1

bool machine_save(const char *path)
{
	FILE *f = fopen(path, "wb");
	int v = SAVE_VERSION * 100000 + (int) sizeof(g);   /* muda se a struct mudar */
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

/* ---- 0x16F prog_timer_int: som, aleatório, espera, botões. Devolve o custo de CPU (s) ---- */
static double prog_timer_int(void)
{
	u8 down, prev;
	double snd = sound_step() / 1000.0;   /* 0x1A0 configure_buzzer vem primeiro */

	front_pump(now + snd);                /* os botões (0x17C) são lidos depois dele */
	g.rng = (g.rng + 1) & 0xF;            /* 0x05A */
	if (g.delay) g.delay--;               /* 0x022/0x023 */

	down = front_buttons & 7;             /* botões ativos-baixos na porta K0 */
	prev = g.btn_down;
	g.btn_down = down;
	g.btn_edges |= (prev ^ down) & down;  /* apertos (bordas de subida) */

	/* auto-repeat: mesmo estado por 16 ticks, depois a cada 8 */
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

/* ---- 0x11D clock_timer_int: relógio e tique de minuto ---- */
static void bcd_inc(u8 *v) { *v = ((*v & 0xF) == 9) ? (*v & 0xF0) + 0x10 : *v + 1; }

static void clock_timer_int(void)
{
	if (g.timeout_s) g.timeout_s--;
	if (g.clock_running) {
		g.clock_dirty = 1;
		bcd_inc(&g.sec);
		if (g.sec >= 0x60) {
			g.sec = 0;
			bcd_inc(&g.min);                    /* label_36: minutos BCD, volta em 60 */
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

/* Executa as interrupções vencidas (cada uma consome seu tempo de CPU). Uma interrupção
 * que vence durante outra espera ela terminar; se as duas estão pendentes, a do prog timer
 * tem prioridade (como no E0C6S46/TamaLIB). */
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

/* O código principal precisa de `ms` de CPU; as interrupções intercalam */
void cost_ms(double ms)
{
	double left = ms / 1000.0;

	cpu_total_ms += ms;

	for (;;) {
		double next = next_clock < next_prog ? next_clock : next_prog;
		if (now + left < next) { now += left; break; }
		left -= next > now ? next - now : 0;
		if (next > now) now = next;
		service_interrupts();
	}
	front_pump(now);
}

/* 0x03C: carrega o contador e espera o prog timer zerá-lo */
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
