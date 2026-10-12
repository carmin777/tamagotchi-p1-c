/*
 * Front-end sem janela: roda o jogo com tempo virtual guiado por um script de input no
 * mesmo formato do harness/trace (wait/press/hold/mark/frame) e grava frames.txt no mesmo
 * formato, para comparar com o emulador de referência (tools/cmpframes.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tama.h"

u8 front_buttons;

enum { EV_DOWN, EV_UP, EV_MARK, EV_FRAME, EV_DUMP, EV_END };
struct ev { double t; int kind; u8 btn; char text[96]; };

static struct ev *evs;
static int nevs, cur;
static FILE *f_frames, *f_ram, *f_snd;
static int snd_logged_freq = -1;
static char last_frame[16 * 33 + 16];
static double now_s;

static void add(double t, int kind, u8 btn, const char *text)
{
	evs = realloc(evs, sizeof(*evs) * (nevs + 1));
	evs[nevs].t = t;
	evs[nevs].kind = kind;
	evs[nevs].btn = btn;
	snprintf(evs[nevs].text, sizeof(evs[nevs].text), "%s", text ? text : "");
	nevs++;
}

static u8 parse_btn(const char *s)
{
	switch (s[0]) {
	case 'L': case 'l': return BTN_L;
	case 'M': case 'm': return BTN_M;
	case 'R': case 'r': return BTN_R;
	}
	fprintf(stderr, "botão inválido: %s\n", s);
	exit(1);
}

static void load_script(FILE *f)
{
	char line[512], cmd[64], a1[256], a2[64];
	double t = 0;

	while (fgets(line, sizeof(line), f)) {
		char *c = strchr(line, '#');
		if (c) *c = 0;
		a1[0] = a2[0] = 0;
		if (sscanf(line, "%63s %255s %63s", cmd, a1, a2) < 1) continue;
		if (!strcmp(cmd, "wait")) t += atof(a1);
		else if (!strcmp(cmd, "press")) {
			u8 b = parse_btn(a1);
			add(t, EV_DOWN, b, NULL); t += 0.15;
			add(t, EV_UP, b, NULL); t += 0.35;
		} else if (!strcmp(cmd, "hold")) {
			u8 b = parse_btn(a1);
			add(t, EV_DOWN, b, NULL); t += atof(a2);
			add(t, EV_UP, b, NULL);
		} else if (!strcmp(cmd, "mark")) {
			char *m = line + strlen("mark");
			while (*m == ' ') m++;
			m[strcspn(m, "\n")] = 0;
			add(t, EV_MARK, 0, m);
		} else if (!strcmp(cmd, "frame")) add(t, EV_FRAME, 0, NULL);
		else if (!strcmp(cmd, "dump")) add(t, EV_DUMP, 0, a1);
		else if (!strcmp(cmd, "save") || !strcmp(cmd, "load"))
			fprintf(stderr, "aviso: '%s' ignorado na reescrita\n", cmd);
		else { fprintf(stderr, "comando desconhecido: %s\n", cmd); exit(1); }
	}
	add(t, EV_END, 0, NULL);
}

static void emit_frame(int force)
{
	char buf[sizeof(last_frame)], *p = buf;
	int x, y, i;

	for (i = 0; i < 8; i++) *p++ = (lcd_icons >> i) & 1 ? '0' + i : '.';
	*p++ = '\n';
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 32; x++) *p++ = lcd[y][x] ? '#' : '.';
		*p++ = '\n';
	}
	*p = 0;
	if (!force && !strcmp(buf, last_frame)) return;
	strcpy(last_frame, buf);
	fprintf(f_frames, "@%.3f\n%s", now_s, buf);
}

void front_lcd_changed(void) { emit_frame(0); }

/* Reconstrói a RAM do original (só os endereços que a reescrita mapeia; o resto fica '-')
 * para comparar com o ram.txt do harness (tools/cmpram.py) */
static void put8(int *ram, int a, unsigned v, int nibbles)
{
	int i;
	for (i = 0; i < nibbles; i++) ram[a + i] = (v >> (4 * i)) & 0xF;
}

