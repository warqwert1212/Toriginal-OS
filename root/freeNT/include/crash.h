#ifndef CRASH_H
#define CRASH_H

#include "interrupts.h"

#define CRASH_FACE_FATAL "(X _ X)"
#define CRASH_FACE_SOFT  "(- _ -)"

void rsod_show(const char *title, const char *reason, const interrupt_frame_t *frame) __attribute__((noreturn));
void gui_error_report(const char *what, const char *detail);
void gui_error_draw(uint32_t screen_w, uint32_t screen_h, uint32_t taskbar_h);

#endif
