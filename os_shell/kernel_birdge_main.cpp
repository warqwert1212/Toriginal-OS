#include <stdint.h>
#include "terminal.hpp"

// tiny strcmp/strncmp (no libc)
static int kstrcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int kstrncmp(const char* a, const char* b, int n) {
    while (n-- && *a && (*a == *b)) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

extern "C" void kernel_main() {
    Terminal term;
    term.set_color(0x0A, 0x00); // green on black

    term.writeln("Kernel Initialized Successfully!");
    term.writeln("Toriganal OS is running in 32-bit protected mode");
    term.writeln("System ready for next phase...");
    term.writeln("");
    term.writeln("Toriganal Shell v0.1");
    term.writeln("Type 'help' for commands.");
    term.writeln("");

    char line[128];

    while (true) {
        term.write("toriganal> ");
        int len = term.read_line(line, sizeof(line));
        if (len == 0) continue;

        if (!kstrcmp(line, "help")) {
            term.writeln("Commands:");
            term.writeln("  help   - show this help");
            term.writeln("  clear  - clear screen");
            term.writeln("  echo X - print X");
            term.writeln("  halt   - halt CPU");
        }
        else if (!kstrcmp(line, "clear")) {
            term.clear();
        }
        else if (!kstrncmp(line, "echo ", 5)) {
            term.writeln(line + 5);
        }
        else if (!kstrcmp(line, "halt")) {
            term.writeln("Halting...");
            while (1) asm volatile ("hlt");
        }
        else {
            term.writeln("Unknown command. Type 'help'.");
        }
    }
}
