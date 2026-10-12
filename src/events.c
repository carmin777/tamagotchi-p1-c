/*
 * Care rules: initial state, per-species parameters, minute tick (0xC7A),
 * event detection (0xF00) and its handlers (0x307).
 */
#include "tama.h"


/* 0x761 label_232 */
void init_new_game_state(void)
{
	cost_ms(16.5);
	g.species = 0;
	g.anim_set = 1;
	g.food_choice = 0;
	g.hunger = 1;
	g.happy = 1;
	g.care_mistakes = 0;
	g.discipline = 0;
	g.weight = 0x05;
	g.age = 0;
	g.sick = 0; g.sick_count = 0; g.sleep = 0; g.light = 0xF;
	g.m4c = 0; g.poop = 0; g.lifespan_over = 0; g.lifespan_strikes = 0;
	g.stage = 0; g.discipline_misses = 0;
	g.event = 0;
	g.hunger_timer = 0x00;
	g.happy_timer = 0x02;
	g.sleep_minutes = 0xFF;
	g.poop_timer = 0x0F;
	g.call_hunger = 0;
	g.call_discipline = 0;
	g.empty_heart_min = 0;
	g.sick_timer = (g.sick_timer & 0x0FF) | 0xF00;
	g.stage_timer = (g.stage_timer & 0x0FF) | 0xF00;
	g.nap = 5;
	g.baby_awake = 0x28;
}

/* 0x6E5 label_214: keeps the weight within the species' [min, max) */
static void clamp_weight(void)
{
	if (g.weight < g.p.weight_min) g.weight = g.p.weight_min;
	else if (g.weight >= g.p.weight_max) g.weight = g.p.weight_max;
}

/* 0xDD2 label_347: loads the species' 13-byte block (ROM 0x1564+) and restarts the
 * stage counters */
void load_species_params(void)
{
	u8 b[13];
	u16 jp = rom_word(0x1558 + g.species);        /* jp to the block, in page 0x15 */
	int i;

	cost_ms(40);
	g.sick_count = 0;
	rom_table(0x1500 | (jp & 0xFF), b, 13);
	for (i = 0; i < 13; i++) {
		g.p.raw[2 * i] = b[i] & 0xF;
		g.p.raw[2 * i + 1] = b[i] >> 4;
	}
	g.p.wake_hour = b[0];
	g.p.bed_hour = b[1];
	g.p.hunger_interval = b[2];
	g.p.happy_interval = b[3];
	g.p.sick_reload = b[4] | (b[5] & 0xF) << 8;
	g.p.medicine_doses = b[5] >> 4;
	g.p.weight_min = b[6];
	g.p.weight_max = b[7];
	g.p.stage_minutes = b[8] | (b[9] & 0xF) << 8;
	g.p.call_reload = b[9] >> 4;
	g.p.game_win_chance = b[10] & 0xF;
	g.p.game_param_245 = b[10] >> 4;
	g.p.weight_start = b[11];
	g.p.flag_248 = b[12] & 0xF;
	g.p.param_249 = b[12] >> 4;

	g.stage_timer = g.p.stage_minutes;
	if (!(g.sick & 8)) g.sick_timer = g.p.sick_reload;
	g.call_q213 = g.p.call_reload;
	g.discipline = g.p.game_win_chance;          /* M[0x043] = Q[0x244] */
	if (g.stage == 2 || g.stage == 4) g.discipline = 8;
	clamp_weight();
}

/* ---------------------------------------------------------------- 0xC7A minute tick */

static u8 inc_nz_sat4(u8 v) { return (v != 0 && v != 0xF) ? v + 1 : v; }

void minute_tick(void)
{
	if (g.egg == 0) return;
	if (g.egg != 0xF) { g.egg--; return; }       /* egg countdown */
	if (g.sleep & 8) {
		if (g.species == 1) { if (g.nap) g.nap--; }
		else if (g.sleep_minutes != 0xFF) g.sleep_minutes++;
		return;
	}
	if (g.species == 1 && g.baby_awake) g.baby_awake--;
	if (g.hunger_timer) g.hunger_timer--;
	if (g.happy_timer) g.happy_timer--;
	g.call_hunger = inc_nz_sat4(g.call_hunger);
	g.call_discipline = inc_nz_sat4(g.call_discipline);
	if (g.poop_timer) g.poop_timer--;
	if (g.hunger == 0 || g.happy == 0) g.empty_heart_min = (g.empty_heart_min + 1) & 0xFFF;
	else g.empty_heart_min = 0;
	if (!(g.sick & 8)) { if (g.sick_timer) g.sick_timer--; }
	else g.sick_timer = (g.sick_timer + 1) & 0xFFF;
	if (g.stage_timer) g.stage_timer--;
}

/* ---------------------------------------------------------------- 0xF00 check_events */

static u8 sat_inc4(u8 v) { return v == 0xF ? v : v + 1; }

static void add_care_mistake(void)         /* 0xFDF */
{
	g.care_mistakes = sat_inc4(g.care_mistakes);
	if (g.stage == 0xF && g.stage_timer == 0) g.lifespan_strikes = sat_inc4(g.lifespan_strikes);
}

static u8 shrink_25pct(u8 v) { return v - (v >> 2); }   /* 0xF3D */

static bool set_event(u8 e) { g.event = e; return true; }

