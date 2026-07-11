#ifndef _WALLPAPER_PICKER_H
#define _WALLPAPER_PICKER_H

#include <stdint.h>
#include <stddef.h>

#define PICKER_WALLPAPER_DIR   "assets/desktop/wallpapers"
#define PICKER_CONFIG_PATH     "assets/desktop/config.cfg"
#define PICKER_MAX_ITEMS       64
#define PICKER_PATH_MAX        512

/* Reads config.cfg for a previously-saved wallpaper path.
 * Returns 1 and fills out_path if found, 0 if no config exists yet
 * (first boot) or the saved file no longer exists on disk. */
int picker_load_saved_choice(char *out_path, size_t out_path_sz);

/* Persists the chosen wallpaper path so future boots skip the picker. */
void picker_save_choice(const char *path);

/* Blocking graphical picker. Opens on PICKER_WALLPAPER_DIR by default but
 * lets the user navigate into subfolders and back out via ".." -- browsing
 * is not locked to that one folder. Runs its own event/draw loop until the
 * user clicks an image. Returns 1 and fills out_path on success, 0 if the
 * user quit out without choosing (caller should fall back to default fill). */
int picker_run(uint32_t *fb, int32_t fb_w, int32_t fb_h,
               char *out_path, size_t out_path_sz);

#endif
