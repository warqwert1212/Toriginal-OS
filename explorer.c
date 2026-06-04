#include <stdint.h>
#include <stddef.h>

// Shared OS Environment Metrics
extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch;
extern uint32_t* os_back_buffer;

// Input States
extern uint32_t global_mouse_x;
extern uint32_t global_mouse_y;
extern uint32_t global_mouse_clicked;

// Kernel Calls
extern void sys_draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
extern int sys_file_exists(const char* path);
extern uint8_t* sys_read_file(const char* path);
extern void sys_exit_application(void);
extern void desktop_draw_icon(const char* filename, uint32_t x, uint32_t y);

// Application Window Properties (Windows 7 Standard Dimensions)
static uint32_t win_x = 80;
static uint32_t win_y = 60;
static uint32_t win_w = 640;
static uint32_t win_h = 420;
static uint32_t app_running = 1;
static uint32_t exp_last_click = 0;

// Flat color rasterizer
static void exp_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            if ((x + j) < screen_width && (y + i) < screen_height) {
                os_back_buffer[(y + i) * stride + (x + j)] = color;
            }
        }
    }
}

// Text renderer
static void exp_print(uint32_t x, uint32_t y, const char* str, uint32_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        sys_draw_char(x + (i * 8), y, str[i], color);
    }
}

// Custom Icon Loader with explicit Display001 -> File002 Fallback logic
static void exp_draw_safe_icon(const char* primary_ico, const char* fallback_ico, uint32_t x, uint32_t y) {
    char path[64] = "/sys/";
    int i = 5, j = 0;
    while (primary_ico[j] != '\0' && i < 63) { path[i++] = primary_ico[j++]; }
    path[i] = '\0';

    if (sys_file_exists(path)) {
        desktop_draw_icon(primary_ico, x, y); // Safe to load primary
    } else {
        desktop_draw_icon(fallback_ico, x, y); // Force fallback if missing
    }
}

// ---------------------------------------------------------
// WINDOWS 7 LAYOUT ENGINE
// ---------------------------------------------------------

