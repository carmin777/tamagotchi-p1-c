/*
 * trace - emulador headless do Tamagotchi P1 para engenharia reversa.
 *
 * Roda o TamaLIB com tempo virtual (o mais rápido possível, determinístico),
 * input vindo de um script, e grava:
 *   frames.txt  - cada frame do LCD que mudou (32x16 + 8 ícones), com o tempo
 *   writes.tsv  - escritas na RAM (t_ms pc addr old new), só quando o valor muda
 *   cov.bin     - contagem de execuções por PC (u32 x 0x2000)
 *   ram.txt     - dumps de RAM pedidos pelo script (comando "dump")
 *   -C          - grava frames só ao fim de cada cópia p/ VRAM / escrita de ícones
 *   -P 0xADDR   - (repetível) perfil de tempo emulado por chamada à rotina em ADDR (até retornar)
 *
 * Script (uma linha por comando, '#' comenta):
 *   wait <seg>          avança o tempo emulado
 *   press <L|M|R>       aperta 0,15 s e solta, depois espera 0,35 s
 *   hold <L|M|R> <seg>  segura o botão pelo tempo dado
 *   frame               força a gravação do frame atual
 *   dump [rótulo]       grava a RAM 0x000-0x2FF em ram.txt
 *   mark <texto>        escreve um marcador em frames.txt e writes.tsv
 *   save <arquivo>      salva o estado (formato TamaTool)
 *   load <arquivo>      carrega um estado (formato TamaTool)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>

#include "tamalib/tamalib.h"
#include "state.h"

#define TS_FREQ 32768ULL     /* timestamps em ticks do oscilador (exato: sem arredondar ciclos) */

static timestamp_t now;
static u8_t lcd[16][32], icons[8];
static char last_frame[16 * 33 + 16];
static FILE *f_frames, *f_writes, *f_ram, *f_snd;
static u32_t snd_freq_dhz;
static int snd_on = -1, snd_logged_freq = -1;
static u32_t cov[0x2000];
static int log_writes = 1, all_frames = 0, commit_frames = 0;
/* -C: grava o frame só quando a cópia para a VRAM (0x056) ou a escrita de ícones (0x7E1)
 * termina — os mesmos pontos em que a reescrita apresenta um quadro */
static int commit_active[2];
static u32_t commit_depth[2];
static const u13_t commit_addr[2] = { 0x056, 0x7E1 };

/* Profiler: -P 0xADDR mede quanto tempo emulado cada chamada à rotina leva até retornar */
#define MAX_PROF 32
static struct { u13_t addr; int active; u32_t depth; timestamp_t t0, isr0; unsigned long n; double sum, min, max, pure; } prof[MAX_PROF];
static int nprof;
/* tempo acumulado dentro das ISRs (0x116 genérica, 0x11D clock, 0x16F prog) */
static timestamp_t isr_total, isr_t0;
static int isr_active;
static u32_t isr_depth;

static void *hal_malloc(u32_t size) { return malloc(size); }
static void hal_free(void *p) { free(p); }
static void hal_halt(void) { fprintf(stderr, "CPU halted\n"); exit(1); }
static bool_t hal_is_log_enabled(log_level_t l) { return l == LOG_ERROR; }
static void hal_log(log_level_t l, char *fmt, ...)
{
	va_list ap;
	if (l != LOG_ERROR) return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
static void hal_sleep_until(timestamp_t ts) { if (ts > now) now = ts; }
static timestamp_t hal_get_timestamp(void) { return now; }
static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t v) { lcd[y][x] = v; }
static void hal_set_lcd_icon(u8_t i, bool_t v) { icons[i] = v; }
/* snd.txt: mudanças do buzzer (tempo, liga/desliga, índice de frequência 0-7) */
static void log_snd(void)
{
	static const u32_t dhz[8] = { 40960, 32768, 27307, 23406, 20480, 16384, 13653, 11703 };
	int i, idx = -1;
	for (i = 0; i < 8; i++) if (dhz[i] == snd_freq_dhz) idx = i;
	if (snd_on == 1) {
		if (snd_logged_freq != idx) fprintf(f_snd, "%.3f on %d\n", now / (double) TS_FREQ, idx);
		snd_logged_freq = idx;
	} else if (snd_logged_freq != -1) {
		fprintf(f_snd, "%.3f off\n", now / (double) TS_FREQ);
		snd_logged_freq = -1;
	}
}
static void hal_set_frequency(u32_t f) { snd_freq_dhz = f; if (snd_on == 1) log_snd(); }
static void hal_play_frequency(bool_t en) { snd_on = en; log_snd(); }
static int hal_handler(void) { return 0; }

