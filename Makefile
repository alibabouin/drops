SDLCONFIG = sdl-config
CFLAGS = -O2 -Wall `$(SDLCONFIG) --cflags`
LIBS = -lSDL_image -lSDL_gfx -lSDL_ttf -lSDL_mixer `$(SDLCONFIG) --libs`

drops: drops.c
	$(CC) $(CFLAGS) -o drops drops.c $(LIBS)

clean:
	rm -rf drops *.o

