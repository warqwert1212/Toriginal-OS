#ifndef APPHOST_H
#define APPHOST_H

#include <stdint.h>
#include "interrupts.h"

#define APP_MAX 8

int  apphost_launch(const char *path);
void apphost_step(void);
int  apphost_running(void);
int  apphost_in_slice(void);
int  apphost_owns_window(uint32_t window_id);
void apphost_close_window(uint32_t window_id);
void apphost_shutdown_all(void);
void apphost_preempt(interrupt_frame_t *f);
void apphost_crash(interrupt_frame_t *f);
void apphost_exit_current(int status) __attribute__((noreturn));

#endif
