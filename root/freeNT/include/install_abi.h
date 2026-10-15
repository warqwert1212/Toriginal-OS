#ifndef _INSTALL_ABI_H
#define _INSTALL_ABI_H
/* Shared kernel <-> TRP ABI for the install-time programs
 * (partitionmanager.trp, teroobe.trp). Included by BOTH the kernel and
 * the freestanding TRP sources under tools/trp/, so it only uses
 * fixed-width integer types - no kernel headers. */

#define SYS_DISK_INFO     70  /* (disk_info_t *out)                      */
#define SYS_PART_READ     71  /* (void *mbr512_out)                      */
#define SYS_PART_WRITE    72  /* (const void *mbr512_in) install-only    */
#define SYS_INSTALL_CTL   73  /* (cmd, arg)  see INSTALL_CTL_*           */
#define SYS_OOBE_SUBMIT   74  /* (const char *kv_text, len) install-only */
#define SYS_CONSOLE_CTL   75  /* (cmd, a, b) see CONSOLE_CTL_*           */
#define SYS_SYSINFO       76  /* (sysinfo_t *out)                        */

typedef struct {
    unsigned int       present;          /* 1 if drive 0 exists            */
    unsigned int       boot_reserved;    /* first usable partition LBA     */
    unsigned long long total_sectors;
    unsigned int       sector_size;      /* always 512                     */
    unsigned int       install_session;  /* 1 while the installer runs us  */
    char               model[48];
} disk_info_t;

typedef struct {
    unsigned long long ram_kb;           /* usable RAM the PMM reports     */
    unsigned int       fb_width;
    unsigned int       fb_height;
    unsigned int       has_nic;
    unsigned int       _pad;
} sysinfo_t;

/* SYS_INSTALL_CTL commands */
#define INSTALL_CTL_SELECT_TARGET 1  /* arg = MBR slot 0..3 -> 0 / -1  */
#define INSTALL_CTL_GET_TARGET    2  /* returns slot or -1             */
#define INSTALL_CTL_DONE          3  /* partition manager finished     */

/* SYS_CONSOLE_CTL commands */
#define CONSOLE_CTL_CLEAR     1
#define CONSOLE_CTL_SETCOLOR  2      /* a = fg 0..15, b = bg 0..15     */

/* The partition manager reserves this LBA as the start of the first
 * partition (== ATA_BOOT_RESERVED_SECTORS in ata.h). */
#define INSTALL_PART_ALIGN_LBA 70048u

#endif
