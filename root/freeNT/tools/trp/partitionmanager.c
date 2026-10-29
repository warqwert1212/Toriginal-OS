/* partitionmanager.c */
#include "trp_rt.h"

#define ALIGN_SECTORS 2048u          /* 1 MiB */
#define MIN_SYSTEM_MIB 128u

typedef struct {
    u8  boot;
    u8  chs_s[3];
    u8  type;
    u8  chs_e[3];
    u32 start;
    u32 count;
} __attribute__((packed)) pe_t;

static u8        g_sector[512];
static pe_t     *g_pe;                 /* -> g_sector + 446 */
static disk_info_t g_disk;
static int       g_target = -1;        /* chosen system slot */
static int       g_dirty;

static u64 mib(u64 sectors) { return sectors / 2048ULL; }
static u64 align_up(u64 v) { return (v + ALIGN_SECTORS - 1) / ALIGN_SECTORS * ALIGN_SECTORS; }
static u64 align_down(u64 v) { return v / ALIGN_SECTORS * ALIGN_SECTORS; }

static const char *type_name(u8 t) {
    switch (t) {
    case 0x83: return "Toriginal (TRPFS)";
    case 0x07: return "Data";
    case 0x0C: return "Data (FAT32)";
    case 0x00: return "-";
    default:   return "Other";
    }
}

static void load_table(void) {
    trp_sys(SYS_PART_READ, (long)g_sector, 0, 0);
    if (g_sector[510] != 0x55 || g_sector[511] != 0xAA) {
        t_memset(g_sector + 446, 0, 64);           /* blank/unknown disk */
    }
    g_pe = (pe_t *)(g_sector + 446);
    /* Anything not a valid partition for our rules (e.g. the stage1 placeholder
     * entry with start LBA 0) is dropped from the editable copy. */
    for (int i = 0; i < 4; i++) {
        if (g_pe[i].count && g_pe[i].start < g_disk.boot_reserved) t_memset(&g_pe[i], 0, 16);
    }
    g_dirty = 0;
    g_target = -1;
}

__attribute__((unused)) static int used_slots(void) { int n = 0; for (int i = 0; i < 4; i++) if (g_pe[i].count) n++; return n; }

