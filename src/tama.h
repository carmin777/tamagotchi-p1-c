/*
 * Tamagotchi P1 — native C rewrite.
 *
 * The original game runs on an Epson E0C6S46 (4-bit). This rewrite reproduces the logic
 * routine by routine, but with names: each function and field carries, in a comment, the
 * address of the original ROM / RAM it came from (see re/notes/).
 *
 * Execution model: the game code is sequential and "blocking", like the original.
 * The two hardware interrupts become C functions (machine.c) called whenever the
 * game code consumes time via cost_ms() or waits for ticks.
 */
#ifndef TAMA_H
#define TAMA_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;

/* ---------------------------------------------------------------- ROM (rom.c) */
/* The data (sprites, animation scripts, sounds, per-species parameters) is read from the
 * original ROM at run time: E0C6S46 tables are sequences of `lbpx mx,b` instructions
 * (store byte b and continue) ended by `retd b` (store b and return). */
bool rom_load(const char *path);
u16 rom_word(u16 addr);
/* Reads the table bytes starting at addr up to the retd (inclusive). Returns how many. */
int rom_table(u16 addr, u8 *out, int max);
/* Byte of a single table entry (lbpx or retd). */
u8 rom_byte(u16 addr);

/* ---------------------------------------------------------------- buttons */
enum { BTN_R = 1, BTN_M = 2, BTN_L = 4 };   /* bits as in the game's RAM (K00=R, K01=M, K02=L) */

/* ---------------------------------------------------------------- machine (machine.c) */
void machine_reset(void);
void cost_ms(double ms);          /* consumes CPU time (fires the "interrupts") */
void wait_ticks(u8 n);            /* 0x03C: waits n prog timer ticks (27.25 ms) */
void read_buttons(void);          /* 0x021: snapshot into g.btn_held / g.btn_pressed */
double machine_now(void);         /* emulated time in seconds */
bool machine_save(const char *path);  /* game state + vram + emulated clock */
bool machine_load(const char *path);

/* Sounds (pointers into the step table of page E; see sound.c) */
enum {
	SND_IDLE = 0x7D, SND_BOOT = 0x80, SND_BEEP = 0x94, SND_TOGGLE = 0x97,
	SND_CALL = 0xA5, SND_EVOLVE = 0xAE, SND_DEATH1 = 0xBF, SND_DEATH2 = 0xC4,
	SND_DEATH3 = 0xC9, SND_GRAVE = 0xCE, SND_HATCH = 0xD1,
};
void play_sound(u8 ptr, u8 repeat_lo, u8 repeat_hi); /* 0x0BF: only if g.sound_on */
void play_beep(void);                                 /* 0x0BB */
double sound_step(void);                              /* 0x1A0, every tick; returns the cost (ms) */

/* ---------------------------------------------------------------- video (video.c) */
/* Video buffer = RAM 0x100–0x1FF (256 nibbles): 64 columns x 16 rows.
 * vram[i]: nibble i; column x of the upper half occupies vram[2x] (rows 0-3) and vram[2x+1]
 * (rows 4-7); the lower half is at +0x80. The LCD shows a 32-column window
 * starting at offset g.view (0x02A/0x02B). Drawing addresses are 8-bit nibble
 * indices (with wrap-around like the original). */
extern u8 vram[256];
extern u8 lcd[16][32];            /* what is on the LCD right now (1 = lit) */
extern u8 lcd_icons;              /* bit i = icon i lit */

void vram_clear(void);                    /* 0x040 clear_page_0x100 (clears everything, view=0) */
void vram_clear_range(u8 at, int n);      /* clears n nibbles starting at at */
u8 draw_rom_bitmap(u8 at, u16 rom_addr);  /* copies the lbpx/retd table into vram; returns the final at */
u8 draw_asset(u8 at, u8 page, u8 idx);    /* 0x000 render_asset: ROM 0x1000+0x100*page+0x10*idx */
u8 draw_tile(u8 at, u8 tile);             /* 0x7A5 misc_render: 8x8 tile (0x1600+8n); >=64 erases */
void lcd_present(void);                   /* 0x056 copy_video_buf_to_vram */
void set_menu_icon(u8 icon);              /* 0x7C0: 0 none, 1..8 */
void write_icons(void);                   /* 0x7E1 */
void view_to_right(void);                 /* 0x0CE label_30: view = 0x40, drawing cursor = 0x40 */
void scroll_view_left(u8 step);           /* 0x500 */
void scroll_view_right(u8 limit_hi, u8 step); /* 0xAF0 */
void render_tile_list(u8 at, const u8 *tiles); /* 0x792: 8 tiles (2 strips of 4) */

