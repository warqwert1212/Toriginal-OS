#include <stdint.h>
#include <stddef.h>

// Screen & Environment Metrics
extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch;
extern uint32_t* os_back_buffer;

// Kernel File System & Execution Hooks
extern int sys_file_exists(const char* path);
extern uint8_t* sys_read_file(const char* path);
extern void sys_draw_png(uint8_t* png_data, uint32_t x, uint32_t y, uint32_t target_w, uint32_t target_h);

// Modular Linkage to Neighboring Components
extern void startbutton_draw(uint32_t x, uint32_t y);
extern void startbutton_check_click(uint32_t mx, uint32_t my, uint32_t clicked);
extern void time_finder_draw_tray_clock(uint32_t x, uint32_t y);
extern void time_finder_check_click(uint32_t mx, uint32_t my, uint32_t clicked, uint32_t cx, uint32_t cy);

// ICO Structures required for the File002.ico fallback route
#pragma pack(push, 1)
typedef struct { uint16_t res; uint16_t type; uint16_t count; } task_ico_head_t;
typedef struct { uint8_t w; uint8_t h; uint8_t cls; uint8_t res; uint16_t pl; uint16_t bpp; uint32_t sz; uint32_t off; } task_ico_ent_t;
typedef struct { uint32_t sz; int32_t w; int32_t h; uint16_t pl; uint16_t bpp; uint32_t cp; uint32_t img_sz; } task_ico_bmp_t;
#pragma pack(pop)

// Fallback rendering fallback parser for .ico files
static void taskbar_render_fallback_ico(uint8_t* buf, uint32_t dest_x, uint32_t dest_y) {
    task_ico_head_t* head = (task_ico_head_t*)buf;
    if (head->res != 0 || head->type != 1 || head->count == 0) return;
    task_ico_ent_t* ent = (task_ico_ent_t*)(buf + sizeof(task_ico_head_t));
    task_ico_bmp_t* bmp = (task_ico_bmp_t*)(buf + ent->off);
    uint32_t* pixels = (uint32_t*)(buf + ent->off + bmp->sz);
    uint32_t iw = ent->w == 0 ? 32 : ent->w; // Limit safely to taskbar standard height bounds
    uint32_t ih = ent->h == 0 ? 32 : ent->h;
    uint32_t stride = screen_pitch / 4;

    for (uint32_t y = 0; y < ih; y++) {
        uint32_t ty = dest_y + y;
        if (ty >= screen_height) break;
        for (uint32_t x = 0; x < iw; x++) {
            uint32_t tx = dest_x + x;
            if (tx >= screen_width) continue;
            uint32_t color = pixels[(ih - 1 - y) * iw + x];
            if (((color >> 24) & 0xFF) > 32) { 
                os_back_buffer[ty * stride + tx] = color & 0x00FFFFFF;
            }
        }
    }
}

// Flat color rasterizer loop helper
static void taskbar_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            if ((x + j) < screen_width && (y + i) < screen_height) {
                os_back_buffer[(y + i) * stride + (x + j)] = color;
            }
        }
    }
}

// Asset Router Engine
static void taskbar_draw_background_and_brand(uint32_t start_y) {
    // 1. Check for modern custom UI theme element
    if (sys_file_exists("/sys/1000062376_0.png")) {
        uint8_t* png_data = sys_read_file("/sys/1000062376_0.png");
        if (png_data) {
            // Stretch/Tile the background PNG across the resolution width perfectly
            sys_draw_png(png_data, 0, start_y, screen_width, 40);
            
            // Also render it as a brand logo quick launch item next to Start button
            sys_draw_png(png_data, 75, start_y + 4, 32, 32);
            return;
        }
    }

    // 2. Failure Fallback: Draw classic steel bevel frame and load generic fallback document icon
    taskbar_fill_rect(0, start_y, screen_width, 1, 0xFFFFFF);
    taskbar_fill_rect(0, start_y + 1, screen_width, 39, 0xD4D0C8);

    if (sys_file_exists("/sys/File002.ico")) {
        uint8_t* ico_data = sys_read_file("/sys/File002.ico");
        if (ico_data) {
            taskbar_render_fallback_ico(ico_data, 75, start_y + 4);
        }
    }
}

// Global Execution Entry Hook called inside your main execution refresh loops
void taskbar_refresh_layer(uint32_t mouse_x, uint32_t mouse_y, uint32_t mouse_clicked) {
    // Dynamically lock positions strictly matching whatever layout resolution is currently active
    uint32_t start_y = screen_height - 40;
    uint32_t clock_x = screen_width - 74;
    uint32_t clock_y = start_y + 14;

    // 1. Paint underlying stretched skin context or classic box with fallback icon routing
    taskbar_draw_background_and_brand(start_y);

    // 2. Render functional layout layers over top of background base
    startbutton_draw(4, start_y + 4);
    time_finder_draw_tray_clock(clock_x, clock_y);

    // 3. Process pointer click checks across modular boundaries
    startbutton_check_click(mouse_x, mouse_y, mouse_clicked);
    time_finder_check_click(mouse_x, mouse_y, mouse_clicked, clock_x, clock_y);
}