#include "wallpaper_picker.h"
#include "ui_draw.h"
#include "../image/image_loader.h"
#include "../platform/platform.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define THUMB_W    220
#define THUMB_H    140
#define GRID_PAD   24
#define GRID_TOP   90

typedef enum { ENTRY_UP, ENTRY_DIR, ENTRY_IMAGE } entry_kind_t;

typedef struct {
    entry_kind_t kind;
    char     label[256];       /* filename or dirname shown on the tile */
    char     full_path[PICKER_PATH_MAX];
    image_t  thumb;             /* only populated for ENTRY_IMAGE */
    int32_t  cell_x, cell_y;
} picker_entry_t;

static int has_supported_ext(const char *name) {
    size_t len = strlen(name);
    if (len >= 4 && strcasecmp(name + len - 4, ".png") == 0) return 1;
    if (len >= 4 && strcasecmp(name + len - 4, ".bmp") == 0) return 1;
    return 0;
}

int picker_load_saved_choice(char *out_path, size_t out_path_sz) {
    FILE *f = fopen(PICKER_CONFIG_PATH, "r");
    if (!f) return 0;

    char line[PICKER_PATH_MAX];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        if (strncmp(line, "wallpaper=", 10) == 0) {
            snprintf(out_path, out_path_sz, "%s", line + 10);
            found = 1;
            break;
        }
    }
    fclose(f);

    if (!found) return 0;

    FILE *check = fopen(out_path, "rb");
    if (!check) return 0;
    fclose(check);
    return 1;
}

void picker_save_choice(const char *path) {
    FILE *f = fopen(PICKER_CONFIG_PATH, "w");
    if (!f) return;
    fprintf(f, "wallpaper=%s\n", path);
    fclose(f);
}

/* Strips the last path component, in place, e.g. "a/b/c" -> "a/b".
 * If there's no parent left to go to (no '/', or root-ish), returns 0. */
static int go_up(char *dir) {
    size_t len = strlen(dir);
    while (len > 0 && dir[len - 1] == '/') dir[--len] = '\0';

    char *slash = strrchr(dir, '/');
    if (!slash) {
        if (strcmp(dir, ".") == 0) return 0;   /* nowhere higher to go */
        strcpy(dir, ".");
        return 1;
    }
    if (slash == dir) {
        dir[1] = '\0';   /* reached "/" */
        return 1;
    }
    *slash = '\0';
    return 1;
}

