#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>

typedef struct {
    char username[32];
    char password[32];
    int timezone_offset;
    uint32_t ui_accent_color;
    int complex_setup_completed;
} user_config_t;

extern user_config_t current_user;

void auth_init(void);
void auth_update_username(const char* new_name);

#endif