bool check_events(void)
{
	u8 hour;

	if (g.egg != 0xF || g.event != 0) { cost_ms(2.2); return g.event != 0; }
	/* The cost (~67 ms of CPU) is spread over the sections, in the original's order: the minute
	 * tick (interrupt) may land in the middle and only the following sections see the effect. */
	cost_ms(8);
	hour = g.hour;
	if (g.sleep & 8) {                                    /* sleeping */
		bool wake;
		if (g.species == 1) wake = (g.nap == 0);
		else wake = hour >= g.p.wake_hour && hour < g.p.bed_hour;
		if (wake) {
			g.light = 0xF;
			g.sleep = 0;
			/* age in days (BCD), saturates at 99 */
			if (g.age != 0x99) g.age = ((g.age & 0xF) == 9) ? (g.age & 0xF0) + 0x10 : g.age + 1;
			if (g.stage == 0xF && g.stage_timer == 0) {
				g.p.hunger_interval = shrink_25pct(g.p.hunger_interval);
				g.p.happy_interval = shrink_25pct(g.p.happy_interval);
			}
			return false;
		}
		if (NIB(g.sleep_minutes, 1) != 0xF && NIB(g.sleep_minutes, 1) >= 4) {
			g.sleep_minutes = 0xF0 | (g.sleep_minutes & 0xF);
			add_care_mistake();                           /* light was left on */
		}
		return false;
	}
	cost_ms(6);
	/* bedtime? */
	if (g.species == 1) {
		if (g.nap != 0 && g.baby_awake == 0) return set_event(1);
	} else if (hour >= g.p.bed_hour || hour < g.p.wake_hour) {
		return set_event(1);
	}
	/* death (nibble-wise comparisons, like the original) */
	cost_ms(14);
	{
		bool dead = g.sick_count >= 3;
		if (g.species != 1) {
			if (NIB(g.empty_heart_min, 2) >= 2 && NIB(g.empty_heart_min, 1) >= 0xD) dead = true;
			if ((g.sick & 8) && NIB(g.sick_timer, 2) >= 1 && NIB(g.sick_timer, 1) >= 6) dead = true;
		}
		if (g.lifespan_strikes >= 5) dead = true;
		if (dead) return set_event(6);
	}
	cost_ms(8);
	if (g.stage_timer == 0) {
		if (g.stage < 0xF) return set_event(5);
		g.lifespan_over = 0xF;
	}
	if (g.poop_timer == 0) return set_event(4);
	if (!(g.sick & 8) && g.sick_timer == 0) { g.sick = 0xF; g.sick_count++; }
	cost_ms(8);
	if (g.hunger_timer == 0) {
		g.hunger_timer = g.p.hunger_interval;
		if (g.hunger >= 4) { g.hunger -= 4; if (g.call_q213) g.call_q213--; }
		else { g.hunger = 0; return set_event(2); }
	}
	cost_ms(8);
	if (g.happy_timer == 0) {
		g.happy_timer = g.p.happy_interval;
		if (g.happy >= 4) { g.happy -= 4; if (g.call_q213) g.call_q213--; }
		else { g.happy = 0; return set_event(2); }
	}
	cost_ms(7);
	if (g.call_q213 == 0) {
		unsigned s = g.call_q214 + g.discipline + 1;
		g.call_q213 = g.p.call_reload;
		g.call_q214 = s & 0xF;
		if (s <= 0xF && g.species != 1) return set_event(3);
	}
	cost_ms(5);
	if (g.call_hunger == 0xF) { g.call_hunger = 0; if (g.species != 1) add_care_mistake(); }
	cost_ms(3);
	if (g.call_discipline == 0xF) { g.call_discipline = 0; g.discipline_misses = sat_inc4(g.discipline_misses); }
	return false;
}

/* ---------------------------------------------------------------- 0x307 events */

static void ev_bedtime(void)                 /* 0x30C */
{
	g.sleep = 0xF;
	g.sleep_minutes = (g.species == 1) ? 0x3D : 0x00;
	play_sound(SND_CALL, 8, 0);
}

static void ev_call(bool discipline)         /* 0x31C / 0x324 */
{
	if (discipline) g.call_discipline = 1;
	else g.call_hunger = (g.species == 1) ? 0xD : 1;
	play_sound(SND_CALL, 8, 0);
}

static void ev_poop(void)                    /* 0x329: poops */
{
	u8 saved = g.light;
	g.light = 0xF;
	play_overlay_anim(4, 8, 2);
	g.light = saved;
	g.poop_timer = (g.species == 1) ? 0x19 : 0xB4;
	if (++g.poop >= 8) {
		g.poop = 8;
		if (!(g.sick & 8)) g.sick_timer = 0;
	}
}

void dispatch_event(void)
{
	u8 e = g.event;

	cost_ms(3.8);
	g.event = 0;
	switch (e) {
	case 1: ev_bedtime(); break;
	case 2: ev_call(false); break;
	case 3: ev_call(true); break;
	case 4: ev_poop(); break;
	case 5: ev_evolve(); break;
	case 6: ev_death(); break;
	}
}

/* 0xFEC label_391: the egg hatches */
void hatch_egg(void)
{
	g.egg = 0xF;
	play_sound(SND_HATCH, 6, 0);
	play_overlay_anim(2, 4, 0);
	g.skippable = 0xF;
	g.species = 1;
	g.anim_set = 1;
	reset_pet_animation();
	load_species_params();
}
