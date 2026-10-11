CFLAGS ?= -O2 -g -Wall -Wno-unused-function
SDL := $(shell sdl2-config --cflags --libs)
SRC := trace.c state.c tamalib/cpu.c tamalib/hw.c tamalib/tamalib.c

trace: $(SRC)
	$(CC) $(CFLAGS) -I. -o $@ $(SRC) $(SDL)
