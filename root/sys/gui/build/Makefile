CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g
LDFLAGS = -lm

# Platform implementation: choose one
# PLATFORM = platform_sdl.c    # Requires SDL2 (dev/testing)
# PLATFORM = platform_stub.c   # No dependencies (proof-of-concept)
PLATFORM = platform_stub.c

# SDL2 flags (optional, only if using platform_sdl.c)
ifeq ($(PLATFORM), platform_sdl.c)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
CFLAGS += $(SDL_CFLAGS)
LDFLAGS += $(SDL_LIBS)
endif

# Source files
SOURCES = main.c $(PLATFORM) wm.c compositor.c mouse.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = desktop

# Rules
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "✓ Built $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "✓ Cleaned"

.PHONY: all run clean
