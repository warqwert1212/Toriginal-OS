#include <stdint.h>
#include "../kernel/include/auth.h"

// Kernel IO Dependencies
extern void sys_shell_print(const char* text);
extern void sys_shell_read_line(char* buffer, int max_len);
extern void sys_shell_clear(void);
extern void sys_execute_program(const char* path);

static void read_clean_input(char* buffer, int max_len) {
    sys_shell_read_line(buffer, max_len);
    // Strip trailing newlines if present
    for (int i = 0; i < max_len; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\r') {
            buffer[i] = '\0';
            break;
        }
    }
}

void main_oobe_setup(void) {
    char choice[4];
    char temp_user[32];
    char temp_pass[32];
    char temp_tz[8];
    char temp_color[4];

    sys_shell_clear();
    sys_shell_print("==================================================\n");
    sys_shell_print("          TORIGINAL OS - OUT OF BOX EXPERIENCE    \n");
    sys_shell_print("==================================================\n\n");
    
    // --- SIMPLE SETUP ---
    sys_shell_print("[STEP 1: SIMPLE USER ACCOUNT CREATION]\n");
    sys_shell_print("Enter New Username (Replaces useraccount1): ");
    read_clean_input(temp_user, 32);
    
    if (temp_user[0] != '\0') {
        auth_update_username(temp_user);
    }

    sys_shell_print("Enter New Password: ");
    read_clean_input(temp_pass, 32);
    int p = 0;
    while (temp_pass[p] != '\0' && p < 31) {
        current_user.password[p] = temp_pass[p];
        p++;
    }
    current_user.password[p] = '\0';

    // --- CHOICE FOR COMPLEX SETUP ---
    sys_shell_print("\nWould you like to configure Advanced System Settings? (Y/N): ");
    read_clean_input(choice, 4);

    if (choice[0] == 'Y' || choice[0] == 'y') {
        sys_shell_print("\n[STEP 2: COMPLEX SYSTEM PROFILING]\n");
        
        // 1. Timezone Engine Configuration
        sys_shell_print("Enter Timezone GMT Offset (-12 to 14): ");
        read_clean_input(temp_tz, 8);
        int offset = 0;
        int sign = 1;
        int idx = 0;
        if (temp_tz[0] == '-') { sign = -1; idx++; }
        else if (temp_tz[0] == '+') { idx++; }
        
        while (temp_tz[idx] >= '0' && temp_tz[idx] <= '9') {
            offset = (offset * 10) + (temp_tz[idx] - '0');
            idx++;
        }
        current_user.timezone_offset = offset * sign;

        // 2. Desktop UI Accent Palette
        sys_shell_print("Select Windows 7 Theme Style Accent:\n");
        sys_shell_print(" 1. Aero Blue (Default)\n");
        sys_shell_print(" 2. Slate Gray\n");
        sys_shell_print(" 3. Classic Teal\n");
        sys_shell_print("Choice (1-3): ");
        read_clean_input(temp_color, 4);

        if (temp_color[0] == '2') {
            current_user.ui_accent_color = 0x708090;
        } else if (temp_color[0] == '3') {
            current_user.ui_accent_color = 0x008080;
        } else {
            current_user.ui_accent_color = 0x99B4D1; 
        }

        current_user.complex_setup_completed = 1;
        sys_shell_print("\nComplex profiling recorded successfully.\n");
    } else {
        sys_shell_print("\nSkipping advanced profile. Using performance fallbacks.\n");
    }

    sys_shell_print("\nSetup Complete! Booting Workspace Canvas...\n");
    sys_shell_print("Press Enter to log in...");
    read_clean_input(choice, 2);

    // Launch main graphical desktop interface
    sys_execute_program("/sys/desktop.trp");
}