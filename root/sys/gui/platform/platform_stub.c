#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

/* ==============================================================================
 * PLATFORM_STUB.C — Minimal Platform Stub
 *
 * No SDL2 dependency. Allocates a framebuffer in RAM, does nothing with it.
 * Used to prove the WM/compositor architecture compiles and links.
 * 
 * In real deployment:
 *   - Swap this out for platform_sdl.c (dev) or platform_frnt.c (bare metal)
 *   - Keep platform.h unchanged
 * ============================================================================== */

typedef struct {
    uint32_t     *framebuffer;
    int32_t      width, height;
    int32_t      pitch;
    
    time_t       init_time;
    
    fb_t         fb_desc;
} plat_state_t;

static plat_state_t g_plat = {0};

int plat_init(int32_t width, int32_t height) {
    g_plat.pitch = width * 4;
    g_plat.framebuffer = (uint32_t *)malloc(width * height * g_plat.pitch);
    if (!g_plat.framebuffer) {
        fprintf(stderr, "Platform stub: malloc failed\n");
        return 0;
    }
    
    g_plat.width = width;
    g_plat.height = height;
    g_plat.init_time = time(NULL);
    
    g_plat.fb_desc.pixels = g_plat.framebuffer;
    g_plat.fb_desc.width = width;
    g_plat.fb_desc.height = height;
    g_plat.fb_desc.pitch = g_plat.pitch;
    
    printf("Platform (stub): initialized %dx%d (framebuffer in RAM)\n", width, height);
    printf("NOTE: This stub doesn't display anything. Use platform_sdl.c for SDL2 rendering.\n");
    return 1;
}

void plat_shutdown(void) {
    if (g_plat.framebuffer) {
        free(g_plat.framebuffer);
        g_plat.framebuffer = NULL;
    }
    memset(&g_plat, 0, sizeof(g_plat));
}

const fb_t *plat_get_framebuffer(void) {
    return &g_plat.fb_desc;
}

void plat_present(const fb_t *fb) {
    /* Stub: do nothing. On real platform, upload to display. */
    (void)fb;
}

int plat_poll_event(event_t *evt) {
    /* Stub: no events. Return 0 (quit after a few frames for testing). */
    static int frame_count = 0;
    if (frame_count++ > 60) {
        evt->type = EVT_QUIT;
        return 1;
    }
    return 0;
}

uint32_t plat_get_ticks(void) {
    return (uint32_t)(time(NULL) - g_plat.init_time) * 1000;
}

void plat_delay(uint32_t ms) {
    /* Stub: don't actually sleep. */
    (void)ms;
}
