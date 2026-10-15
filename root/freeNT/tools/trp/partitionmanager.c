/* partitionmanager.trp - install-time partition manager (terminal UI).
 *
 * Runs from the live CD before anything is installed. It edits the MBR
 * partition table through SYS_PART_READ / SYS_PART_WRITE (the kernel
 * validates and refuses anything overlapping the TRBL boot region),
 * picks which partition Toriginal OS goes on, and exits 0 so the kernel
 * can format that partition and install. Exit 1 = cancelled.
 */
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

static void header(void) {
    clear_();
    color_(15, 1);
    puts_("  TORIGINAL OS  -  PARTITION MANAGER          \n");
    color_(7, 0);
}

static void draw(void) {
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
    color_(11, 0);
    puts_(" [A] Use whole disk   [N] New   [D] Delete\n");
    puts_(" [S] Select target    [W] Write + install\n");
    puts_(" [R] Reset            [Q] Quit\n");
    color_(7, 0);
    puts_("\n> ");
}

static void pause_msg(const char *m) {
    color_(12, 0); puts_(m); color_(7, 0);
    puts_("\n(press any key)\n"); getch_();
}

static int ask_slot(const char *prompt, int want_used) {
    char b[8]; u64 v;
    puts_(prompt); readline_(b, sizeof(b), 0);
    if (!parse_u64(b, &v) || v < 1 || v > 4) return -1;
    int s = (int)v - 1;
    if (want_used && !g_pe[s].count) return -1;
    if (!want_used && g_pe[s].count) return -1;
    return s;
}

static void do_auto(void) {
    puts_("\nThis removes ALL partitions on the disk and creates one Toriginal OS\n");
    puts_("partition using the whole disk. Type YES to continue: ");
    char b[8]; readline_(b, sizeof(b), 0);
    if (!t_streq(b, "YES")) return;
    t_memset(g_pe, 0, 64);
    u64 start = align_up(g_disk.boot_reserved);
    u64 end = align_down(g_disk.total_sectors);
    if (end > 0xFFFFF800ULL) end = 0xFFFFF800ULL;
    u64 len = end - start;
    if (mib(len) < MIN_SYSTEM_MIB) { pause_msg("Disk is too small (need at least 128 MiB)."); return; }
    g_pe[0].type = 0x83; g_pe[0].start = (u32)start; g_pe[0].count = (u32)len;
    g_target = 0; g_dirty = 1;
}

static void do_new(void) {
    int slot = -1;
    for (int i = 0; i < 4; i++) if (!g_pe[i].count) { slot = i; break; }
    if (slot < 0) { pause_msg("All 4 partition slots are in use. Delete one first."); return; }
    u64 gs[6], gl[6]; int ng = find_gaps(gs, gl, 6);
    if (!ng) { pause_msg("No free space. Delete a partition first."); return; }
    puts_("\nFree gaps:\n");
    for (int i = 0; i < ng; i++) {
        puts_("  "); putu((u64)(i + 1)); puts_(") at "); putu(mib(gs[i])); puts_(" MiB, "); putu(mib(gl[i])); puts_(" MiB free\n");
    }
    char b[16]; u64 v;
    puts_("Gap number: "); readline_(b, sizeof(b), 0);
    if (!parse_u64(b, &v) || v < 1 || (int)v > ng) { pause_msg("Invalid gap."); return; }
    int gi = (int)v - 1;
    puts_("Size in MiB (or 'max'): "); readline_(b, sizeof(b), 0);
    u64 len;
    if (t_streq(b, "max")) len = gl[gi];
    else { if (!parse_u64(b, &v) || v < 1) { pause_msg("Invalid size."); return; } len = align_down(v * 2048ULL); }
    if (len > gl[gi]) { pause_msg("That is larger than the free gap."); return; }
    if (len < ALIGN_SECTORS) { pause_msg("Too small."); return; }
    puts_("Type: 1) Toriginal OS system  2) Data : "); readline_(b, sizeof(b), 0);
    u8 type = (b[0] == '2') ? 0x07 : 0x83;
    if (type == 0x83 && mib(len) < MIN_SYSTEM_MIB) { pause_msg("A system partition needs at least 128 MiB."); return; }
    g_pe[slot].type = type; g_pe[slot].start = (u32)gs[gi]; g_pe[slot].count = (u32)len;
    g_pe[slot].boot = 0;
    if (type == 0x83 && g_target < 0) g_target = slot;
    g_dirty = 1;
}

static void do_delete(void) {
    int s = ask_slot("\nDelete which partition (1-4): ", 1);
    if (s < 0) { pause_msg("No such partition."); return; }
    puts_("Type YES to delete partition "); putu((u64)(s + 1)); puts_(": ");
    char b[8]; readline_(b, sizeof(b), 0);
    if (!t_streq(b, "YES")) return;
    t_memset(&g_pe[s], 0, 16);
    if (g_target == s) g_target = -1;
    g_dirty = 1;
}

static void do_select(void) {
    int s = ask_slot("\nInstall Toriginal OS on partition (1-4): ", 1);
    if (s < 0) { pause_msg("No such partition."); return; }
    if (g_pe[s].type != 0x83) { pause_msg("Only a 'Toriginal (TRPFS)' partition can be the target."); return; }
    if (mib(g_pe[s].count) < MIN_SYSTEM_MIB) { pause_msg("That partition is smaller than 128 MiB."); return; }
    g_target = s;
}

static void do_write(void) {
    if (g_target < 0) { pause_msg("Choose an install target first ([S])."); return; }
    header();
    color_(14, 0);
    puts_("\nReady to install.\n\n");
    color_(7, 0);
    puts_("  Partition "); putu((u64)(g_target + 1)); puts_(" ("); putu(mib(g_pe[g_target].count));
    puts_(" MiB) will be ERASED and formatted for Toriginal OS.\n");
    puts_("  The partition table will be written to the disk now.\n\n");
    puts_("Type INSTALL to continue, anything else to go back: ");
    char b[16]; readline_(b, sizeof(b), 0);
    if (!t_streq(b, "INSTALL")) return;

    if (trp_sys(SYS_PART_WRITE, (long)g_sector, 0, 0) != 0) {
        pause_msg("The kernel rejected the partition table."); return;
    }
    if (trp_sys(SYS_INSTALL_CTL, INSTALL_CTL_SELECT_TARGET, g_target, 0) != 0) {
        pause_msg("The kernel rejected that install target."); return;
    }
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

    for (;;) {
        draw();
        int c = t_lower((char)getch_());
        putc_(c); putc_('\n');
        switch (c) {
        case 'a': do_auto(); break;
        case 'n': do_new(); break;
        case 'd': do_delete(); break;
        case 's': do_select(); break;
        case 'w': do_write(); break;
        case 'r': load_table(); break;
        case 'q': {
            puts_("\nCancel the installation? Type YES: ");
            char b[8]; readline_(b, sizeof(b), 0);
            if (t_streq(b, "YES")) return 1;
            break;
        }
        default: break;
        }
    }
}