static void emit_frame(int force)
{
	char buf[sizeof(last_frame)], *p = buf;
	int x, y, i;

	for (i = 0; i < 8; i++) *p++ = icons[i] ? '0' + i : '.';
	*p++ = '\n';
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 32; x++) *p++ = lcd[y][x] ? '#' : '.';
		*p++ = '\n';
	}
	*p = 0;
	if (!force && strcmp(buf, last_frame) == 0) return;
	strcpy(last_frame, buf);
	fprintf(f_frames, "@%.3f\n%s", now / (double) TS_FREQ, buf);
}

static void hal_update_screen(void) { if (!commit_frames) emit_frame(all_frames); }

static void mem_hook(u12_t n, u4_t old, u4_t v, u13_t pc)
{
	if (log_writes && n < 0x300 && old != v)
		fprintf(f_writes, "%.3f\t%04X\t%03X\t%X\t%X\n", now / (double) TS_FREQ, pc, n, old, v);
}

static void pc_hook(u13_t pc)
{
	int i;
	u32_t depth;

	cov[pc & 0x1FFF]++;
	depth = *tamalib_get_state()->call_depth;
	if (commit_frames) {
		for (i = 0; i < 2; i++) {
			if (commit_active[i] && depth < commit_depth[i]) { commit_active[i] = 0; emit_frame(0); }
			if (!commit_active[i] && pc == commit_addr[i]) { commit_active[i] = 1; commit_depth[i] = depth; }
		}
	}
	if (!nprof) return;
	if (isr_active && depth < isr_depth) { isr_active = 0; isr_total += now - isr_t0; }
	if (!isr_active && (pc == 0x11D || pc == 0x16F || pc == 0x116)) { isr_active = 1; isr_depth = depth; isr_t0 = now; }
	for (i = 0; i < nprof; i++) {
		if (prof[i].active && depth < prof[i].depth) {
			double d = (now - prof[i].t0) * 1000.0 / TS_FREQ;
			prof[i].active = 0; prof[i].n++; prof[i].sum += d;
			prof[i].pure += d - (isr_total - prof[i].isr0) * 1000.0 / TS_FREQ;
			if (prof[i].n == 1 || d < prof[i].min) prof[i].min = d;
			if (d > prof[i].max) prof[i].max = d;
		}
		if (!prof[i].active && pc == prof[i].addr) {
			prof[i].active = 1; prof[i].depth = depth; prof[i].t0 = now; prof[i].isr0 = isr_total;
		}
	}
}

static hal_t hal = {
	.malloc = hal_malloc, .free = hal_free, .halt = hal_halt,
	.is_log_enabled = hal_is_log_enabled, .log = hal_log,
	.sleep_until = hal_sleep_until, .get_timestamp = hal_get_timestamp,
	.update_screen = hal_update_screen,
	.set_lcd_matrix = hal_set_lcd_matrix, .set_lcd_icon = hal_set_lcd_icon,
	.set_frequency = hal_set_frequency, .play_frequency = hal_play_frequency,
	.handler = hal_handler,
};

/* Roda até o tempo emulado alcançar 'until', atualizando a tela a 30 fps */
static void run_until(timestamp_t until)
{
	static timestamp_t screen_ts;

	while (now < until) {
		tamalib_step();
		if (now - screen_ts >= TS_FREQ / 30) {
			screen_ts = now;
			hal_update_screen();
		}
	}
}

static void run_for(double sec) { run_until(now + (timestamp_t) (sec * TS_FREQ)); }

static int parse_btn(const char *s)
{
	switch (s[0]) {
	case 'L': case 'l': return BTN_LEFT;
	case 'M': case 'm': return BTN_MIDDLE;
	case 'R': case 'r': return BTN_RIGHT;
	}
	fprintf(stderr, "botão inválido: %s\n", s);
	exit(1);
}

static void dump_ram(const char *label)
{
	state_t *st = tamalib_get_state();
	int i;

	fprintf(f_ram, "@%.3f %s\n", now / (double) TS_FREQ, label);
	for (i = 0; i < 0x280; i++) {
		if (i % 32 == 0) fprintf(f_ram, "%03X:", i);
		fprintf(f_ram, "%s%X", i % 4 == 0 ? " " : "", GET_RAM_MEMORY(st->memory, i));
		if (i % 32 == 31) fprintf(f_ram, "\n");
	}
}

