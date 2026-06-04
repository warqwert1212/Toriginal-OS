#include <stdint.h>
#include <stddef.h>

extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_pitch; 
extern uint32_t global_mouse_x;
extern uint32_t global_mouse_y;
extern uint32_t global_mouse_clicked;

static uint32_t last_click_state = 0;
extern uint32_t* os_back_buffer;   

extern int sys_file_exists(const char* path);
extern uint8_t* sys_read_file(const char* path);
extern void sys_execute_program(const char* path); 
extern void sys_draw_char(uint32_t x, uint32_t y, char c, uint32_t color);

// Core Authentication & User System Integration
typedef struct {
    char username[32];
    char password[32];
    int timezone_offset;
    uint32_t ui_accent_color;
    int complex_setup_completed;
} user_config_t;

extern user_config_t current_user;

typedef struct {
    const char* label;
    const char* filename;
    uint32_t grid_x;
    uint32_t grid_y;
    const char* exe_target;
} desktop_shortcut_t;

#define MAX_DESKTOP_ITEMS 24
static desktop_shortcut_t desktop_grid[MAX_DESKTOP_ITEMS];
static uint32_t desktop_grid_count = 0;
static char string_pool[1024];

#pragma pack(push, 1)
typedef struct { uint16_t res; uint16_t type; uint16_t count; } ico_header_t;
typedef struct { uint8_t w; uint8_t h; uint8_t cls; uint8_t res; uint16_t pl; uint16_t bpp; uint32_t sz; uint32_t off; } ico_entry_t;
typedef struct { uint32_t sz; int32_t w; int32_t h; uint16_t pl; uint16_t bpp; uint32_t cp; uint32_t img_sz; } ico_bmp_t;
#pragma pack(pop)

static void desktop_render_ico(uint8_t* buf, uint32_t dest_x, uint32_t dest_y) {
    ico_header_t* head = (ico_header_t*)buf;
    if (head->res != 0 || head->type != 1 || head->count == 0) return;
    ico_entry_t* ent = (ico_entry_t*)(buf + sizeof(ico_header_t));
    ico_bmp_t* bmp = (ico_bmp_t*)(buf + ent->off);
    uint32_t* pixels = (uint32_t*)(buf + ent->off + bmp->sz);
    uint32_t iw = ent->w == 0 ? 256 : ent->w;
    uint32_t ih = ent->h == 0 ? 256 : ent->h;
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

void desktop_draw_icon(const char* filename, uint32_t x, uint32_t y) {
    char path[64] = "/sys/";
    int i = 5, j = 0;
    while (filename[j] != '\0' && i < 63) { path[i++] = filename[j++]; }
    path[i] = '\0';

    uint8_t* data = NULL;
    if (sys_file_exists(path)) {
        data = sys_read_file(path);
    }
    
    if (!data) {
        data = sys_read_file("/sys/File002.ico");
    }
    
    if (data) {
        desktop_render_ico(data, x, y);
    }
}

void desktop_load_configuration(void) {
    uint8_t* file_data = sys_read_file("/sys/desktop.cfg");
    if (!file_data) return;
    desktop_grid_count = 0;
    uint32_t cx = 0, cy = 0;
    uint32_t row_limit = (screen_height - 50) / 80; 

    char* ptr = (char*)file_data;
    char* pool_ptr = string_pool;
    char* pool_end = string_pool + sizeof(string_pool) - 1;

    while (*ptr != '\0' && desktop_grid_count < MAX_DESKTOP_ITEMS) {
        while (*ptr == '\n' || *ptr == '\r' || *ptr == ' ') ptr++;
        if (*ptr == '\0') break;
        desktop_shortcut_t* item = &desktop_grid[desktop_grid_count];
        
        item->label = pool_ptr;
        while (*ptr != '\0' && *ptr != '|' && *ptr != '\n' && pool_ptr < pool_end) *pool_ptr++ = *ptr++;
        *pool_ptr++ = '\0'; if (*ptr == '|') ptr++;

        item->filename = pool_ptr;
        while (*ptr != '\0' && *ptr != '|' && *ptr != '\n' && pool_ptr < pool_end) *pool_ptr++ = *ptr++;
        *pool_ptr++ = '\0'; if (*ptr == '|') ptr++;

        item->exe_target = pool_ptr;
        while (*ptr != '\0' && *ptr != '\n' && *ptr != '\r' && pool_ptr < pool_end) *pool_ptr++ = *ptr++;
        *pool_ptr++ = '\0';

        if (sys_file_exists(item->exe_target)) {
            item->grid_x = cx; item->grid_y = cy;
            desktop_grid_count++;
            if (++cy >= row_limit) { cy = 0; cx++; }
        } else {
            pool_ptr = (char*)item->label;
        }
    }
}

void desktop_draw_wallpaper(void) {
    uint32_t stride = screen_pitch / 4;
    
    // Fallback theme baseline base color options derived directly from chosen system accent profile
    uint32_t theme_base_r = (current_user.ui_accent_color >> 16) & 0xFF;
    uint32_t theme_base_g = (current_user.ui_accent_color >> 8) & 0xFF;
    uint32_t theme_base_b = current_user.ui_accent_color & 0xFF;

    for (uint32_t x = 0; x < screen_width; x++) {
        uint32_t r = (theme_base_r * x) / screen_width;
        uint32_t g = (theme_base_g * x) / screen_width;
        uint32_t b = (theme_base_b * x) / screen_width;
        uint32_t color = (r << 16) | (g << 8) | b;
        for (uint32_t y = 0; y < screen_height; y++) os_back_buffer[y * stride + x] = color;
    }
}

void desktop_process_workspace_clicks(void) {
    if (global_mouse_clicked == 1 && last_click_state == 0 && global_mouse_y < (screen_height - 40)) {
        for (uint32_t i = 0; i < desktop_grid_count; i++) {
            uint32_t ix = 30 + (desktop_grid[i].grid_x * 90);
            uint32_t iy = 30 + (desktop_grid[i].grid_y * 80);
            if (global_mouse_x >= ix && global_mouse_x <= ix + 45 && global_mouse_y >= iy && global_mouse_y <= iy + 45) {
                sys_execute_program(desktop_grid[i].exe_target);
                break;
            }
        }
    }
    last_click_state = global_mouse_clicked;
}

void desktop_refresh(void) {
    desktop_draw_wallpaper();
    
    // Draw default shortcut item arrays
    for (uint32_t i = 0; i < desktop_grid_count; i++) {
        uint32_t ix = 30 + (desktop_grid[i].grid_x * 90);
        uint32_t iy = 30 + (desktop_grid[i].grid_y * 80);
        desktop_draw_icon(desktop_grid[i].filename, ix, iy);
        int l = 0; while (desktop_grid[i].label[l] != '\0') {
            sys_draw_char(ix - 8 + (l * 8), iy + 38, desktop_grid[i].label[l], 0xFFFFFF); l++;
        }
    }

    // Dynamic User Account Text Tracking Output (Replaces useraccount1 profile lookups)
    uint32_t status_x = screen_width - 200;
    uint32_t status_y = 20;
    int u = 0;
    while (current_user.username[u] != '\0' && u < 32) {
        sys_draw_char(status_x + (u * 8), status_y, current_user.username[u], 0xFFFFFF);
        u++;
    }

    desktop_process_workspace_clicks();
}