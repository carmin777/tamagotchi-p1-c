CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CORE := rom.c machine.c sound.c video.c anim.c screens.c events.c menus.c menu_care.c menu_info.c life.c

all: tama-headless

tama-headless: $(CORE) front_headless.c tama.h
	$(CC) $(CFLAGS) -o $@ $(CORE) front_headless.c

clean:
	rm -f tama-headless tama
