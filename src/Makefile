CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CORE := rom.c machine.c sound.c video.c anim.c screens.c events.c menus.c menu_care.c menu_info.c life.c

SDL_CFLAGS := $(shell sdl2-config --cflags) $(shell pkg-config --exists SDL2_image && echo -DHAVE_SDL_IMAGE)
SDL_LIBS := $(shell sdl2-config --libs) $(shell pkg-config --libs SDL2_image 2>/dev/null)

all: tama-headless tama

tama: $(CORE) front_sdl.c tama.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) front_sdl.c $(SDL_LIBS)

tama-headless: $(CORE) front_headless.c tama.h
	$(CC) $(CFLAGS) -o $@ $(CORE) front_headless.c

clean:
	rm -f tama-headless tama
