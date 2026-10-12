/*
 * Front-end jogável (SDL2): LCD, ícones, buzzer e teclado em tempo real.
 *
 * Teclas: ← ou A = botão L (seleciona), ↓ ou S = M (confirma), → ou D = R (cancela).
 *         F = alterna velocidade 1x / 10x.  Fechar a janela salva o jogo.
 * O jogo salvo fica em ~/.local/share/tamagotchi-p1/save.bin (ou -S arquivo). Com o
 * aplicativo fechado o tempo do pet para (como no TamaTool), já que o original não
 * tem relógio de tempo real fora do CPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL.h>
#ifdef HAVE_SDL_IMAGE
#include <SDL_image.h>
#endif
#include "tama.h"

#define PIXEL 12                 /* tamanho do pixel do LCD na tela */
#define ICON 48
#define MARGIN 16
#define LCD_W (32 * PIXEL)
#define LCD_H (16 * PIXEL)
#define WIN_W (LCD_W + 2 * MARGIN)
#define WIN_H (LCD_H + 2 * ICON + 4 * MARGIN)

u8 front_buttons;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *icons_tex;
static bool dirty = true;
static double wall0, emu0;       /* sincronia: wall0 (s) corresponde a emu0 (s emulado) */
static int speed = 1;
static char save_path[1024];

/* ---- som: onda quadrada na frequência do buzzer ---- */
static SDL_AudioDeviceID audio;
static volatile int buzz_on, buzz_hz;
static double phase;

static void audio_cb(void *u, Uint8 *stream, int len)
{
	Sint16 *s = (Sint16 *) stream;
	int i, n = len / 2;

	(void) u;
	for (i = 0; i < n; i++) {
		if (buzz_on && buzz_hz) {
			phase += (double) buzz_hz / 44100.0;
			if (phase >= 1) phase -= 1;
			s[i] = phase < 0.5 ? 2500 : -2500;
		} else {
			s[i] = 0;
		}
	}
}

/* frequências do buzzer do E0C6S46 (registrador F74, valores 0..7) */
static const int buzzer_hz[8] = { 4096, 3277, 2731, 2341, 2048, 1638, 1365, 1170 };

void front_set_buzzer(bool on, u8 freq)
{
	buzz_hz = buzzer_hz[freq & 7];
	buzz_on = on && speed == 1;
}

void front_lcd_changed(void) { dirty = true; }

static double wall(void) { return SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency(); }

static void render(void)
{
	SDL_Rect r;
	int x, y, i;

	SDL_SetRenderDrawColor(ren, 0xC8, 0xD0, 0xB0, 255);   /* fundo do LCD */
	SDL_RenderClear(ren);
	for (i = 0; i < 8; i++) {
		SDL_Rect d = { MARGIN + (i % 4) * (LCD_W / 4) + (LCD_W / 4 - ICON) / 2,
		               i < 4 ? MARGIN : WIN_H - MARGIN - ICON, ICON, ICON };
		bool on = (lcd_icons >> i) & 1;
		if (icons_tex) {
			SDL_Rect s = { (i % 4) * 64, (i / 4) * 64, 64, 64 };
			SDL_SetTextureAlphaMod(icons_tex, on ? 255 : 40);
			SDL_RenderCopy(ren, icons_tex, &s, &d);
		} else {
			SDL_SetRenderDrawColor(ren, 0x30, 0x34, 0x28, on ? 255 : 30);
			SDL_RenderFillRect(ren, &d);
		}
	}
	SDL_SetRenderDrawColor(ren, 0x30, 0x34, 0x28, 255);
	for (y = 0; y < 16; y++)
		for (x = 0; x < 32; x++)
			if (lcd[y][x]) {
				r.x = MARGIN + x * PIXEL; r.y = 2 * MARGIN + ICON + y * PIXEL;
				r.w = r.h = PIXEL - 1;
				SDL_RenderFillRect(ren, &r);
			}
	SDL_RenderPresent(ren);
	dirty = false;
}

static u8 key_button(SDL_Keycode k)
{
	switch (k) {
	case SDLK_LEFT: case SDLK_a: return BTN_L;
	case SDLK_DOWN: case SDLK_s: return BTN_M;
	case SDLK_RIGHT: case SDLK_d: return BTN_R;
	}
	return 0;
}

static void quit(void)
{
	if (save_path[0] && !machine_save(save_path))
		fprintf(stderr, "não consegui salvar em %s\n", save_path);
	SDL_Quit();
	exit(0);
}

void front_pump(double t)
{
	static double last_render;
	SDL_Event e;

	/* não deixa o tempo emulado passar à frente do relógio real */
	for (;;) {
		double ahead = (t - emu0) / speed - (wall() - wall0);
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) quit();
			if (e.type == SDL_KEYDOWN && !e.key.repeat) {
				if (e.key.keysym.sym == SDLK_f) {
					speed = speed == 1 ? 10 : 1;
					wall0 = wall(); emu0 = t;
					buzz_on = 0;
				}
				front_buttons |= key_button(e.key.keysym.sym);
			}
			if (e.type == SDL_KEYUP) front_buttons &= ~key_button(e.key.keysym.sym);
		}
		if (dirty && wall() - last_render > 1.0 / 60) { render(); last_render = wall(); }
		if (ahead <= 0.001) break;
		SDL_Delay(ahead > 0.01 ? 10 : 1);
	}
	if (t - emu0 > 0 && wall() - wall0 > 0.5 + (t - emu0) / speed) { wall0 = wall(); emu0 = t; }  /* atrasou: não acelera depois */
}

int main(int argc, char **argv)
{
	const char *rom = NULL, *res = NULL;
	bool fresh = false;
	SDL_AudioSpec want = { 0 };
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-r") && i + 1 < argc) rom = argv[++i];
		else if (!strcmp(argv[i], "-S") && i + 1 < argc) snprintf(save_path, sizeof(save_path), "%s", argv[++i]);
		else if (!strcmp(argv[i], "-i") && i + 1 < argc) res = argv[++i];
		else if (!strcmp(argv[i], "-n")) fresh = true;
		else {
			fprintf(stderr, "uso: %s [-r rom] [-S save] [-i icones.png] [-n (novo jogo)]\n", argv[0]);
			return 1;
		}
	}
	if (!rom) rom = getenv("TAMA_ROM") ? getenv("TAMA_ROM") : "rom/tama.b";
	if (!rom_load(rom)) { fprintf(stderr, "não consegui ler a ROM %s\n", rom); return 1; }
	if (!save_path[0] && getenv("HOME")) {
		char dir[900];
		snprintf(dir, sizeof(dir), "%s/.local/share/tamagotchi-p1", getenv("HOME"));
		mkdir(dir, 0755);
		snprintf(save_path, sizeof(save_path), "%s/save.bin", dir);
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
	win = SDL_CreateWindow("Tamagotchi P1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, 0);
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
#ifdef HAVE_SDL_IMAGE
	if (!res) res = "tamatool/res/p1-icons.png";
	icons_tex = IMG_LoadTexture(ren, res);
#endif
	want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
	audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	if (audio) SDL_PauseAudioDevice(audio, 0);

	machine_reset();
	wall0 = wall(); emu0 = 0;
	if (!fresh && machine_load(save_path)) {
		emu0 = machine_now();
		render();
		game_resume();
	}
	game_main();
	return 0;
}
