#include "platform.h"
#include "wm.h"
#include "compositor.h"
#include <stdio.h>
#include <string.h>

/* ==============================================================================
 * MAIN.C — Desktop Environment Test
 *
 * Ties together: platform (SDL2) + WM + compositor
 * Creates a test window, renders the desktop, handles input.
 * ============================================================================== */

#define SCREEN_W 1024
#define SCREEN_H 768

int main(void) {
    printf("Initializing desktop environment...\n");
    
    /* Initialize platform (SDL2 backend) */
    if (!plat_init(SCREEN_W, SCREEN_H)) {
        fprintf(stderr, "Platform init failed\n");
        return 1;
    }
    printf("Platform initialized (SDL2)\n");
    
    /* Initialize window manager */
    wm_init(SCREEN_W, SCREEN_H);
    printf("Window manager initialized\n");
    
    /* Initialize compositor */
    comp_init(SCREEN_W, SCREEN_H);
    printf("Compositor initialized\n");
    
    /* Create a test window */
    uint32_t win1 = wm_create_window(100, 100, 400, 300, "Test Window 1");
    uint32_t win2 = wm_create_window(250, 200, 400, 300, "Test Window 2");
    
    if (win1 == 0 || win2 == 0) {
        fprintf(stderr, "Failed to create test windows\n");
        return 1;
    }
    printf("Created test windows: %u, %u\n", win1, win2);
    
    /* Fill windows with test patterns */
    wm_window_t *w1 = wm_get_window(win1);
    wm_window_t *w2 = wm_get_window(win2);
    
    if (w1) {
        /* Blue gradient */
        for (int y = 0; y < w1->client_h; y++) {
            for (int x = 0; x < w1->client_w; x++) {
                uint8_t b = (uint8_t)(255 * x / w1->client_w);
                uint8_t g = (uint8_t)(255 * y / w1->client_h);
                w1->pixels[y * (w1->pitch/4) + x] = 0xFF000000 | (g << 8) | b;
            }
        }
    }
    
    if (w2) {
        /* Red gradient */
        for (int y = 0; y < w2->client_h; y++) {
            for (int x = 0; x < w2->client_w; x++) {
                uint8_t r = (uint8_t)(255 * x / w2->client_w);
                uint8_t g = (uint8_t)(255 * y / w2->client_h);
                w2->pixels[y * (w2->pitch/4) + x] = 0xFF000000 | (r << 16) | (g << 8);
            }
        }
    }
    
    printf("Test windows filled with patterns\n");
    printf("Controls: drag title bars to move, drag edges to resize, click to focus, ESC to quit\n");
    
    /* Main event loop */
    int running = 1;
    while (running) {
        event_t evt;
        
        /* Poll events */
        while (plat_poll_event(&evt)) {
            switch (evt.type) {
                case EVT_QUIT:
                    running = 0;
                    break;
                
                case EVT_KEY:
                    if (evt.key == 27) {  /* ESC */
                        running = 0;
                    }
                    wm_handle_key(evt.key, evt.is_pressed);
                    break;
                
                case EVT_MOUSE_MOVE:
                    wm_handle_mouse_move(evt.x, evt.y);
                    break;
                
                case EVT_MOUSE_DOWN:
                    wm_handle_mouse_down(evt.x, evt.y, evt.button);
                    break;
                
                case EVT_MOUSE_UP:
                    wm_handle_mouse_up(evt.x, evt.y, evt.button);
                    break;
                
                default:
                    break;
            }
        }
        
        /* Composite the scene */
        comp_composite();
        
        /* Present framebuffer */
        uint32_t *fb;
        int32_t fb_w, fb_h, fb_pitch;
        comp_get_framebuffer(&fb, &fb_w, &fb_h, &fb_pitch);
        
        fb_t plat_fb = {
            .pixels = fb,
            .width = fb_w,
            .height = fb_h,
            .pitch = fb_pitch
        };
        plat_present(&plat_fb);
        
        /* Frame timing (aim for ~60 FPS) */
        plat_delay(16);  /* 16ms per frame */
    }
    
    printf("Shutting down...\n");
    comp_shutdown();
    wm_shutdown();
    plat_shutdown();
    
    return 0;
}
