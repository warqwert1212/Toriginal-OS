
#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>


void desktop_run(void);
int desktop_is_running(void);
int desktop_request_resolution(uint32_t w, uint32_t h);

#endif 
