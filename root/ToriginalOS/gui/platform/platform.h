#ifndef _PLATFORM_H
#define _PLATFORM_H

#include <stdint.h>

/* ==============================================================================
 * PLATFORM.H — Platform Abstraction Layer
 *
 * Abstracts hardware/SDL2 differences. Desktop environment code calls these
 * functions; platform_sdl.c implements them for development on SDL2.
 * On bare-metal freeNT, alternative implementations (platform_frnt.c) will
 * talk to the real VBE framebuffer and hardware interrupt handlers.
 * ============================================================================== */

/* ─ Framebuffer structure (ARGB8888 format) ────────────────────────────── */
typedef struct {
    uint32_t *pixels;       /* ARGB8888 pixels */
    int32_t   width;
    int32_t   height;
    int32_t   pitch;        /* bytes per row */
} fb_t;

/* ─ Event types ────────────────────────────────────────────────────────── */
typedef enum {
    EVT_NONE = 0,
    EVT_QUIT,
    EVT_KEY,
    EVT_MOUSE_MOVE,
    EVT_MOUSE_DOWN,
    EVT_MOUSE_UP,
    EVT_MOUSE_WHEEL,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        struct {
            int key;
            int is_pressed;
        };
        struct {
            int32_t x, y;
            int button;  /* 0=left, 1=right, 2=middle */
        };
        struct {
            int wheel_delta;  /* >0 = up, <0 = down */
        };
    };
} event_t;

/* ─ Platform lifecycle ────────────────────────────────────────────────── */

/* Initialize the platform. Allocates framebuffer, opens window, etc.
 * Returns 1 on success, 0 on failure. */
int plat_init(int32_t width, int32_t height);

/* Shutdown: free resources. */
void plat_shutdown(void);

/* Get the platform's current framebuffer. DO NOT MODIFY.
 * Valid only until the next plat_get_framebuffer() or plat_present() call. */
const fb_t *plat_get_framebuffer(void);

/* Present the framebuffer to the display.
 * (On SDL2, this does a texture update + render.)
 * (On bare metal, this is a no-op if using the VBE framebuffer directly.) */
void plat_present(const fb_t *fb);

/* Poll one event. Returns 1 if an event is available (fills *evt), 0 otherwise. */
int plat_poll_event(event_t *evt);

/* Get current ticks in milliseconds since plat_init(). */
uint32_t plat_get_ticks(void);

/* Delay for N milliseconds. */
void plat_delay(uint32_t ms);

#endif /* _PLATFORM_H */
