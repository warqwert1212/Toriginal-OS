#include "types.h"
#include "fs.h"
#include "string.h"
#include "serial.h"
#include "syscfg.h"

typedef struct {
    char     path[128];
    uint32_t data_offset;
    uint32_t data_len;
} __attribute__((packed)) osimage_entry_t;

int  os_find_boot_module(const char *dest, const uint8_t **data, uint64_t *len);
void os_write_file(const char *dest_path, const uint8_t *src, size_t total);

#define OSIMAGE_PATH "/system/osimage.img"

static char g_status[96] = "OS image: not checked yet";
static int g_total = 0, g_applied = 0, g_failed = 0;

const char *osimage_status(void) { return g_status; }
int os_boot_module_count(void);

static void put(size_t *i, const char *t) {
    while (*t && *i < sizeof(g_status) - 1) g_status[(*i)++] = *t++;
}

static void put_num(size_t *i, uint64_t n) {
    char d[24]; int k = 0;
    if (n == 0) d[k++] = '0';
    while (n > 0 && k < 23) { d[k++] = (char)('0' + n % 10); n /= 10; }
    while (k > 0 && *i < sizeof(g_status) - 1) g_status[(*i)++] = d[--k];
}

void osimage_note_no_disk(void) {
    size_t i = 0;
    put(&i, "OSIMG: no disk, modules=");
    put_num(&i, (uint64_t)os_boot_module_count());
    g_status[i] = 0;
}

static void set_status(const char *a, int n) {
    size_t i = 0;
    while (a[i] && i < sizeof(g_status) - 16) { g_status[i] = a[i]; i++; }
    char d[12]; int k = 0;
    if (n == 0) d[k++] = '0';
    while (n > 0 && k < 11) { d[k++] = (char)('0' + n % 10); n /= 10; }
    while (k > 0) g_status[i++] = d[--k];
    g_status[i] = 0;
}

void seed_embedded_assets_to_fs(void) {
    const uint8_t *img = NULL;
    uint64_t size = 0;
    if (!os_find_boot_module(OSIMAGE_PATH, &img, &size)) {
        serial_puts("[OSIMG] no OS image delivered this boot, using files already on disk\n");
        { size_t i = 0; put(&i, "OSIMG: no image, modules="); put_num(&i, (uint64_t)os_boot_module_count()); g_status[i] = 0; }
        return;
    }
    if (size < 8 || img[0] != 'T' || img[1] != 'R' || img[2] != 'A' || img[3] != 'S') {
        serial_puts("[OSIMG] bad magic, skipping\n");
        set_status("OS image: bad magic, size ", (int)(size / 1024));
        return;
    }
    uint32_t count;
    memcpy(&count, img + 4, 4);
    uint64_t table_bytes = (uint64_t)count * sizeof(osimage_entry_t);
    if (8 + table_bytes > size) {
        serial_puts("[OSIMG] entry table exceeds image, skipping\n");
        return;
    }
    const osimage_entry_t *table = (const osimage_entry_t *)(img + 8);

    uint64_t h = 1469598103934665603ULL;
    for (uint64_t b = 0; b < size; b++) {
        h ^= img[b];
        h *= 1099511628211ULL;
    }
    static const char hex[] = "0123456789ABCDEF";
    char stamp[24];
    for (int d = 0; d < 16; d++) stamp[d] = hex[(h >> (60 - d * 4)) & 0xF];
    stamp[16] = '\0';
    char have[24];
    int up_to_date = syscfg_get("osimage_stamp", have, sizeof(have)) && strcmp(have, stamp) == 0;

    int wrote = 0;
    g_total = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        const osimage_entry_t *e = &table[i];
        if ((uint64_t)e->data_offset + e->data_len > size) {
            serial_puts("[OSIMG] entry out of range, skipping\n");
            continue;
        }
        char path[129];
        memcpy(path, e->path, 128);
        path[128] = '\0';
        if (up_to_date) {
            inode_t st;
            if (fs_stat(path, &st) == 0 && st.size == e->data_len) continue;
        }
        os_write_file(path, img + e->data_offset, e->data_len);
        wrote = 1;
        {
            inode_t st2;
            if (fs_stat(path, &st2) == 0 && st2.size == e->data_len) g_applied++; else g_failed++;
        }
    }
    if (!up_to_date || wrote) syscfg_set("osimage_stamp", stamp);
    {
        size_t i = 0;
        put(&i, g_failed ? "OSIMG: FAILED " : "OSIMG: ok ");
        put_num(&i, (uint64_t)(g_failed ? g_failed : g_applied));
        put(&i, "/"); put_num(&i, (uint64_t)g_total);
        put(&i, " files, ");
        put_num(&i, size / 1024); put(&i, "KB");
        g_status[i] = 0;
    }
    serial_puts(g_status);
    serial_puts("\n");
}
