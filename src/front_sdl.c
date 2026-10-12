/*
 * Front-end jogável (SDL2): LCD, ícones, buzzer e teclado em tempo real.
 *
 * Teclas: ← ou A = botão L (seleciona), ↓ ou S = M (confirma), → ou D = R (cancela).
 *         F = alterna velocidade 1x / 10x.  Fechar a janela salva o jogo.
 * O jogo salvo fica na pasta de preferências do SDL (Linux: ~/.local/share/tamagotchi-p1/save.bin;
 * Windows: %APPDATA%\tamagotchi-p1\save.bin) ou em -S arquivo. Com o
 * aplicativo fechado o tempo do pet para (como no TamaTool), já que o original não
 * tem relógio de tempo real fora do CPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
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
#define BASE_H (LCD_H + 2 * ICON + 4 * MARGIN)
#ifdef __ANDROID__
#define PAD_H 160                /* faixa dos 3 botões de toque abaixo do LCD */
#else
#define PAD_H 0
#endif
#define WIN_H (BASE_H + PAD_H)

u8 front_buttons;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *icons_tex;
static bool dirty = true;
static double wall0, emu0;       /* sincronia: wall0 (s) corresponde a emu0 (s emulado) */
static int speed = 1;
static char save_path[1024];
static volatile sig_atomic_t want_quit;

static void on_signal(int sig) { (void) sig; want_quit = 1; }

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

#ifdef __ANDROID__
/* toque: cada dedo acende o botão sob ele (faixa PAD_H, em coordenadas lógicas) */
static struct { SDL_FingerID id; u8 mask; bool used; } fingers[10];

static u8 touch_buttons(void)
{
	u8 m = 0;
	int i;
	for (i = 0; i < 10; i++) if (fingers[i].used) m |= fingers[i].mask;
	return m;
}

static u8 touch_mask_at(float nx, float ny)
{
	int w, h;
	float lx, ly;
	SDL_GetWindowSize(win, &w, &h);
	SDL_RenderWindowToLogical(ren, (int) (nx * w), (int) (ny * h), &lx, &ly);
	if (ly < BASE_H || ly >= WIN_H || lx < 0 || lx >= WIN_W) return 0;
	return (u8) (BTN_L >> (int) (lx * 3 / WIN_W));          /* L, M, R */
}

static void touch_event(const SDL_TouchFingerEvent *t, bool down)
{
	int i, slot = -1;
	for (i = 0; i < 10; i++) if (fingers[i].used && fingers[i].id == t->fingerId) slot = i;
	if (slot < 0 && down) for (i = 0; i < 10; i++) if (!fingers[i].used) { slot = i; break; }
	if (slot < 0) return;
	fingers[slot].id = t->fingerId;
	fingers[slot].used = down;
	fingers[slot].mask = down ? touch_mask_at(t->x, t->y) : 0;
	front_buttons = touch_buttons();
	dirty = true;
}
#endif

static void render(void)
{
	SDL_Rect r;
	int x, y, i;

	SDL_SetRenderDrawColor(ren, 0xC8, 0xD0, 0xB0, 255);   /* fundo do LCD */
	SDL_RenderClear(ren);
	for (i = 0; i < 8; i++) {
		SDL_Rect d = { MARGIN + (i % 4) * (LCD_W / 4) + (LCD_W / 4 - ICON) / 2,
		               i < 4 ? MARGIN : BASE_H - MARGIN - ICON, ICON, ICON };
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
#ifdef __ANDROID__
	for (i = 0; i < 3; i++) {
		SDL_Rect b = { MARGIN + i * (WIN_W - 2 * MARGIN) / 3 + 8, BASE_H + 12, (WIN_W - 2 * MARGIN) / 3 - 16, PAD_H - 24 };
		bool down = (touch_buttons() >> (2 - i)) & 1;       /* esquerda->direita = L, M, R */
		SDL_SetRenderDrawColor(ren, down ? 0xE8 : 0xB0, down ? 0x70 : 0x40, 0x40, 255);
		SDL_RenderFillRect(ren, &b);
		SDL_SetRenderDrawColor(ren, 0xFF, 0xFF, 0xFF, 255);
		for (y = 0; y <= i; y++) {                           /* 1, 2 ou 3 pontos = A, B, C */
			SDL_Rect d = { b.x + b.w / 2 - 10 * (i + 1) + 20 * y + 4, b.y + b.h / 2 - 8, 16, 16 };
			SDL_RenderFillRect(ren, &d);
		}
	}
#endif
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

#ifdef __EMSCRIPTEN__
/* no navegador não há "fechar": salva a cada poucos segundos e quando a aba some;
 * syncfs persiste o sistema de arquivos (IDBFS) no IndexedDB */
EMSCRIPTEN_KEEPALIVE void tama_save(void)
{
	if (save_path[0] && machine_save(save_path))
		EM_ASM(if (Module.persist) Module.persist(););
}

EMSCRIPTEN_KEEPALIVE void tama_button(int mask, int down)   /* botões na tela (toque) */
{
	if (down) front_buttons |= mask; else front_buttons &= ~mask;
}
#endif

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

	if (want_quit) quit();
#ifdef __EMSCRIPTEN__
	{
		static double last_save, last_yield;
		if (wall() - last_save > 10) { last_save = wall(); tama_save(); }
		if (wall() - last_yield > 0.016) { last_yield = wall(); emscripten_sleep(0); }  /* devolve o controle ao navegador */
	}
#endif
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
#ifdef __ANDROID__
			if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION) touch_event(&e.tfinger, true);
			if (e.type == SDL_FINGERUP) touch_event(&e.tfinger, false);
			if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_TERMINATING)
				if (save_path[0]) machine_save(save_path);
#endif
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
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
#ifdef __ANDROID__
	static char arom[1100];
	if (!rom) {   /* a ROM foi copiada pela PickActivity para o armazenamento interno do app */
		snprintf(arom, sizeof(arom), "%s/tama.b", SDL_AndroidGetInternalStoragePath());
		rom = arom;
	}
#endif
	if (!rom) rom = getenv("TAMA_ROM") ? getenv("TAMA_ROM") : "rom/tama.b";
	if (!rom_load(rom)) {
		/* sem -r: tenta também ao lado do executável (duplo clique no Windows) */
		char *base = SDL_GetBasePath();
		char alt[1100];
		bool ok = false;
		if (base) {
			snprintf(alt, sizeof(alt), "%srom/tama.b", base);
			ok = rom_load(alt);
			if (!ok) { snprintf(alt, sizeof(alt), "%stama.b", base); ok = rom_load(alt); }
			SDL_free(base);
		}
		if (!ok) { fprintf(stderr, "não consegui ler a ROM %s (coloque tama.b ao lado do programa ou use -r)\n", rom); return 1; }
	}
	if (!save_path[0]) {
		char *pref = SDL_GetPrefPath("", "tamagotchi-p1");   /* cria a pasta se preciso */
		if (pref) {
			snprintf(save_path, sizeof(save_path), "%ssave.bin", pref);
			SDL_free(pref);
		}
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	win = SDL_CreateWindow("Tamagotchi P1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, 0);
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
#ifdef __ANDROID__
	SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);       /* escala para a tela do aparelho */
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "0");
#endif
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