static int scan_dir(const char *dir, picker_entry_t *entries, int max) {
    DIR *d = opendir(dir);
    if (!d) return 0;

    int count = 0;

    if (strcmp(dir, "/") != 0 && count < max) {
        picker_entry_t *e = &entries[count++];
        e->kind = ENTRY_UP;
        snprintf(e->label, sizeof(e->label), "..");
        e->full_path[0] = '\0';
    }

    struct dirent *ent;
    while (count < max && (ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[PICKER_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            picker_entry_t *e = &entries[count];
            e->kind = ENTRY_DIR;
            snprintf(e->label, sizeof(e->label), "%s", ent->d_name);
            snprintf(e->full_path, sizeof(e->full_path), "%s", full_path);
            count++;
            continue;
        }

        if (!has_supported_ext(ent->d_name)) continue;

        picker_entry_t *e = &entries[count];
        image_t full = {0};
        if (!image_load(full_path, &full)) continue;
        if (!image_downscale(&full, THUMB_W, THUMB_H, &e->thumb)) {
            image_free(&full);
            continue;
        }
        image_free(&full);

        e->kind = ENTRY_IMAGE;
        snprintf(e->label, sizeof(e->label), "%s", ent->d_name);
        snprintf(e->full_path, sizeof(e->full_path), "%s", full_path);
        count++;
    }
    closedir(d);
    return count;
}

static void free_thumbs(picker_entry_t *entries, int count) {
    for (int i = 0; i < count; i++) {
        if (entries[i].kind == ENTRY_IMAGE) image_free(&entries[i].thumb);
    }
}

static void blit_thumb(const image_t *thumb, uint32_t *fb, int32_t fb_w, int32_t fb_h,
                       int32_t x, int32_t y) {
    for (int32_t ty = 0; ty < thumb->height; ty++) {
        int32_t fy = y + ty;
        if (fy < 0 || fy >= fb_h) continue;
        for (int32_t tx = 0; tx < thumb->width; tx++) {
            int32_t fx = x + tx;
            if (fx < 0 || fx >= fb_w) continue;
            fb[fy * fb_w + fx] = thumb->pixels[ty * thumb->width + tx] | 0xFF000000;
        }
    }
}

static void draw_tile_border(uint32_t *fb, int32_t fb_w, int32_t fb_h,
                              int32_t x, int32_t y, uint32_t color) {
    ui_fill_rect(fb, fb_w, fb_h, x - 2, y - 2, THUMB_W + 4, 2, color);
    ui_fill_rect(fb, fb_w, fb_h, x - 2, y + THUMB_H, THUMB_W + 4, 2, color);
    ui_fill_rect(fb, fb_w, fb_h, x - 2, y - 2, 2, THUMB_H + 4, color);
    ui_fill_rect(fb, fb_w, fb_h, x + THUMB_W, y - 2, 2, THUMB_H + 4, color);
}

int picker_run(uint32_t *fb, int32_t fb_w, int32_t fb_h,
               char *out_path, size_t out_path_sz) {
    static picker_entry_t entries[PICKER_MAX_ITEMS];
    char current_dir[PICKER_PATH_MAX];
    snprintf(current_dir, sizeof(current_dir), "%s", PICKER_WALLPAPER_DIR);

    int count = scan_dir(current_dir, entries, PICKER_MAX_ITEMS);

    int cols = (fb_w - GRID_PAD) / (THUMB_W + GRID_PAD);
    if (cols < 1) cols = 1;

    uint32_t bg           = ui_argb(255, 0x20, 0x20, 0x20);
    uint32_t heading_color = ui_argb(255, 0xFF, 0xFF, 0xFF);
    uint32_t path_color    = ui_argb(255, 0xA0, 0xA0, 0xA0);
    uint32_t border_normal = ui_argb(255, 0x60, 0x60, 0x60);
    uint32_t border_hover  = ui_argb(255, 0xFF, 0xCC, 0x00);
    uint32_t dir_fill      = ui_argb(255, 0x38, 0x38, 0x48);
    uint32_t dir_text      = ui_argb(255, 0xE0, 0xE0, 0xFF);

    int selected = -1;
    int mx = fb_w / 2, my = fb_h / 2;
    int quit = 0;

    while (selected < 0 && !quit) {
        for (int i = 0; i < count; i++) {
            int col = i % cols;
            int row = i / cols;
            entries[i].cell_x = GRID_PAD + col * (THUMB_W + GRID_PAD);
            entries[i].cell_y = GRID_TOP + row * (THUMB_H + GRID_PAD);
        }

        int nav_target = -1;

        event_t evt;
        while (plat_poll_event(&evt)) {
            if (evt.type == EVT_QUIT) {
                quit = 1;
            } else if (evt.type == EVT_KEY && evt.key == 27 && evt.is_pressed) {
                quit = 1;
            } else if (evt.type == EVT_MOUSE_MOVE) {
                mx = evt.x;
                my = evt.y;
            } else if (evt.type == EVT_MOUSE_DOWN && evt.button == 0) {
                for (int i = 0; i < count; i++) {
                    if (evt.x >= entries[i].cell_x && evt.x < entries[i].cell_x + THUMB_W &&
                        evt.y >= entries[i].cell_y && evt.y < entries[i].cell_y + THUMB_H) {
                        if (entries[i].kind == ENTRY_IMAGE) {
                            selected = i;
                        } else {
                            nav_target = i;
                        }
                        break;
                    }
                }
            }
        }

        if (nav_target >= 0) {
            if (entries[nav_target].kind == ENTRY_UP) {
                go_up(current_dir);
            } else {
                snprintf(current_dir, sizeof(current_dir), "%s", entries[nav_target].full_path);
            }
            free_thumbs(entries, count);
            count = scan_dir(current_dir, entries, PICKER_MAX_ITEMS);
        }

        int hover = -1;
        for (int i = 0; i < count; i++) {
            if (mx >= entries[i].cell_x && mx < entries[i].cell_x + THUMB_W &&
                my >= entries[i].cell_y && my < entries[i].cell_y + THUMB_H) {
                hover = i;
                break;
            }
        }

        ui_fill_rect(fb, fb_w, fb_h, 0, 0, fb_w, fb_h, bg);
        ui_draw_text(fb, fb_w, fb_h, GRID_PAD, 40, "SELECT A WALLPAPER", heading_color);
        ui_draw_text(fb, fb_w, fb_h, GRID_PAD, 62, current_dir, path_color);

        for (int i = 0; i < count; i++) {
            uint32_t border = (i == hover) ? border_hover : border_normal;

            if (entries[i].kind == ENTRY_IMAGE) {
                blit_thumb(&entries[i].thumb, fb, fb_w, fb_h,
                           entries[i].cell_x, entries[i].cell_y);
            } else {
                ui_fill_rect(fb, fb_w, fb_h, entries[i].cell_x, entries[i].cell_y,
                             THUMB_W, THUMB_H, dir_fill);
                const char *label = (entries[i].kind == ENTRY_UP) ? ".. (UP)" : entries[i].label;
                ui_draw_text(fb, fb_w, fb_h, entries[i].cell_x + 10,
                             entries[i].cell_y + THUMB_H / 2 - 4, label, dir_text);
            }
            draw_tile_border(fb, fb_w, fb_h, entries[i].cell_x, entries[i].cell_y, border);
        }

        fb_t plat_fb = { .pixels = fb, .width = fb_w, .height = fb_h, .pitch = fb_w * 4 };
        plat_present(&plat_fb);
        plat_delay(16);
    }

    int ok = 0;
    if (selected >= 0) {
        snprintf(out_path, out_path_sz, "%s", entries[selected].full_path);
        ok = 1;
    }

    free_thumbs(entries, count);
    return ok;
}
