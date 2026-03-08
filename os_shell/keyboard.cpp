#include <stdint.h>
#include "keyboard_define.hpp"
#include "io.hpp"

static const char scancode_table[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0,   0,   0,   ' ',
    // rest zero
};

char keyboard_getchar() {
    while (true) {
        uint8_t sc = inb(0x60);

        // ignore key releases
        if (sc & 0x80)
            continue;

        if (sc < 128) {
            char c = scancode_table[sc];
            if (c != 0)
                return c;
        }
    }
}

