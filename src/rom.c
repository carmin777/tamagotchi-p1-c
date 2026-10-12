/* Leitura da ROM original e das suas tabelas lbpx/retd. */
#include <stdio.h>
#include <stdlib.h>
#include "tama.h"

#define ROM_WORDS 0x1800

static u16 rom[ROM_WORDS];

bool rom_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	u8 b[2];
	int i;

	if (!f) return false;
	for (i = 0; i < ROM_WORDS && fread(b, 2, 1, f) == 1; i++)
		rom[i] = b[1] | ((b[0] & 0xF) << 8);
	fclose(f);
	return i == ROM_WORDS;
}

u16 rom_word(u16 addr) { return rom[addr % ROM_WORDS]; }

static bool is_lbpx(u16 w) { return (w & 0xF00) == 0x900; }
static bool is_retd(u16 w) { return (w & 0xF00) == 0x100; }

u8 rom_byte(u16 addr)
{
	u16 w = rom_word(addr);

	if (!is_lbpx(w) && !is_retd(w)) {
		fprintf(stderr, "rom: palavra 0x%03X em 0x%04X não é dado\n", w, addr);
		abort();
	}
	return w & 0xFF;
}

int rom_table(u16 addr, u8 *out, int max)
{
	int n = 0;

	for (;;) {
		u16 w = rom_word(addr++);
		if (!is_lbpx(w) && !is_retd(w)) {
			fprintf(stderr, "rom: tabela em 0x%04X tem palavra 0x%03X\n", addr - 1, w);
			abort();
		}
		if (n < max) out[n] = w & 0xFF;
		n++;
		if (is_retd(w)) return n;
	}
}
