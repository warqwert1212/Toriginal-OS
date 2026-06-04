#include "auth.h"

// Default initialization replacing the old fallback identity
user_config_t current_user = {
    .username = "useraccount1",
    .password = "password",
    .timezone_offset = 0,
    .ui_accent_color = 0x99B4D1,
    .complex_setup_completed = 0
};

void auth_init(void) {
    // Identity system ready
}

void auth_update_username(const char* new_name) {
    int i = 0;
    while (new_name[i] != '\0' && i < 31) {
        current_user.username[i] = new_name[i];
        i++;
    }
    current_user.username[i] = '\0';
}