static void run_script(FILE *f)
{
	char line[512], cmd[64], a1[256], a2[64];
	int n;

	while (fgets(line, sizeof(line), f)) {
		char *c = strchr(line, '#');
		if (c) *c = 0;
		a1[0] = a2[0] = 0;
		n = sscanf(line, "%63s %255s %63s", cmd, a1, a2);
		if (n < 1) continue;

		if (!strcmp(cmd, "wait")) {
			run_for(atof(a1));
		} else if (!strcmp(cmd, "press")) {
			int b = parse_btn(a1);
			tamalib_set_button(b, BTN_STATE_PRESSED);
			run_for(0.15);
			tamalib_set_button(b, BTN_STATE_RELEASED);
			run_for(0.35);
		} else if (!strcmp(cmd, "hold")) {
			int b = parse_btn(a1);
			tamalib_set_button(b, BTN_STATE_PRESSED);
			run_for(atof(a2));
			tamalib_set_button(b, BTN_STATE_RELEASED);
		} else if (!strcmp(cmd, "frame")) {
			emit_frame(1);
		} else if (!strcmp(cmd, "dump")) {
			dump_ram(a1);
		} else if (!strcmp(cmd, "mark")) {
			char *m = line + strlen("mark");
			while (*m == ' ') m++;
			m[strcspn(m, "\n")] = 0;
			fprintf(f_frames, "## %s\n", m);
			if (log_writes) fprintf(f_writes, "## %s\n", m);
		} else if (!strcmp(cmd, "save")) {
			state_save(a1);
		} else if (!strcmp(cmd, "load")) {
			state_load(a1);
		} else {
			fprintf(stderr, "comando desconhecido: %s\n", cmd);
			exit(1);
		}
	}
}

static u12_t *load_rom(const char *path)
{
	FILE *f = fopen(path, "rb");
	u12_t *prog;
	u8_t buf[2];
	int i;

	if (!f) { perror(path); exit(1); }
	prog = malloc(0x2000 * sizeof(u12_t));
	memset(prog, 0, 0x2000 * sizeof(u12_t));
	for (i = 0; i < 0x2000 && fread(buf, 2, 1, f) == 1; i++)
		prog[i] = buf[1] | ((buf[0] & 0xF) << 8);
	fclose(f);
	return prog;
}

static FILE *open_out(const char *dir, const char *name)
{
	char path[1024];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "w");
	if (!f) { perror(path); exit(1); }
	return f;
}

int main(int argc, char **argv)
{
	const char *rom = "rom/tama.b", *script = NULL, *out = "out", *load = NULL;
	FILE *fs;
	int opt;

	while ((opt = getopt(argc, argv, "r:s:o:l:WFCP:")) != -1) {
		switch (opt) {
		case 'r': rom = optarg; break;
		case 's': script = optarg; break;
		case 'o': out = optarg; break;
		case 'l': load = optarg; break;
		case 'W': log_writes = 0; break;
		case 'F': all_frames = 1; break;
		case 'C': commit_frames = 1; break;
		case 'P': if (nprof < MAX_PROF) prof[nprof++].addr = strtol(optarg, NULL, 0); break;
		default:
			fprintf(stderr, "uso: %s [-r rom] [-s script|-] [-o dir] [-l save] [-W sem writes] [-F todo frame]\n", argv[0]);
			return 1;
		}
	}

	f_frames = open_out(out, "frames.txt");
	f_writes = open_out(out, "writes.tsv");
	f_ram = open_out(out, "ram.txt");
	f_snd = open_out(out, "snd.txt");

	tamalib_register_hal(&hal);
	if (tamalib_init(load_rom(rom), NULL, TS_FREQ)) return 1;
	tamalib_set_exec_mode(EXEC_MODE_RUN);
	if (load) state_load((char *) load);
	g_mem_hook = mem_hook;
	g_pc_hook = pc_hook;

	fs = !script || !strcmp(script, "-") ? stdin : fopen(script, "r");
	if (!fs) { perror(script); return 1; }
	run_script(fs);

	{
		FILE *fc = open_out(out, "cov.bin");
		fwrite(cov, sizeof(cov), 1, fc);
		fclose(fc);
	}
	for (opt = 0; opt < nprof; opt++)
		fprintf(stderr, "prof %04X: n=%lu média=%.2f ms min=%.2f max=%.2f  puro=%.2f ms\n", prof[opt].addr, prof[opt].n,
			prof[opt].n ? prof[opt].sum / prof[opt].n : 0, prof[opt].min, prof[opt].max,
			prof[opt].n ? prof[opt].pure / prof[opt].n : 0);
	fclose(f_frames);
	fclose(f_writes);
	fclose(f_ram);
	fclose(f_snd);
	return 0;
}