/* ---------------------------------------------------------------- game state */
/* Per-species parameters (13-byte block of ROM 0x1564+, loaded into RAM 0x230). */
struct species_params {
	u8 wake_hour, bed_hour;            /* 0x230, 0x232 (binary hour; FF = n/a) */
	u8 hunger_interval, happy_interval;/* 0x234, 0x236: minutes per 1/4 heart */
	u16 sick_reload;                   /* 0x238-0x23A (12 bits) */
	u8 medicine_doses;                 /* 0x23B (?) */
	u8 weight_min, weight_max;         /* 0x23C, 0x23E (BCD) */
	u16 stage_minutes;                 /* 0x240-0x242 (12 bits) */
	u8 call_reload;                    /* 0x243 (reload of q213) */
	u8 game_win_chance;                /* 0x244 (?) low nibble of byte 10 */
	u8 game_param_245;                 /* 0x245 */
	u8 weight_start;                   /* 0x246 (?) */
	u8 flag_248, param_249;            /* 0x248, 0x249 */
	u8 raw[26];                        /* nibbles 0x230..0x249 */
};

struct game {
	/* clock (0x010-0x015): seconds and minutes in BCD, hour in binary 0..23 */
	u8 sec, min, hour;
	u8 clock_running;      /* 0x03C: !=0 => the clock runs */
	u8 clock_dirty;        /* 0x02E: 1 every second; cleared on redraw */
	u8 seconds_counter_lo; /* 0x02F: free-running seconds counter (battery only) */
	u8 timeout_s;          /* 0x057: seconds left (−1/s) */

	/* input (0x026-0x029, 0x03D-0x03F) */
	u8 btn_down;           /* 0x026: currently held */
	u8 btn_edges;          /* 0x027: presses accumulated since the last read */
	u8 btn_held;           /* 0x028: snapshot of 0x026 after read_buttons */
	u8 btn_pressed;        /* 0x029: snapshot of 0x027 after read_buttons */
	u8 rep_last, rep_count, rep_mask; /* 0x03D, 0x03E, 0x03F: auto-repeat */
	u8 delay;              /* 0x022/0x023: wait_ticks counter */
	u8 rng;                /* 0x05A: free-running 4-bit counter (+1 per tick) */

	/* sound (0x032-0x039, 0x058/0x059, 0x07B) */
	u8 snd_dur, snd_freq, snd_env, snd_ptr, snd_rep_lo, snd_rep_hi, snd_hlp;
	u8 sound_on;           /* 0x07B */

	/* video (0x02A-0x02D, 0x03A/0x03B) */
	u8 view;               /* 0x02A/0x02B: window offset */
	u16 icon_mask;         /* 0x02C/0x02D */
	u8 draw_at;            /* 0x03A/0x03B: drawing cursor (nibble in vram) */

	/* pet (page 0) */
	u8 hunger, happy;      /* 0x040, 0x041: 1/4 heart (4 = 1 heart), 0..F */
	u8 care_mistakes;      /* 0x042 */
	u8 discipline;         /* 0x043 */
	u8 weight;             /* 0x046/0x047 (BCD) */
	u8 sick;               /* 0x048: bit3 = sick */
	u8 sick_count;         /* 0x049 */
	u8 sleep;              /* 0x04A: bit3 = sleeping, bits 0-2 = phase of the "Z" */
	u8 light;              /* 0x04B: 0 = light off */
	u8 m4c;                /* 0x04C (?) */
	u8 poop;               /* 0x04D */
	u8 lifespan_over;      /* 0x04E */
	u8 lifespan_strikes;   /* 0x04F */
	u8 stage;              /* 0x050: evolution branch (F = final form) */
	u8 discipline_misses;  /* 0x051 */
	u8 age;                /* 0x054/0x055 (BCD, days) */
	u8 event;              /* 0x05C: pending event */
	u8 species;            /* 0x05D: 0 egg, 1 Babytchi, 2 Marutchi, ... */
	u8 anim_set;           /* 0x05E */
	u8 food_choice;        /* 0x073 */
	u8 skippable;          /* 0x074: button interrupts the animation */
	u8 cursor, cursor_limit, timeout_reload; /* 0x075-0x077 */
	u8 saved_cursor78;     /* 0x078 */
	u8 egg;                /* 0x07C: 5..1 egg countdown, 0 = hatch, F = hatched */
	u8 frame_counter;      /* 0x07E */
	u8 menu_saved;         /* 0x0A0 */
	u8 m80;                /* 0x080: scratch of the screens */

	/* pet animation engine (0x05F-0x070) */
	u8 pose_timer;         /* 0x05F */
	u8 pose_flags;         /* 0x063 (0x062 = delay) */
	u8 pose_ptr;           /* 0x064/0x065 (page B) */
	u8 move_count;         /* 0x066/0x067 */
	u8 move_arg;           /* 0x068/0x069 */
	u8 move_ptr;           /* 0x06A/0x06B (page 9) */
	u8 pet_pos;            /* 0x06C/0x06D */
	u8 sprite_a, sprite_b; /* 0x06E, 0x06F */
	u8 move_flags;         /* 0x070 */

