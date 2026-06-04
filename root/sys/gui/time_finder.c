#include <stdint.h>

// Kernel RTC Read/Write Hardware Interfaces
extern void sys_get_rtc_time(uint8_t* hour, uint8_t* min, uint8_t* sec);
extern void sys_set_rtc_time(uint8_t hour, uint8_t min, uint8_t sec); // Writes back to BIOS/CMOS

extern void sys_draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
extern uint32_t* os_back_buffer;
extern uint32_t screen_pitch;

static uint32_t clock_last_click_state = 0;
static uint32_t adjust_mode = 0; // 0 = Normal, 1 = Editing Hours, 2 = Editing Minutes

// Internal cached time registers
static uint8_t clock_h = 0;
static uint8_t clock_m = 0;
static uint8_t clock_s = 0;

static void clock_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            os_back_buffer[(y + i) * stride + (x + j)] = color;
        }
    }
}

// BCD Unpack Helper to handle hardware-encoded values safely
static uint8_t bcd_to_bin(uint8_t val) {
    if ((val & 0x10) || (val & 0x0F) > 9) {
        return ((val >> 4) * 10) + (val & 0x0F);
    }
    return val;
}

// Bin to BCD Helper required for raw CMOS/BIOS writes
static uint8_t bin_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

void time_finder_draw_tray_clock(uint32_t x, uint32_t y) {
    // If we aren't actively modifying the time, stream directly from the BIOS hardware settings
    if (adjust_mode == 0) {
        uint8_t raw_h = 0, raw_m = 0, raw_s = 0;
        sys_get_rtc_time(&raw_h, &raw_m, &raw_s);
        clock_h = bcd_to_bin(raw_h);
        clock_m = bcd_to_bin(raw_m);
        clock_s = bcd_to_bin(raw_s);
    }

    char time_string[9] = "00:00:00";
    time_string[0] = '0' + (clock_h / 10); time_string[1] = '0' + (clock_h % 10);
    time_string[3] = '0' + (clock_m / 10); time_string[4] = '0' + (clock_m % 10);
    time_string[6] = '0' + (clock_s / 10); time_string[7] = '0' + (clock_s % 10);

    // Draw standard inset container frame
    clock_fill_rect(x - 6, y - 4, 76, 20, 0x808080);
    clock_fill_rect(x - 5, y - 3, 75, 19, 0xFFFFFF);
    clock_fill_rect(x - 5, y - 3, 74, 18, 0xD4D0C8);

    // Highlight the selection visual text strings if the user enters adjustment mode
    uint32_t text_color_h = (adjust_mode == 1) ? 0x0A246A : 0x000000;
    uint32_t text_color_m = (adjust_mode == 2) ? 0x0A246A : 0x000000;

    // Draw Hours
    sys_draw_char(x + 0,  y, time_string[0], text_color_h);
    sys_draw_char(x + 8,  y, time_string[1], text_color_h);
    sys_draw_char(x + 16, y, ':', 0x000000);
    
    // Draw Minutes
    sys_draw_char(x + 24, y, time_string[3], text_color_m);
    sys_draw_char(x + 32, y, time_string[4], text_color_m);
    sys_draw_char(x + 40, y, ':', 0x000000);
    
    // Draw Seconds
    sys_draw_char(x + 48, y, time_string[6], 0x000000);
    sys_draw_char(x + 56, y, time_string[7], 0x000000);
}

void time_finder_check_click(uint32_t mx, uint32_t my, uint32_t clicked, uint32_t clock_x, uint32_t clock_y) {
    if (clicked == 1 && clock_last_click_state == 0) {
        // Evaluate click bounds against the tray coordinate positions calculated by taskbar.c
        if (mx >= (clock_x - 6) && mx <= (clock_x + 70) && my >= (clock_y - 4) && my <= (clock_y + 16)) {
            
            if (adjust_mode == 0) {
                // First click: lock the time tracking loop and select Hours
                adjust_mode = 1;
            } else if (adjust_mode == 1) {
                // Second click: advance focus to Minutes
                adjust_mode = 2;
            } else {
                // Third click: Commit and flash edited properties back to BIOS system architecture
                sys_set_rtc_time(bin_to_bcd(clock_h), bin_to_bcd(clock_m), bin_to_bcd(clock_s));
                adjust_mode = 0; // Return to tracking live hardware updates
            }
        } else if (adjust_mode != 0) {
            // User clicked elsewhere outside the clock box: context change shortcut interface
            if (adjust_mode == 1) {
                clock_h = (clock_h + 1) % 24; // Increment Hours
            } else if (adjust_mode == 2) {
                clock_m = (clock_m + 1) % 60; // Increment Minutes
            }
        }
    }
    clock_last_click_state = clicked;
}