static void explorer_draw_layout(void) {
    // 1. Base Window Frame & Border
    exp_fill_rect(win_x, win_y, win_w, win_h, 0xF0F0F0); // Win7 Light Gray Base
    exp_fill_rect(win_x, win_y, win_w, 1, 0xA0A0A0);
    exp_fill_rect(win_x, win_y, 1, win_h, 0xA0A0A0);
    exp_fill_rect(win_x + win_w - 1, win_y, 1, win_h, 0xA0A0A0);
    exp_fill_rect(win_x, win_y + win_h - 1, win_w, 1, 0xA0A0A0);

    // 2. Windows 7 Basic Title Bar
    exp_fill_rect(win_x + 1, win_y + 1, win_w - 2, 28, 0x99B4D1); 
    exp_print(win_x + 10, win_y + 8, "Computer", 0x000000);

    // Close Window [X] Button (Win7 Style Red/Standard)
    uint32_t bx = win_x + win_w - 45;
    uint32_t by = win_y + 1;
    exp_fill_rect(bx, by, 44, 20, 0xC04040);
    exp_fill_rect(bx, by, 1, 20, 0xFFFFFF);
    exp_print(bx + 18, by + 6, "X", 0xFFFFFF);

    // 3. Command & Address Bar Area (Top Ribbon Area)
    uint32_t cmd_y = win_y + 29;
    exp_fill_rect(win_x + 1, cmd_y, win_w - 2, 40, 0xF5F6F7); // Ribbon bg
    
    // Back/Forward buttons (approximated)
    exp_fill_rect(win_x + 10, cmd_y + 8, 24, 24, 0xD9D9D9);
    exp_print(win_x + 18, cmd_y + 12, "<", 0x000000);
    exp_fill_rect(win_x + 40, cmd_y + 8, 24, 24, 0xD9D9D9);
    exp_print(win_x + 48, cmd_y + 12, ">", 0x000000);

    // Address Bar
    exp_fill_rect(win_x + 75, cmd_y + 8, win_w - 250, 24, 0xFFFFFF);
    exp_fill_rect(win_x + 75, cmd_y + 8, win_w - 250, 1, 0xA0A0A0); // Inner shadow
    exp_print(win_x + 82, cmd_y + 12, "Computer > Local Disk (C:)", 0x000000);

    // Search Box
    uint32_t search_x = win_x + win_w - 165;
    exp_fill_rect(search_x, cmd_y + 8, 150, 24, 0xFFFFFF);
    exp_fill_rect(search_x, cmd_y + 8, 150, 1, 0xA0A0A0);
    exp_print(search_x + 8, cmd_y + 12, "Search...", 0x808080);

    // 4. Navigation Pane (Left Sidebar)
    uint32_t nav_y = cmd_y + 40;
    uint32_t nav_w = 160;
    uint32_t content_h = win_h - 29 - 40 - 25; // Window - Title - CmdBar - StatusBar
    exp_fill_rect(win_x + 1, nav_y, nav_w, content_h, 0xFFFFFF);
    exp_fill_rect(win_x + nav_w, nav_y, 1, content_h, 0xD9D9D9); // Divider

    // Nav Pane Items
    exp_print(win_x + 10, nav_y + 15, "* Favorites", 0x404040);
    exp_print(win_x + 20, nav_y + 35, "Desktop", 0x000000);
    exp_print(win_x + 20, nav_y + 55, "Downloads", 0x000000);
    
    exp_print(win_x + 10, nav_y + 85, "# Libraries", 0x404040);
    exp_print(win_x + 20, nav_y + 105, "Documents", 0x000000);
    exp_print(win_x + 20, nav_y + 125, "Pictures", 0x000000);
    
    exp_print(win_x + 10, nav_y + 155, "> Computer", 0x000000); // Active
    exp_fill_rect(win_x + 2, nav_y + 152, nav_w - 4, 18, 0xD9EBF9); // Win7 Selection highlight
    exp_print(win_x + 10, nav_y + 157, "> Computer", 0x000000);
    exp_print(win_x + 20, nav_y + 175, "Local Disk (C:)", 0x000000);

    // 5. Main Content Pane (Right View)
    uint32_t view_x = win_x + nav_w + 1;
    uint32_t view_w = win_w - nav_w - 2;
    exp_fill_rect(view_x, nav_y, view_w, content_h, 0xFFFFFF);

    // Content Headers (Hard Disk Drives)
    exp_print(view_x + 15, nav_y + 15, "Hard Disk Drives (1)", 0x003399); // Win7 Blue Header
    exp_fill_rect(view_x + 15, nav_y + 30, view_w - 30, 1, 0xD9D9D9); // Header Line

    // Draw the requested Icon with Fallback to File002.ico
    exp_draw_safe_icon("Display001.ico", "File002.ico", view_x + 20, nav_y + 45);

    // Drive Details
    exp_print(view_x + 65, nav_y + 50, "Local Disk (C:)", 0x000000);
    
    // Fake Storage Bar (Win7 style blue storage meter)
    exp_fill_rect(view_x + 65, nav_y + 65, 150, 12, 0xE6E6E6); // Bar BG
    exp_fill_rect(view_x + 65, nav_y + 65, 90, 12, 0x00A300); // Bar Fill (Green/Blue)
    exp_print(view_x + 65, nav_y + 82, "42.1 GB free of 120 GB", 0x404040);

    // 6. Details / Status Pane (Bottom Bar)
    uint32_t status_y = win_y + win_h - 25;
    exp_fill_rect(win_x + 1, status_y, win_w - 2, 24, 0xF0F4F9); // Win7 Light Blue details pane
    exp_fill_rect(win_x + 1, status_y, win_w - 2, 1, 0xA0A0A0); // Top Border
    exp_print(win_x + 10, status_y + 7, "1 item", 0x404040);
}

static void explorer_check_inputs(void) {
    if (global_mouse_clicked == 1 && exp_last_click == 0) {
        // Intercept close [X] button click
        uint32_t bx = win_x + win_w - 45;
        uint32_t by = win_y + 1;
        if (global_mouse_x >= bx && global_mouse_x <= bx + 44 &&
            global_mouse_y >= by && global_mouse_y <= by + 20) {
            app_running = 0;
            sys_exit_application(); // Tell kernel to kill process
            return;
        }
    }
    exp_last_click = global_mouse_clicked;
}

// Master execution entry hook for explorer.exe
void main_explorer_executable(void) {
    app_running = 1;

    while (app_running) {
        // Paint the Windows 7 File Explorer Layout
        explorer_draw_layout();
        
        // Listen for user clicks (Close window, etc.)
        explorer_check_inputs();
    }
}