/* Free gaps (aligned), sorted by start. Returns count; gap_start/gap_len in sectors. */
static int find_gaps(u64 *gs, u64 *gl, int max) {
    u64 order_start[4]; u64 order_end[4]; int n = 0;
    for (int i = 0; i < 4; i++) if (g_pe[i].count) {
        order_start[n] = g_pe[i].start; order_end[n] = (u64)g_pe[i].start + g_pe[i].count; n++;
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
        if (order_start[j] < order_start[i]) {
            u64 a = order_start[i]; order_start[i] = order_start[j]; order_start[j] = a;
            a = order_end[i]; order_end[i] = order_end[j]; order_end[j] = a;
        }
    int g = 0;
    u64 cur = align_up(g_disk.boot_reserved);
    u64 disk_end = align_down(g_disk.total_sectors);
    /* MBR start/length fields are 32-bit: on a disk over 2 TiB only the
     * first 2 TiB can be partitioned. */
    if (disk_end > 0xFFFFF800ULL) disk_end = 0xFFFFF800ULL;
    for (int i = 0; i <= n; i++) {
        u64 next = (i < n) ? order_start[i] : disk_end;
        if (next > cur && next - cur >= ALIGN_SECTORS && g < max) {
            gs[g] = cur; gl[g] = align_down(next) - cur; if (gl[g] >= ALIGN_SECTORS) g++;
        }
        if (i < n && order_end[i] > cur) cur = align_up(order_end[i]);
    }
    return g;
}


#define K_UP    0x83
#define K_DOWN  0x84
#define K_ESC   27

static int key_(void) {
    for (;;) {
        int c = getch_();
        if (c == '\r' || c == '\n') return '\n';
        if (c == K_UP || c == K_DOWN || c == K_ESC || c == 0x81 || c == 0x82) return c;
        if (c >= 0x20 && c < 0x7F) return c;
    }
}

static void header(void) {
    clear_();
    color_(15, 1);
    puts_("  TORIGINAL OS  -  PARTITION MANAGER          \n");
    color_(7, 0);
}

static void draw_info(void) {
    header();
    puts_("Disk: "); puts_(g_disk.model[0] ? g_disk.model : "ATA disk");
    puts_("  "); putu(mib(g_disk.total_sectors)); puts_(" MiB\n");
    puts_("Boot area (reserved): first "); putu(mib(g_disk.boot_reserved) + 1); puts_(" MiB\n\n");
    color_(14, 0);
    puts_(" # "); put_pad("Type", 19); put_pad("Start", 8); put_pad("Size MiB", 9); puts_("Role\n");
    color_(7, 0);
    for (int i = 0; i < 4; i++) {
        putc_(' '); putu((u64)(i + 1)); putc_(' ');
        if (!g_pe[i].count) { color_(8, 0); puts_("(empty)\n"); color_(7, 0); continue; }
        put_pad(type_name(g_pe[i].type), 19);
        put_upad(mib(g_pe[i].start), 8);
        put_upad(mib(g_pe[i].count), 9);
        if (i == g_target) { color_(10, 0); puts_("<= INSTALL HERE"); color_(7, 0); }
        putc_('\n');
    }
    u64 gs[6], gl[6]; int ng = find_gaps(gs, gl, 6);
    u64 freeMiB = 0; for (int i = 0; i < ng; i++) freeMiB += mib(gl[i]);
    puts_("\nFree space: "); putu(freeMiB); puts_(" MiB in "); putu((u64)ng); puts_(" gap(s)\n");
    puts_("Install target: ");
    if (g_target >= 0) { color_(10, 0); puts_("partition "); putu((u64)(g_target + 1)); }
    else { color_(12, 0); puts_("not chosen yet"); }
    color_(7, 0);
    puts_("\n\n");
}

static void hint(const char *text) {
    color_(11, 0); puts_(text); color_(7, 0); putc_('\n');
}

static int pick(const char *title, const char **items, int n, int sel, const char *esc_text) {
    for (;;) {
        draw_info();
        color_(14, 0); puts_(title); color_(7, 0); puts_("\n\n");
        for (int i = 0; i < n; i++) {
            if (i == sel) { color_(0, 15); puts_(" > "); put_pad(items[i], 44); color_(7, 0); }
            else { puts_("   "); puts_(items[i]); }
            putc_('\n');
        }
        puts_("\n");
        hint("Up/Down = move    Enter = OK");
        hint(esc_text);
        int k = key_();
        if (k == K_UP && sel > 0) sel--;
        else if (k == K_DOWN && sel < n - 1) sel++;
        else if (k == '\n') return sel;
        else if (k == K_ESC) return -1;
    }
}

static int confirm(const char *l1, const char *l2) {
    draw_info();
    color_(14, 0); puts_(l1); color_(7, 0); putc_('\n');
    if (l2) { puts_(l2); putc_('\n'); }
    puts_("\n");
    hint("Enter = OK    Esc = No");
    for (;;) {
        int k = key_();
        if (k == '\n') return 1;
        if (k == K_ESC) return 0;
    }
}

static void pause_msg(const char *m) {
    draw_info();
    color_(12, 0); puts_(m); color_(7, 0);
    puts_("\n\n");
    hint("Enter = OK");
    for (;;) { int k = key_(); if (k == '\n' || k == K_ESC) return; }
}

static int read_num_esc(const char *title, u64 *out) {
    char b[16]; int n = 0;
    for (;;) {
        draw_info();
        color_(14, 0); puts_(title); color_(7, 0); puts_("\n\n> ");
        b[n] = 0; puts_(b);
        puts_("\n\n");
        hint("Type digits    Enter = OK    Esc = cancel");
        int k = key_();
        if (k == K_ESC) return 0;
        if (k == '\n') { if (n && parse_u64(b, out)) return 1; continue; }
        if ((k == '\b' || k == 127) && n > 0) n--;
        else if (k >= '0' && k <= '9' && n < 10) b[n++] = (char)k;
    }
}

static char g_lines[8][64];

static void fmt_part(char *dst, int i) {
    char tmp[24]; int o = 0, l;
    dst[o++] = (char)('1' + i); dst[o++] = ' '; dst[o++] = ' ';
    const char *t = type_name(g_pe[i].type);
    while (*t && o < 30) dst[o++] = *t++;
    while (o < 24) dst[o++] = ' ';
    u64 v = mib(g_pe[i].count); int tl = 0;
    if (!v) tmp[tl++] = '0';
    while (v) { tmp[tl++] = (char)('0' + v % 10); v /= 10; }
    for (l = tl - 1; l >= 0; l--) dst[o++] = tmp[l];
    dst[o++] = ' '; dst[o++] = 'M'; dst[o++] = 'i'; dst[o++] = 'B'; dst[o] = 0;
}

static int pick_partition(const char *title, int need_target_ok) {
    const char *items[4]; int idx[4]; int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!g_pe[i].count) continue;
        if (need_target_ok && (g_pe[i].type != 0x83 || mib(g_pe[i].count) < MIN_SYSTEM_MIB)) continue;
        fmt_part(g_lines[n], i); items[n] = g_lines[n]; idx[n] = i; n++;
    }
    if (!n) { pause_msg(need_target_ok ? "No suitable Toriginal partition (needs 128 MiB or more)." : "There are no partitions."); return -1; }
    int r = pick(title, items, n, 0, "Esc = cancel");
    return r < 0 ? -1 : idx[r];
}

static void do_auto(void) {
    if (!confirm("Use the whole disk?", "This removes ALL partitions and creates one Toriginal OS partition.")) return;
    u64 start = align_up(g_disk.boot_reserved);
    u64 end = align_down(g_disk.total_sectors);
    if (end > 0xFFFFF800ULL) end = 0xFFFFF800ULL;
    u64 len = end - start;
    if (mib(len) < MIN_SYSTEM_MIB) { pause_msg("Disk is too small (need at least 128 MiB)."); return; }
    t_memset(g_pe, 0, 64);
    g_pe[0].type = 0x83; g_pe[0].start = (u32)start; g_pe[0].count = (u32)len;
    g_target = 0; g_dirty = 1;
}

