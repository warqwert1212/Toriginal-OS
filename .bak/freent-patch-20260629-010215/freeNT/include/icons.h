/* Icon resource system for freeNT GUI */
#ifndef ICONS_H
#define ICONS_H

#include <stdint.h>
#include <stddef.h>

/* Icon format - simple bitmap */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *data;  /* ARGB color data */
} icon_t;

/* Standard icon sizes */
typedef enum {
    ICON_SIZE_16 = 16,
    ICON_SIZE_24 = 24,
    ICON_SIZE_32 = 32,
    ICON_SIZE_48 = 48,
    ICON_SIZE_64 = 64,
} icon_size_t;

/* Standard icon types */
typedef enum {
    ICON_FILE,
    ICON_FOLDER,
    ICON_EXECUTABLE,
    ICON_IMAGE,
    ICON_TEXT,
    ICON_SETTINGS,
    ICON_CLOSE,
    ICON_MINIMIZE,
    ICON_MAXIMIZE,
    ICON_RESTORE,
    ICON_WINDOW,
    ICON_FOLDER_OPEN,
    ICON_SAVE,
    ICON_DELETE,
    ICON_COPY,
    ICON_PASTE,
    ICON_CUT,
    ICON_COUNT
} icon_type_t;

/* Icon manager */
typedef struct {
    icon_t *icons[ICON_COUNT];
    uint32_t default_size;
} icon_manager_t;

/* Initialization */
int icons_init(icon_manager_t *manager);
void icons_cleanup(icon_manager_t *manager);

/* Icon loading/creation */
icon_t* icons_create(uint32_t width, uint32_t height);
void icons_destroy(icon_t *icon);
icon_t* icons_get(icon_manager_t *manager, icon_type_t type, icon_size_t size);

/* Icon rendering */
void icons_draw(icon_t *icon, uint32_t x, uint32_t y);

#endif /* ICONS_H */
