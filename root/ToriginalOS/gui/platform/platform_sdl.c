#include "platform.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    
    uint32_t     *framebuffer;  /* son of a fucking retarded fucking cunt dick fucker no ones reading this any way.*/
    int32_t      width, height;
    int32_t      pitch;
    
    uint32_t     init_ticks;    /* SDL_GetTicks() at init time */
    
    fb_t         fb_desc;       /* Returned by plat_get_framebuffer() */
} plat_state_t;

static plat_state_t g_plat = {0};

int plat_init(int32_t width, int32_t height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    
    g_plat.window = SDL_CreateWindow(
        "freeNT Desktop",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN
    );
    if (!g_plat.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    
    g_plat.renderer = SDL_CreateRenderer(g_plat.window, -1, 0);
    if (!g_plat.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_plat.window);
        SDL_Quit();
        return 0;
    }
    
    /* Create texture for our framebuffer (ARGB8888) */
    g_plat.texture = SDL_CreateTexture(
        g_plat.renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
    if (!g_plat.texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g_plat.renderer);
        SDL_DestroyWindow(g_plat.window);
        SDL_Quit();
        return 0;
    }
    
    /* Allocate our framebuffer (the compositor writes here) */
    g_plat.pitch = width * 4;  /* ARGB8888 = 4 bytes per pixel */
    g_plat.framebuffer = (uint32_t *)malloc((size_t)height * g_plat.pitch);
    if (!g_plat.framebuffer) {
        fprintf(stderr, "malloc framebuffer failed\n");
        SDL_DestroyTexture(g_plat.texture);
        SDL_DestroyRenderer(g_plat.renderer);
        SDL_DestroyWindow(g_plat.window);
        SDL_Quit();
        return 0;
    }
    
    g_plat.width = width;
    g_plat.height = height;
    g_plat.init_ticks = SDL_GetTicks();
    
    /* Fill fb_desc */
    g_plat.fb_desc.pixels = g_plat.framebuffer;
    g_plat.fb_desc.width = width;
    g_plat.fb_desc.height = height;
    g_plat.fb_desc.pitch = g_plat.pitch;
    
    printf("Platform (SDL2): initialized %dx%d\n", width, height);
    return 1;
}

void plat_shutdown(void) {
    if (g_plat.framebuffer) {
        free(g_plat.framebuffer);
        g_plat.framebuffer = NULL;
    }
    if (g_plat.texture) {
        SDL_DestroyTexture(g_plat.texture);
        g_plat.texture = NULL;
    }
    if (g_plat.renderer) {
        SDL_DestroyRenderer(g_plat.renderer);
        g_plat.renderer = NULL;
    }
    if (g_plat.window) {
        SDL_DestroyWindow(g_plat.window);
        g_plat.window = NULL;
    }
    SDL_Quit();
}

const fb_t *plat_get_framebuffer(void) {
    return &g_plat.fb_desc;
}

void plat_present(const fb_t *fb) {
    if (!g_plat.texture || !g_plat.renderer || !g_plat.window) {
        return;
    }
    
    /* Update texture from framebuffer */
    SDL_UpdateTexture(g_plat.texture, NULL, fb->pixels, fb->pitch);
    
    /* Clear and render */
    SDL_SetRenderDrawColor(g_plat.renderer, 0, 0, 0, 0xFF);
    SDL_RenderClear(g_plat.renderer);
    SDL_RenderCopy(g_plat.renderer, g_plat.texture, NULL, NULL);
    SDL_RenderPresent(g_plat.renderer);
}

int plat_poll_event(event_t *evt) {
    SDL_Event sdl_evt;
    
    while (SDL_PollEvent(&sdl_evt)) {
        evt->type = EVT_NONE;
        
        switch (sdl_evt.type) {
            case SDL_QUIT:
                evt->type = EVT_QUIT;
                return 1;
            
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                evt->type = EVT_KEY;
                evt->key = sdl_evt.key.keysym.sym;
                evt->is_pressed = (sdl_evt.type == SDL_KEYDOWN) ? 1 : 0;
                return 1;
            
            case SDL_MOUSEMOTION:
                evt->type = EVT_MOUSE_MOVE;
                evt->x = sdl_evt.motion.x;
                evt->y = sdl_evt.motion.y;
                return 1;
            
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int btn;
                switch (sdl_evt.button.button) {
                    case SDL_BUTTON_LEFT:   btn = 0; break;
                    case SDL_BUTTON_RIGHT:  btn = 1; break;
                    case SDL_BUTTON_MIDDLE: btn = 2; break;
                    default: btn = -1; break;
                }
                if (btn >= 0) {
                    evt->type = (sdl_evt.type == SDL_MOUSEBUTTONDOWN) ?
                                EVT_MOUSE_DOWN : EVT_MOUSE_UP;
                    evt->x = sdl_evt.button.x;
                    evt->y = sdl_evt.button.y;
                    evt->button = btn;
                    return 1;
                }
                break;
            }
            
            case SDL_MOUSEWHEEL:
                evt->type = EVT_MOUSE_WHEEL;
                evt->wheel_delta = sdl_evt.wheel.y;
                return 1;
            
            default:
                break;
        }
    }
    
    return 0;  /* No more events */
}

uint32_t plat_get_ticks(void) {
    return SDL_GetTicks() - g_plat.init_ticks;
}

void plat_delay(uint32_t ms) {
    SDL_Delay(ms);
}
