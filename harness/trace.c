/*
 * trace - emulador headless do Tamagotchi P1 para engenharia reversa.
 *
 * Roda o TamaLIB com tempo virtual (o mais rápido possível, determinístico),
 * input vindo de um script, e grava:
 *   frames.txt  - cada frame do LCD que mudou (32x16 + 8 ícones), com o tempo
 *   writes.tsv  - escritas na RAM (t_ms pc addr old new), só quando o valor muda
 *   cov.bin     - contagem de execuções por PC (u32 x 0x2000)
 *   ram.txt     - dumps de RAM pedidos pelo script (comando "dump")
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

#define TS_FREQ 1000000ULL /* timestamps em microssegundos */

static timestamp_t now;
static u8_t lcd[16][32], icons[8];
static char last_frame[16 * 33 + 16];
static FILE *f_frames, *f_writes, *f_ram;
static u32_t cov[0x2000];
static int log_writes = 1, all_frames = 0;

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
static void hal_set_frequency(u32_t f) { (void) f; }
static void hal_play_frequency(bool_t en) { (void) en; }
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

static void hal_update_screen(void) { emit_frame(all_frames); }

static void mem_hook(u12_t n, u4_t old, u4_t v, u13_t pc)
{
	if (log_writes && n < 0x300 && old != v)
		fprintf(f_writes, "%.3f\t%04X\t%03X\t%X\t%X\n", now / (double) TS_FREQ, pc, n, old, v);
}

static void pc_hook(u13_t pc) { cov[pc & 0x1FFF]++; }

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

	while ((opt = getopt(argc, argv, "r:s:o:l:WF")) != -1) {
		switch (opt) {
		case 'r': rom = optarg; break;
		case 's': script = optarg; break;
		case 'o': out = optarg; break;
		case 'l': load = optarg; break;
		case 'W': log_writes = 0; break;
		case 'F': all_frames = 1; break;
		default:
			fprintf(stderr, "uso: %s [-r rom] [-s script|-] [-o dir] [-l save] [-W sem writes] [-F todo frame]\n", argv[0]);
			return 1;
		}
	}

	f_frames = open_out(out, "frames.txt");
	f_writes = open_out(out, "writes.tsv");
	f_ram = open_out(out, "ram.txt");

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
	fclose(f_frames);
	fclose(f_writes);
	fclose(f_ram);
	return 0;
}
