/* =============================================================================
 * ata.c - ATA PIO disk driver (primary master, 28-bit LBA)
 *
 * Provides real persistent block storage so TRPFS can survive reboots.
 * QEMU/VirtualBox both attach an IDE/ATA hard disk by default, so this
 * works out of the box with `make iso` + a virtual HDD attached, or with
 * QEMU's `-hda disk.img` flag.
 *
 * 512-byte ATA sectors are aggregated 8-at-a-time to match TRPFS's
 * 4096-byte block size (TRPFS_BLOCK_SIZE).
 * ========================================================================= */

#include "ata.h"
#include "trpfs.h"
#include "string.h"
#include "serial.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_SECTOR_SIZE   512u
#define SECTORS_PER_BLOCK (TRPFS_BLOCK_SIZE / ATA_SECTOR_SIZE)  /* 4096/512 = 8 */

static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(port));
}

static int g_ata_present = 0;

/* Wait until BSY clears. Returns -1 on timeout. */
static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* Wait until DRQ is set (data ready) or ERR is set. */
static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

/* ata_detect - probe for a primary master ATA drive via IDENTIFY.
 * Returns 1 if present, 0 if not. Safe to call even with no disk attached
 * (QEMU/VirtualBox without a configured HDD simply won't respond). */
int ata_detect(void) {
    outb(ATA_DRIVE_HEAD, 0xA0); /* select master, LBA mode bit unset for IDENTIFY */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0) { g_ata_present = 0; return 0; } /* no drive at all */

    if (ata_wait_not_busy() != 0) { g_ata_present = 0; return 0; }

    /* Some non-ATA devices (ATAPI) leave LBA_MID/LBA_HI nonzero here. */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) { g_ata_present = 0; return 0; }

    if (ata_wait_drq() != 0) { g_ata_present = 0; return 0; }

    /* Drain the 256-word IDENTIFY data, we don't need its contents for v1. */
    for (int i = 0; i < 256; i++) (void)inw(ATA_DATA);

    g_ata_present = 1;
    return 1;
}

int ata_is_present(void) { return g_ata_present; }

/* Read one 512-byte sector via 28-bit LBA PIO. */
static int ata_read_sector(uint32_t lba, void *buf) {
    if (ata_wait_not_busy() != 0) return -1;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (ata_wait_drq() != 0) return -1;

    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) p[i] = inw(ATA_DATA);
    return 0;
}

/* Write one 512-byte sector via 28-bit LBA PIO, then flush cache. */
static int ata_write_sector(uint32_t lba, const void *buf) {
    if (ata_wait_not_busy() != 0) return -1;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    if (ata_wait_drq() != 0) return -1;

    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++) outw(ATA_DATA, p[i]);

    /* The drive reasserts BSY internally right after the last data word
     * while it finishes committing the sector — issuing FLUSH CACHE
     * immediately after the write loop races that busy window and PIIX3
     * (correctly) rejects it: "guest issued command 0xe7 while controller
     * busy". Wait for BSY to clear from the write itself before flushing. */
    if (ata_wait_not_busy() != 0) return -1;

    /* Flush cache so the write is durable before we report success. */
    outb(ATA_COMMAND, 0xE7);
    if (ata_wait_not_busy() != 0) return -1;
    return 0;
}

/* ── trpfs_blkdev_t adapter - 4096-byte TRPFS block = 8 ATA sectors ─────── */

static int ata_blk_read(trpfs_blkdev_t *dev, uint64_t lba, void *buf) {
    (void)dev;
    uint32_t base = (uint32_t)(lba * SECTORS_PER_BLOCK);
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; i++) {
        if (ata_read_sector(base + i, out + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static int ata_blk_write(trpfs_blkdev_t *dev, uint64_t lba, const void *buf) {
    (void)dev;
    uint32_t base = (uint32_t)(lba * SECTORS_PER_BLOCK);
    const uint8_t *in = (const uint8_t *)buf;
    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; i++) {
        if (ata_write_sector(base + i, in + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static trpfs_blkdev_t g_ata_dev;

/* ata_init_blkdev - fill in a trpfs_blkdev_t backed by the real disk.
 * total_bytes: how much of the disk to expose (e.g. 64 MiB for v1).
 * Returns a pointer to the device on success, NULL if no ATA disk present. */
trpfs_blkdev_t *ata_init_blkdev(uint64_t total_bytes) {
    if (!g_ata_present && !ata_detect()) {
        serial_puts("[ATA] No disk detected - persistence unavailable\n");
        return NULL;
    }

    uint64_t blocks = total_bytes / TRPFS_BLOCK_SIZE;
    g_ata_dev.read_block   = ata_blk_read;
    g_ata_dev.write_block  = ata_blk_write;
    g_ata_dev.total_blocks = blocks;
    g_ata_dev.ctx          = NULL;

    serial_puts("[ATA] Disk detected, persistent storage ready.\n");
    return &g_ata_dev;
}