	/* per-minute counters (page 2) */
	u8 hunger_timer;       /* 0x200/0x201 */
	u8 happy_timer;        /* 0x202/0x203 */
	u8 sleep_minutes;      /* 0x204/0x205 */
	u8 poop_timer;         /* 0x206/0x207: minutes until the next poop */
	u8 call_hunger;        /* 0x208 */
	u8 call_discipline;    /* 0x209 */
	u16 empty_heart_min;   /* 0x20A-0x20C */
	u16 sick_timer;        /* 0x20D-0x20F */
	u16 stage_timer;       /* 0x210-0x212 */
	u8 call_q213, call_q214; /* 0x213, 0x214 */
	u8 nap;                /* 0x215 (Babytchi) */
	u8 baby_awake;         /* 0x216/0x217 (Babytchi) */
	struct species_params p; /* 0x230-0x249 */
	u8 clock_backup[3];    /* 0x272-0x277 */

	/* --- fields added by menu_care.c --- */
	/* --- fields added by menu_info.c --- */
	u8 game_wait;          /* 0x080: wait frames after the guess */
	u8 game_guess;         /* 0x081: guess (BTN_* bits; 0 = none yet) */
	u8 game_rounds;        /* 0x082: rounds left */
	u8 game_wins;          /* 0x083: correct guesses */
	u8 game_result;        /* 0x084: 8 = round won, 0 = lost */
	/* --- fields added by life.c --- */
};
extern struct game g;

/* nibble i (0 = low) of a value */
#define NIB(v, i) (((v) >> (4 * (i))) & 0xF)

/* ---------------------------------------------------------------- game */
void game_main(void);           /* 0x110 reset: does not return */
void game_resume(void);         /* resumes a saved game (does not exist in the original) */

/* anim.c */
void reset_pet_animation(void); /* 0x465 */
void step_pet_animation(void);  /* 0x486 */
void draw_pet_overlays(void);   /* 0x400 */
void render_pet_frame(void);    /* 0x052: step + overlays + present */
void select_pet_anim_set(void); /* 0x596 */
void draw_pet_half(u8 frame, u8 flip, bool lower); /* 0x928 / 0x939 */

/* screens.c */
enum screen { SCR_BOOT_WAIT, SCR_SET_CLOCK, SCR_CLOCK, SCR_PET, SCR_PET_MENU };
enum screen boot_wait_for_start(void); /* 0x97F */
enum screen clock_screen(void);        /* 0x293 */
enum screen pet_screen(void);          /* 0x508 */
enum screen pet_menu_loop(void);       /* 0x512 */
void soft_reset(void);                 /* 0x206: new egg keeping the clock */
u8 draw_clock(void);            /* 0x5A6 */

/* events.c */
void minute_tick(void);         /* 0xC7A */
bool check_events(void);        /* 0xF00 */
void dispatch_event(void);      /* 0x307 */
void init_new_game_state(void); /* 0x761 */
void load_species_params(void); /* 0xDD2 (part) */
void hatch_egg(void);           /* 0xFEC */

/* menu_care.c: food, light, medicine, toilet, discipline */
void menu_food(void);           /* 0x710 */
void menu_light(void);          /* 0xABE */
void menu_medicine(void);       /* 0x600 */
void menu_toilet(void);         /* 0xD7E */
void menu_discipline(void);     /* 0xADA */
/* 0xE5A: 2-option menu; returns 0/1 (confirmed), CHOICE_CANCEL (R) or CHOICE_IDLE (timeout/event) */
enum { CHOICE_CANCEL = -1, CHOICE_IDLE = -2 };
int menu_choose_option(const u8 *tiles, u8 timeout);

/* menu_info.c: status and game */
void menu_status(void);         /* 0x814 */
void menu_game(void);           /* 0x624 */

/* life.c: evolution, death, reset, factory test, dissolve */
void ev_evolve(void);           /* 0x34B */
void ev_death(void);            /* 0x374 */
void factory_lcd_test(void);    /* 0x256 */
void screen_dissolve(void);     /* 0x906 */

void play_overlay_anim(u8 anim_set, u8 seconds, u8 script); /* 0x8A9 (anim.c) */

/* menus.c */
void menu_init(u8 limit, u8 timeout); /* 0x1EC */
int menu_poll(void);            /* 0x2C3: returns MENU_* */
enum { MENU_NONE, MENU_MOVED, MENU_CONFIRM, MENU_CANCEL, MENU_TIMEOUT };
void run_menu_item(u8 cursor);  /* 0x700 */

/* front end (front_*.c) */
void front_pump(double now_s);  /* inputs and real time */
void front_lcd_changed(void);   /* called when the LCD changes */
void front_set_buzzer(bool on, u8 freq); /* buzzer output */
extern u8 front_buttons;          /* buttons physically pressed (BTN_* bits) */
#endif
