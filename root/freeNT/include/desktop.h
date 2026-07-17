/* desktop.h - Minimal real graphical desktop, drawn directly onto the
 * kernel's own framebuffer via graphics_core.h/font8x16.h/cursor.h.
 *
 * This is NOT the SDL-hosted prototype under root/sys/gui (that is a
 * separate, host-only mockup used to iterate on visual design - it
 * never runs inside the freestanding kernel). desktop_run() is the
 * real, bare-metal desktop that the "desktop" shell command and the
 * GRUB "boot to GUI" entry actually launch.
 */
#ifndef DESKTOP_H
#define DESKTOP_H

/* Takes over the screen with a graphical desktop: wallpaper, taskbar
 * with a live clock, and a couple of clickable icons. Runs its own
 * input loop (polls keyboard + mouse directly) until the user exits
 * (Esc, or clicking the "Exit to shell" icon), then restores the text
 * terminal and returns. Requires gterm_is_active(); if graphics never
 * came up (no linear framebuffer), it prints an explanatory message
 * and returns immediately instead of hanging. */
void desktop_run(void);

#endif /* DESKTOP_H */
