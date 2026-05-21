/*
 * Toriginal OS GUI Resources
 * Icons and Wallpapers Configuration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ICONS 50
#define MAX_WALLPAPERS 10

typedef struct {
    char name[64];
    char unicode_char;
    char color[16];
} Icon;

typedef struct {
    char name[64];
    char filename[256];
    char color_scheme[32];
} Wallpaper;

/* Available Icons */
Icon icons[] = {
    {"File Manager", '📁', "[36m"},
    {"Terminal", '🖥', "[37m"},
    {"Text Editor", '📝', "[33m"},
    {"Calculator", '🧮', "[35m"},
    {"Games", '🎮', "[31m"},
    {"Settings", '⚙', "[37m"},
    {"Network", '🌐', "[34m"},
    {"Camera", '📷', "[36m"},
    {"Music", '🎵', "[35m"},
    {"Video", '🎬', "[31m"},
    {"Mail", '📧', "[36m"},
    {"Chat", '💬', "[32m"},
    {"Calendar", '📅', "[31m"},
    {"Clock", '⏰', "[37m"},
    {"Weather", '🌤', "[34m"},
    {"Store", '🛒', "[35m"},
    {"Gallery", '🖼', "[33m"},
    {"Trash", '🗑', "[37m"},
    {"Control Panel", '🎛', "[36m"},
    {"Task Manager", '📊', "[32m"},
    {NULL, 0, NULL}
};

/* Available Wallpapers */
Wallpaper wallpapers[] = {
    {"Default Blue", "/usr/share/wallpapers/default_blue.bmp", "blue_cyan"},
    {"Forest Green", "/usr/share/wallpapers/forest_green.bmp", "green_dark"},
    {"Sunset Orange", "/usr/share/wallpapers/sunset_orange.bmp", "orange_red"},
    {"Night Purple", "/usr/share/wallpapers/night_purple.bmp", "purple_dark"},
    {"Windows XP", "/usr/share/wallpapers/bliss.bmp", "blue_green"},
    {"Tron Grid", "/usr/share/wallpapers/tron_grid.bmp", "cyan_black"},
    {"Matrix", "/usr/share/wallpapers/matrix.bmp", "green_black"},
    {"Cyberpunk", "/usr/share/wallpapers/cyberpunk.bmp", "purple_cyan"},
    {"Minimalist", "/usr/share/wallpapers/minimalist.bmp", "white_gray"},
    {"Dark Mode", "/usr/share/wallpapers/dark_mode.bmp", "black_gray"},
    {NULL, NULL, NULL}
};

int gui_load_icons() {
    int count = 0;
    for (int i = 0; icons[i].name != NULL; i++) {
        printf("  ✓ Loaded icon: %s (%c)\n", icons[i].name, icons[i].unicode_char);
        count++;
    }
    printf("Total icons loaded: %d\n", count);
    return count;
}

int gui_load_wallpapers() {
    int count = 0;
    for (int i = 0; wallpapers[i].name != NULL; i++) {
        printf("  ✓ Loaded wallpaper: %s\n", wallpapers[i].name);
        count++;
    }
    printf("Total wallpapers loaded: %d\n", count);
    return count;
}

Icon* gui_get_icon(const char *name) {
    for (int i = 0; icons[i].name != NULL; i++) {
        if (strcmp(icons[i].name, name) == 0) {
            return &icons[i];
        }
    }
    return NULL;
}

Wallpaper* gui_get_wallpaper(const char *name) {
    for (int i = 0; wallpapers[i].name != NULL; i++) {
        if (strcmp(wallpapers[i].name, name) == 0) {
            return &wallpapers[i];
        }
    }
    return NULL;
}

void gui_list_all_icons() {
    printf("\nAvailable Icons:\n");
    printf("====================================\n");
    for (int i = 0; icons[i].name != NULL; i++) {
        printf("  %c  %s\n", icons[i].unicode_char, icons[i].name);
    }
}

void gui_list_all_wallpapers() {
    printf("\nAvailable Wallpapers:\n");
    printf("====================================\n");
    for (int i = 0; wallpapers[i].name != NULL; i++) {
        printf("  • %s (%s)\n", wallpapers[i].name, wallpapers[i].color_scheme);
    }
}
