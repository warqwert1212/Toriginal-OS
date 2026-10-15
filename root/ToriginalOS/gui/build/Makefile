CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -I.. -I../wm -I../compositor -I../platform -I../image -I../desktop_env
LDFLAGS = -lm -lz

PLATFORM = platform_sdl.c

ifeq ($(PLATFORM), platform_sdl.c)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
CFLAGS += $(SDL_CFLAGS)
LDFLAGS += $(SDL_LIBS)
endif

VPATH = ..:../platform:../wm:../compositor:../image:../desktop_env

SOURCES = main.c $(PLATFORM) wm.c compositor.c image_loader.c ui_draw.c desktop_menu.c wallpaper_picker.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = desktop_env_bin

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all run clean
