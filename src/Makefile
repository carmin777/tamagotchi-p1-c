CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CORE := rom.c machine.c sound.c video.c anim.c screens.c events.c menus.c menu_care.c menu_info.c life.c

SDL_CFLAGS := $(shell sdl2-config --cflags) $(shell pkg-config --exists SDL2_image && echo -DHAVE_SDL_IMAGE)
SDL_LIBS := $(shell sdl2-config --libs) $(shell pkg-config --libs SDL2_image 2>/dev/null)

all: tama-headless tama

tama: $(CORE) front_sdl.c tama.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(CORE) front_sdl.c $(SDL_LIBS)

tama-headless: $(CORE) front_headless.c tama.h
	$(CC) $(CFLAGS) -o $@ $(CORE) front_headless.c

# Navegador: make -C src wasm   (precisa do Emscripten; gera web/index.html + .js/.wasm)
wasm: $(CORE) front_sdl.c tama.h ../web/shell.html
	emcc -O2 -Wall -Wno-unused-parameter -sUSE_SDL=2 -sASYNCIFY -sALLOW_MEMORY_GROWTH -lidbfs.js \
	  -sEXPORTED_FUNCTIONS=_main,_tama_button,_tama_save -sEXPORTED_RUNTIME_METHODS=callMain,FS \
	  --shell-file ../web/shell.html -o ../web/index.html $(CORE) front_sdl.c

clean:
	rm -f tama-headless tama
