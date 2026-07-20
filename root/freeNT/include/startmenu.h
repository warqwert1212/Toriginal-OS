/* startmenu.h - Vista-style Start button + Start menu.
 *
 * Every visible pixel here comes from a PNG read off TRPFS at
 * STARTMENU_DIR - nothing is drawn from a hardcoded shape or baked
 * into the binary. Any asset that isn't installed yet is skipped
 * (orb -> plain circle, panel/taskbar -> flat fill) rather than
 * faked, same honest-degradation contract as wallpaper.c.
 *
 * Assets ship as real multiboot modules (see kernel.c's
 * seed_boot_modules_to_fs() and grub.cfg's module2 lines) - that
 * machinery is already generic from the wallpaper work, so nothing
 * about how modules get onto disk changed for this.
 */
#ifndef STARTMENU_H
#define STARTMENU_H

#include <stdint.h>
#include "graphics_core.h"

#define STARTMENU_DIR             "/system/gui/startmenu"
#define STARTMENU_ORB_PATH        STARTMENU_DIR "/orb.png"
#define STARTMENU_ORB_HOVER_PATH  STARTMENU_DIR "/orb_hover.png"
#define STARTMENU_TASKBAR_PATH    STARTMENU_DIR "/taskbar.png"
#define STARTMENU_PANEL_PATH      STARTMENU_DIR "/panel.png"
#define STARTMENU_PROGRAMS_PATH   STARTMENU_DIR "/programs.png"
#define STARTMENU_POWER_PATH      STARTMENU_DIR "/power.png"

#define STARTMENU_MAX_PROGRAMS 12

typedef void (*startmenu_action_fn)(void *ctx);

/* Loads every asset in STARTMENU_DIR that's actually present. Call
 * once at desktop startup, same timing as wallpaper_init(). Missing
 * files are not an error - see the header comment above. */
void startmenu_init(void);

/* Registers one row in the "all programs" list - label + action,
 * fired the same way desktop_menu_t's items are (see desktop_menu.h).
 * Registered by desktop.c at startup, not hardcoded here - this file
 * only owns layout/drawing/hit-testing, not what the OS can launch. */
int startmenu_add_program(const char *label, startmenu_action_fn action, void *ctx);

void startmenu_toggle(void);
void startmenu_close(void);
int  startmenu_is_open(void);

/* Draws the taskbar background stretched to (screen_w, taskbar_h) -
 * width follows the screen resolution, height stays exactly
 * taskbar_h, per spec. Falls back to `fallback_color` flat fill if
 * STARTMENU_TASKBAR_PATH isn't installed. Draw this BEFORE the clock/
 * status text/orb so they layer on top, same order the old flat fill
 * was drawn in. */
void startmenu_draw_taskbar_bg(uint32_t screen_w, uint32_t screen_h, uint32_t taskbar_h, color_t fallback_color);

/* Draws the orb Start button at its fixed taskbar position (left
 * edge), swapping to the hover asset when (mouse_x, mouse_y) is over
 * it. Falls back to a plain filled circle if orb.png isn't loaded. */
void startmenu_draw_orb(uint32_t screen_h, uint32_t taskbar_h, int32_t mouse_x, int32_t mouse_y);

/* Draws the open panel (background + programs list + labels + power
 * button) - no-op if the menu is closed. */
void startmenu_draw_panel(uint32_t screen_h, uint32_t taskbar_h);

/* True if (x, y) is inside the orb's clickable area. */
int startmenu_orb_hit(uint32_t screen_h, uint32_t taskbar_h, int32_t x, int32_t y);

/* Called on a left click while the menu is open. Handles program-row
 * clicks and the power button; any click outside the panel closes the
 * menu without side effects. Returns 1 if the click landed on the
 * panel (caller shouldn't also treat it as a desktop click), 0
 * otherwise (menu is closed either way once this returns). */
int startmenu_handle_click(uint32_t screen_h, uint32_t taskbar_h, int32_t x, int32_t y);

#endif /* STARTMENU_H */
