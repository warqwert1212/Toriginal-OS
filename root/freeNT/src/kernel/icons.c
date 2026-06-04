/* Icon system implementation for freeNT */

#include "icons.h"
#include "graphics.h"
#include "mm.h"
#include "string.h"

/* Initialize icon manager */
int icons_init(icon_manager_t *manager) {
    if (!manager) return 0;
    
    manager->default_size = 32;
    
    /* Initialize all icon slots to NULL */
    for (int i = 0; i < ICON_COUNT; i++) {
        manager->icons[i] = NULL;
    }
    
    return 1;
}

/* Cleanup icon manager */
void icons_cleanup(icon_manager_t *manager) {
    if (!manager) return;
    
    for (int i = 0; i < ICON_COUNT; i++) {
        if (manager->icons[i]) {
            icons_destroy(manager->icons[i]);
            manager->icons[i] = NULL;
        }
    }
}

/* Create a new icon */
icon_t* icons_create(uint32_t width, uint32_t height) {
    icon_t *icon = (icon_t *)kmalloc(sizeof(icon_t));
    if (!icon) return NULL;
    
    icon->width = width;
    icon->height = height;
    icon->data = (uint32_t *)kmalloc(width * height * sizeof(uint32_t));
    
    if (!icon->data) {
        kfree(icon);
        return NULL;
    }
    
    /* Clear icon data */
    memset(icon->data, 0, width * height * sizeof(uint32_t));
    
    return icon;
}

/* Destroy an icon */
void icons_destroy(icon_t *icon) {
    if (!icon) return;
    
    if (icon->data) {
        kfree(icon->data);
    }
    kfree(icon);
}

/* Get icon from manager */
icon_t* icons_get(icon_manager_t *manager, icon_type_t type, icon_size_t size) {
    if (!manager || type >= ICON_COUNT) return NULL;
    
    /* For now, return the default icon if it exists */
    return manager->icons[type];
}

/* Draw icon at position */
void icons_draw(icon_t *icon, uint32_t x, uint32_t y) {
    if (!icon || !icon->data || !graphics_is_available()) return;
    
    /* Draw each pixel of the icon */
    for (uint32_t row = 0; row < icon->height; row++) {
        for (uint32_t col = 0; col < icon->width; col++) {
            uint32_t pixel_index = (row * icon->width) + col;
            uint32_t color = icon->data[pixel_index];
            
            /* Skip transparent pixels (alpha == 0) */
            if ((color >> 24) == 0) continue;
            
            graphics_draw_pixel(x + col, y + row, color);
        }
    }
}

/* Helper function to create a generic icon from a pattern */
icon_t* icons_create_generic_folder_icon(void) {
    icon_t *icon = icons_create(32, 32);
    if (!icon) return NULL;
    
    /* Create a simple folder icon (blue rectangle with tab) */
    color_t folder_color = graphics_rgb(70, 130, 180);  /* Steel blue */
    color_t tab_color = graphics_rgb(100, 150, 210);
    
    /* Fill main body */
    for (uint32_t y = 8; y < 32; y++) {
        for (uint32_t x = 2; x < 30; x++) {
            icon->data[y * 32 + x] = folder_color | 0xFF000000;
        }
    }
    
    /* Add folder tab */
    for (uint32_t y = 4; y < 10; y++) {
        for (uint32_t x = 2; x < 16; x++) {
            icon->data[y * 32 + x] = tab_color | 0xFF000000;
        }
    }
    
    return icon;
}

/* Create a generic file icon */
icon_t* icons_create_generic_file_icon(void) {
    icon_t *icon = icons_create(32, 32);
    if (!icon) return NULL;
    
    /* Create a simple file icon (white rectangle with lines) */
    color_t document_color = graphics_rgb(200, 200, 200);
    color_t line_color = graphics_rgb(100, 100, 100);
    
    /* Fill main body */
    for (uint32_t y = 4; y < 28; y++) {
        for (uint32_t x = 6; x < 26; x++) {
            icon->data[y * 32 + x] = document_color | 0xFF000000;
        }
    }
    
    /* Add text lines */
    for (int line = 0; line < 4; line++) {
        uint32_t y = 8 + (line * 4);
        for (uint32_t x = 8; x < 24; x++) {
            icon->data[y * 32 + x] = line_color | 0xFF000000;
        }
    }
    
    return icon;
}

/* Create executable icon */
icon_t* icons_create_executable_icon(void) {
    icon_t *icon = icons_create(32, 32);
    if (!icon) return NULL;
    
    /* Create a program icon (gear/cog shape) */
    color_t gear_color = graphics_rgb(200, 100, 50);  /* Orange-brown */
    
    /* Simple gear approximation with squares */
    for (uint32_t y = 10; y < 22; y++) {
        for (uint32_t x = 10; x < 22; x++) {
            icon->data[y * 32 + x] = gear_color | 0xFF000000;
        }
    }
    
    /* Add teeth */
    for (uint32_t x = 6; x < 10; x++) {
        for (uint32_t y = 14; y < 18; y++) {
            icon->data[y * 32 + x] = gear_color | 0xFF000000;
        }
    }
    for (uint32_t x = 22; x < 26; x++) {
        for (uint32_t y = 14; y < 18; y++) {
            icon->data[y * 32 + x] = gear_color | 0xFF000000;
        }
    }
    
    return icon;
}
