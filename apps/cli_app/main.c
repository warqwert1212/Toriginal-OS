/* Simple CLI application for Toriginal OS - placeholder
   Provides a minimal set of built-in commands: echo, cat (text files), help, exit
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    char line[256];
    printf("Toriginal CLI - type 'help' for commands\n");

    while (1) {
        printf("user@toriginal:$ ");
        if (!fgets(line, sizeof(line), stdin)) break;
        /* remove newline */
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "exit") == 0) break;
        if (strcmp(line, "help") == 0) {
            printf("commands: help echo cat exit\n");
            continue;
        }
        if (strncmp(line, "echo ", 5) == 0) {
            printf("%s\n", line + 5);
            continue;
        }
        if (strncmp(line, "cat ", 4) == 0) {
            const char *path = line + 4;
            FILE *f = fopen(path, "r");
            if (!f) { printf("cat: cannot open %s\n", path); continue; }
            char buf[256];
            while (fgets(buf, sizeof(buf), f)) fputs(buf, stdout);
            fclose(f);
            continue;
        }
        printf("Unknown command: %s\n", line);
    }

    printf("Goodbye\n");
    return 0;
}
