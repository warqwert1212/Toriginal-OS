#include <stdint.h>

extern uint32_t screen_height;
extern uint32_t* os_back_buffer;
extern uint32_t screen_pitch;
extern void sys_draw_char(uint32_t x, uint32_t y, char c, uint32_t color);

// Leverages the safe fallback icon resolution system inside desktop.c
extern void desktop_draw_icon(const char* filename, uint32_t x, uint32_t y);

static uint32_t menu_active = 0;
static uint32_t button_last_click_state = 0;

static void start_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            os_back_buffer[(y + i) * stride + (x + j)] = color;
        }
    }
}

static void start_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color) {
    for (int i = 0; str[i] != '\0'; i++) sys_draw_char(x + (i * 8), y, str[i], color);
}

void startbutton_draw(uint32_t x, uint32_t y) {
    uint32_t offset = menu_active ? 1 : 0;
    
    start_fill_rect(x, y, 65, 32, 0xD4D0C8);
    start_fill_rect(x, y, 65, 1, menu_active ? 0x404040 : 0xFFFFFF);
    start_fill_rect(x, y, 1, 32, menu_active ? 0x404040 : 0xFFFFFF);
    start_fill_rect(x + 64, y, 1, 32, menu_active ? 0xFFFFFF : 0x404040);
    start_fill_rect(x, y + 31, 65, 1, menu_active ? 0xFFFFFF : 0x404040);

    // Safely loaded via the system file detector fallback logic
    desktop_draw_icon("Display003.ico", x + 4 + offset, y + 4 + offset);
    start_draw_string(x + 32 + offset, y + 10 + offset, "Start", 0x000000);

    if (menu_active) {
        uint32_t mw = 160, mh = 180;
        uint32_t my = (screen_height - 40) - mh;
        
        start_fill_rect(0, my, mw, mh, 0xD4D0C8);
        start_fill_rect(0, my, mw, 1, 0xFFFFFF);
        start_fill_rect(mw - 1, my, 1, mh, 0x404040);
        start_fill_rect(0, my + mh - 1, mw, 1, 0x404040);
        start_fill_rect(2, my + 2, 18, mh - 4, 0x0A246A); 

        start_draw_string(28, my + 15,  "Programs", 0x000000);
        start_draw_string(28, my + 45,  "Settings", 0x000000);
        start_draw_string(28, my + 75,  "Find Help", 0x000000);
        start_draw_string(28, my + 105, "Run...", 0x000000);
        start_fill_rect(22, my + 135, mw - 24, 1, 0x808080);
        start_draw_string(28, my + 150, "Shut Down...", 0x000000);
    }
}

void startbutton_check_click(uint32_t mx, uint32_t my, uint32_t clicked) {
    uint32_t start_y = screen_height - 40;
    if (clicked == 1 && button_last_click_state == 0) {
        if (mx >= 4 && mx <= 69 && my >= start_y + 4 && my <= start_y + 36) {
            menu_active = !menu_active;
        } else if (menu_active) {
            uint32_t my_start = start_y - 180;
            if (mx > 160 || my < my_start || my > start_y) {
                menu_active = 0; 
            }
        }
    }
    button_last_click_state = clicked;
}