static void dump_ram(const char *label)
{
	int ram[0x280], i;

	for (i = 0; i < 0x280; i++) ram[i] = -1;
	put8(ram, 0x10, g.sec, 2); put8(ram, 0x12, g.min, 2); put8(ram, 0x14, g.hour, 2);
	put8(ram, 0x40, g.hunger, 1); put8(ram, 0x41, g.happy, 1);
	put8(ram, 0x42, g.care_mistakes, 1); put8(ram, 0x43, g.discipline, 1);
	put8(ram, 0x46, g.weight, 2); put8(ram, 0x48, g.sick, 1); put8(ram, 0x49, g.sick_count, 1);
	put8(ram, 0x4A, g.sleep, 1); put8(ram, 0x4B, g.light, 1); put8(ram, 0x4D, g.poop, 1);
	put8(ram, 0x4E, g.lifespan_over, 1); put8(ram, 0x4F, g.lifespan_strikes, 1);
	put8(ram, 0x50, g.stage, 1); put8(ram, 0x51, g.discipline_misses, 1); put8(ram, 0x54, g.age, 2);
	put8(ram, 0x5C, g.event, 1); put8(ram, 0x5D, g.species, 1);
	put8(ram, 0x7B, g.sound_on, 1); put8(ram, 0x7C, g.egg, 1);
	put8(ram, 0x73, g.food_choice, 1); put8(ram, 0x78, g.saved_cursor78, 1);   /* menu_care.c */
	put8(ram, 0x200, g.hunger_timer, 2); put8(ram, 0x202, g.happy_timer, 2);
	put8(ram, 0x204, g.sleep_minutes, 2); put8(ram, 0x206, g.poop_timer, 2);
	put8(ram, 0x208, g.call_hunger, 1); put8(ram, 0x209, g.call_discipline, 1);
	put8(ram, 0x20A, g.empty_heart_min, 3); put8(ram, 0x20D, g.sick_timer, 3);
	put8(ram, 0x210, g.stage_timer, 3); put8(ram, 0x213, g.call_q213, 1); put8(ram, 0x214, g.call_q214, 1);
	put8(ram, 0x215, g.nap, 1); put8(ram, 0x216, g.baby_awake, 2);
	for (i = 0; i < 26; i++) ram[0x230 + i] = g.p.raw[i];
	put8(ram, 0x230, g.p.wake_hour, 2); put8(ram, 0x232, g.p.bed_hour, 2);
	put8(ram, 0x234, g.p.hunger_interval, 2); put8(ram, 0x236, g.p.happy_interval, 2);
	put8(ram, 0x5A, g.rng, 1);                                  /* menu_info.c */
	put8(ram, 0x82, g.game_rounds, 1);
	put8(ram, 0x83, g.game_wins, 1); put8(ram, 0x84, g.game_result, 1);

	fprintf(f_ram, "@%.3f %s\n", now_s, label);
	for (i = 0; i < 0x280; i++) {
		if (i % 32 == 0) fprintf(f_ram, "%03X:", i);
		if (i % 4 == 0) fputc(' ', f_ram);
		if (ram[i] < 0) fputc('-', f_ram); else fprintf(f_ram, "%X", ram[i]);
		if (i % 32 == 31) fputc('\n', f_ram);
	}
	fflush(f_ram);
}
/* snd.txt: mudanças do buzzer, no mesmo formato do harness */
void front_set_buzzer(bool on, u8 freq)
{
	if (on) {
		if (snd_logged_freq != (freq & 7)) fprintf(f_snd, "%.3f on %d\n", now_s, freq & 7);
		snd_logged_freq = freq & 7;
	} else if (snd_logged_freq != -1) {
		fprintf(f_snd, "%.3f off\n", now_s);
		snd_logged_freq = -1;
	}
}

void front_pump(double t)
{
	now_s = t;
	while (cur < nevs && evs[cur].t <= t) {
		struct ev *e = &evs[cur++];
		switch (e->kind) {
		case EV_DOWN: front_buttons |= e->btn; break;
		case EV_UP: front_buttons &= ~e->btn; break;
		case EV_MARK: fprintf(f_frames, "## %s\n", e->text); break;
		case EV_FRAME: emit_frame(1); break;
		case EV_DUMP: dump_ram(e->text); break;
		case EV_END: fclose(f_frames); fclose(f_ram); fclose(f_snd); exit(0);
		}
	}
}

int main(int argc, char **argv)
{
	const char *rom = "rom/tama.b", *script = NULL, *out = "out/native";
	char path[1024];
	FILE *fs;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-r") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-s") && i + 1 < argc) script = argv[++i];
		else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
		else { fprintf(stderr, "uso: %s [-r rom] -s script [-o dir]\n", argv[0]); return 1; }
	}
	if (!rom_load(rom)) { fprintf(stderr, "não consegui ler a ROM %s\n", rom); return 1; }
	fs = (!script || !strcmp(script, "-")) ? stdin : fopen(script, "r");
	if (!fs) { perror(script); return 1; }
	load_script(fs);
	snprintf(path, sizeof(path), "%s/frames.txt", out);
	f_frames = fopen(path, "w");
	if (!f_frames) { perror(path); return 1; }
	snprintf(path, sizeof(path), "%s/ram.txt", out);
	f_ram = fopen(path, "w");
	if (!f_ram) { perror(path); return 1; }
	snprintf(path, sizeof(path), "%s/snd.txt", out);
	f_snd = fopen(path, "w");
	if (!f_snd) { perror(path); return 1; }

	machine_reset();
	game_main();
	return 0;
}
