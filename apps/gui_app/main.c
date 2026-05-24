/* Simple GUI placeholder for Toriginal OS
   This is a minimal framebuffer/console GUI placeholder that prints a list of
   'icons' (text-based) and reacts to simple keyboard input. Replace with a
   real GUI when integrating with the kernel's display driver.
*/

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *icons[] = {"[T] Terminal", "[F] File Manager", "[S] Settings"};
    printf("Toriginal GUI (placeholder)\n");
    printf("Icons:\n");
    for (int i = 0; i < 3; ++i) printf("  %s\n", icons[i]);
    printf("\nPress q to quit.\n");
    while (1) {
        int c = getchar();
        if (c == EOF) break;
        if (c == 'q' || c == 'Q') break;
        if (c == 'T' || c == 't') printf("Launching Terminal (placeholder)\n");
        if (c == 'F' || c == 'f') printf("Launching File Manager (placeholder)\n");
    }
    printf("GUI exit\n");
    return 0;
}
