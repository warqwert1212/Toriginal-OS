/* mkisoboot.c
   Small ISO builder wrapper. Builds an ISO image `freeNT.iso` from the
   repository's `build/freeNT/freeNT` kernel and `freeNT/src/kernel/boot/grub.cfg`.
   This program is a compiled helper so users don't need to run ad-hoc shell
   scripts; it still relies on `grub-mkrescue`/`xorriso` to create a proper
   GRUB-based bootable ISO.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static int copy_file(const char *src, const char *dst) {
    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) return -1;
    FILE *fdst = fopen(dst, "wb");
    if (!fdst) { fclose(fsrc); return -2; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, n, fdst) != n) { fclose(fsrc); fclose(fdst); return -3; }
    }
    fclose(fsrc);
    fclose(fdst);
    return 0;
}

static int run_command(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

static int has_el_torito(const char *iso_path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xorriso -indev %s -report_el_torito plain 2>/dev/null", iso_path);
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "No El Torito information was loaded")) {
            found = 0;
            break;
        }
        if (strstr(line, "El Torito")) {
            found = 1;
            break;
        }
    }
    pclose(fp);
    return found;
}

int main(void) {
    const char *kernel = "build/freeNT/freeNT";
    const char *grub_cfg = "freeNT/src/kernel/boot/grub.cfg";
    const char *iso_dir = "iso_build";
    const char *boot_dir = "iso_build/boot";
    const char *grub_dir = "iso_build/boot/grub";
    const char *out_iso = "freeNT.iso";

    if (!file_exists(kernel)) {
        fprintf(stderr, "Error: kernel not found at %s\n", kernel);
        fprintf(stderr, "Run the build first: make freeNT\n");
        return 2;
    }
    if (!file_exists(grub_cfg)) {
        fprintf(stderr, "Error: grub.cfg not found at %s\n", grub_cfg);
        return 2;
    }

    if (ensure_dir(iso_dir) != 0 || ensure_dir(boot_dir) != 0 || ensure_dir(grub_dir) != 0) {
        fprintf(stderr, "Error: unable to create ISO build directories\n");
        return 3;
    }

    char dst_kernel[512];
    snprintf(dst_kernel, sizeof(dst_kernel), "%s/freeNT", boot_dir);
    if (copy_file(kernel, dst_kernel) != 0) {
        fprintf(stderr, "Failed to copy kernel to %s\n", dst_kernel);
        return 3;
    }

    char dst_cfg[512];
    snprintf(dst_cfg, sizeof(dst_cfg), "%s/grub.cfg", grub_dir);
    if (copy_file(grub_cfg, dst_cfg) != 0) {
        fprintf(stderr, "Failed to copy grub.cfg to %s\n", dst_cfg);
        return 3;
    }

    if (access("grub-mkrescue", X_OK) != 0 && access("/usr/bin/grub-mkrescue", X_OK) != 0) {
        fprintf(stderr, "Error: grub-mkrescue not found on PATH.\n");
        fprintf(stderr, "Install GRUB tools and retry.\n");
        fprintf(stderr, "Debian/Ubuntu: sudo apt install -y grub-pc-bin grub2-common xorriso\n");
        return 4;
    }

    if (access("xorriso", X_OK) != 0 && access("/usr/bin/xorriso", X_OK) != 0) {
        fprintf(stderr, "Error: xorriso not found on PATH.\n");
        fprintf(stderr, "Install xorriso and retry.\n");
        fprintf(stderr, "Debian/Ubuntu: sudo apt install -y xorriso\n");
        return 4;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "grub-mkrescue -o %s %s 2>&1", out_iso, iso_dir);
    int rc = run_command(cmd);
    if (rc != 0) {
        fprintf(stderr, "grub-mkrescue failed with exit code %d\n", rc);
        fprintf(stderr, "Ensure GRUB BIOS/UEFI packages are installed.\n");
        fprintf(stderr, "Debian/Ubuntu: sudo apt install -y grub-pc-bin grub2-common xorriso\n");
        return 5;
    }

    if (!file_exists(out_iso)) {
        fprintf(stderr, "Error: ISO not created\n");
        return 6;
    }

    if (!has_el_torito(out_iso)) {
        fprintf(stderr, "Error: ISO created but contains no El Torito boot record.\n");
        fprintf(stderr, "This means the disk is not BIOS-bootable.\n");
        fprintf(stderr, "Install GRUB BIOS modules and retry: sudo apt install -y grub-pc-bin\n");
        return 7;
    }

    printf("Bootable ISO created: %s\n", out_iso);
    return 0;
}
