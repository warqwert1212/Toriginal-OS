/* wallpaper.h - File-driven desktop wallpaper.
 *
 * Replaces the old hardcoded graphics_clear_screen(graphics_rgb(...))
 * desktop background with a real one: a PNG read off TRPFS, decoded
 * via png.c, and scaled onto the screen. Nothing here is baked into
 * the binary - the active wallpaper is whatever file WALLPAPER_CFG_PATH
 * points at, and that file is rewritten every time wallpaper_next()
 * or wallpaper_set_path() picks a new one. If no wallpaper file can be
 * read or decoded, wallpaper_draw() honestly falls back to the caller-
 * supplied flat color instead of pretending an image loaded.
 */
#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <stdint.h>
#include "graphics_core.h"

/* Directory scanned for candidate wallpapers (*.png) and the config
 * file that persists which one is active. Both are plain TRPFS paths -
 * nothing PNG-shaped exists at either until something (the installer,
 * a dev seeding step, or trpm) actually writes files there. */
#define WALLPAPER_DIR       "/system/gui/wallpapers"
#define WALLPAPER_CFG_PATH  "/system/gui/wallpaper.cfg"

/* Reads WALLPAPER_CFG_PATH (if present) and loads whatever path it
 * names. Safe to call even if nothing is installed yet - just leaves
 * the wallpaper "unset" and wallpaper_draw() will fall back to flat
 * color until wallpaper_next()/wallpaper_set_path() succeeds. */
void wallpaper_init(void);

/* Loads and decodes the PNG at `path`, replacing the current
 * wallpaper on success. On success, also persists `path` to
 * WALLPAPER_CFG_PATH so the choice survives a reboot. Returns 1 on
 * success, 0 on failure (file missing, not a PNG this decoder
 * supports, out of memory) - the previous wallpaper (if any) is left
 * in place on failure rather than cleared. */
int wallpaper_set_path(const char *path);

/* Scans WALLPAPER_DIR for *.png entries and advances to the next one
 * after whatever's currently active (wraps around; picks the first
 * entry if nothing is active yet). No-op if the directory doesn't
 * exist or has no .png files. */
void wallpaper_next(void);

/* Draws the current wallpaper scaled to (screen_w, screen_h), or
 * fills the screen with `fallback_color` if no wallpaper is loaded. */
void wallpaper_draw(uint32_t screen_w, uint32_t screen_h, color_t fallback_color);

/* Scans WALLPAPER_DIR for *.png files and writes up to `max_entries`
 * filenames (not full paths) into `out_names`. Returns the count
 * actually found (0 if the directory doesn't exist or has no PNGs).
 * This is what backs the right-click "Change Wallpaper" picker in
 * desktop.c - the picker's contents are exactly and only whatever's
 * really sitting in this directory on disk, scanned fresh every time
 * it's opened. No hardcoded list anywhere. */
int wallpaper_list(char out_names[][64], int max_entries);

/* True if wallpaper_set_path() has ever succeeded this session. */
int wallpaper_is_loaded(void);

#endif /* WALLPAPER_H */