static void do_new(void) {
    int slot = -1;
    for (int i = 0; i < 4; i++) if (!g_pe[i].count) { slot = i; break; }
    if (slot < 0) { pause_msg("All 4 partition slots are in use. Delete one first."); return; }
    u64 gs[6], gl[6]; int ng = find_gaps(gs, gl, 6);
    if (!ng) { pause_msg("No free space. Delete a partition first."); return; }

    int gi = 0;
    if (ng > 1) {
        const char *items[6];
        for (int i = 0; i < ng; i++) {
            char *d = g_lines[i]; int o = 0; char tmp[24]; int tl;
            const char *a = "Gap at "; while (*a) d[o++] = *a++;
            u64 v = mib(gs[i]); tl = 0; if (!v) tmp[tl++] = '0'; while (v) { tmp[tl++] = (char)('0' + v % 10); v /= 10; }
            while (tl) d[o++] = tmp[--tl];
            a = " MiB, "; while (*a) d[o++] = *a++;
            v = mib(gl[i]); tl = 0; if (!v) tmp[tl++] = '0'; while (v) { tmp[tl++] = (char)('0' + v % 10); v /= 10; }
            while (tl) d[o++] = tmp[--tl];
            a = " MiB free"; while (*a) d[o++] = *a++;
            d[o] = 0; items[i] = d;
        }
        gi = pick("Create the partition in which free space?", items, ng, 0, "Esc = cancel");
        if (gi < 0) return;
    }

    const char *sz_items[2] = { "Use all of this free space", "Enter a size in MiB" };
    int sz = pick("Partition size", sz_items, 2, 0, "Esc = cancel");
    if (sz < 0) return;
    u64 len = gl[gi], v;
    if (sz == 1) {
        if (!read_num_esc("Size in MiB:", &v)) return;
        if (v < 1) { pause_msg("Invalid size."); return; }
        len = align_down(v * 2048ULL);
    }
    if (len > gl[gi]) { pause_msg("That is larger than the free space."); return; }
    if (len < ALIGN_SECTORS) { pause_msg("Too small."); return; }

    const char *ty_items[2] = { "Toriginal OS system", "Data" };
    int ty = pick("Partition type", ty_items, 2, 0, "Esc = cancel");
    if (ty < 0) return;
    u8 type = ty == 1 ? 0x07 : 0x83;
    if (type == 0x83 && mib(len) < MIN_SYSTEM_MIB) { pause_msg("A system partition needs at least 128 MiB."); return; }
    g_pe[slot].type = type; g_pe[slot].start = (u32)gs[gi]; g_pe[slot].count = (u32)len;
    g_pe[slot].boot = 0;
    if (type == 0x83 && g_target < 0) g_target = slot;
    g_dirty = 1;
}

static void do_delete(void) {
    int s = pick_partition("Delete which partition?", 0);
    if (s < 0) return;
    if (!confirm("Delete this partition?", "Its data will be lost when the table is written.")) return;
    t_memset(&g_pe[s], 0, 16);
    if (g_target == s) g_target = -1;
    g_dirty = 1;
}

static void do_select(void) {
    int s = pick_partition("Install Toriginal OS on which partition?", 1);
    if (s >= 0) g_target = s;
}

static void do_write(void) {
    if (g_target < 0) { pause_msg("Choose an install target first."); return; }
    if (!confirm("Ready to install. The target partition will be ERASED.",
                 "The partition table is written to the disk now.")) return;

    if (trp_sys(SYS_PART_WRITE, (long)g_sector, 0, 0) != 0) {
        pause_msg("The kernel rejected the partition table."); return;
    }
    if (trp_sys(SYS_INSTALL_CTL, INSTALL_CTL_SELECT_TARGET, g_target, 0) != 0) {
        pause_msg("The kernel rejected that install target."); return;
    }
    header();
    puts_("\nPartition table written. Starting installation...\n");
    trp_exit(0);
}

int trp_main(void) {
    trp_sys(SYS_DISK_INFO, (long)&g_disk, 0, 0);
    if (!g_disk.present) {
        header();
        color_(12, 0); puts_("\nNo ATA disk found - nothing to install onto.\n");
        color_(7, 0); puts_("Attach a hard disk and restart.\n\n(press any key)\n");
        getch_();
        return 1;
    }
    load_table();

    const char *menu[] = {
        "Use whole disk", "New partition", "Delete partition",
        "Select install target", "Write + install", "Reset changes", "Quit"
    };
    int sel = 4;
    for (;;) {
        int c = pick("What do you want to do?", menu, 7, sel, "Esc = quit");
        if (c < 0) {
            if (confirm("Cancel the installation?", 0)) return 1;
            continue;
        }
        sel = c;
        switch (c) {
        case 0: do_auto(); break;
        case 1: do_new(); break;
        case 2: do_delete(); break;
        case 3: do_select(); break;
        case 4: do_write(); break;
        case 5: load_table(); break;
        case 6: if (confirm("Cancel the installation?", 0)) return 1; break;
        }
    }
}
