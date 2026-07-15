#include "ata.h"
#include "trpfs.h"
#include "string.h"
#include "serial.h"

#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE_HEAD  0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_SECTOR_SIZE   512u
#define SECTORS_PER_BLOCK (TRPFS_BLOCK_SIZE / ATA_SECTOR_SIZE)

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    int      slave;
    int      present;
} ata_channel_t;

static ata_channel_t g_channels[4] = {
    { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   0, 0 },
    { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   1, 0 },
    { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 0, 0 },
    { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 1, 0 },
};

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

static int ata_wait_not_busy(uint16_t io_base) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(inb(io_base + ATA_REG_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(uint16_t io_base) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t s = inb(io_base + ATA_REG_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int ata_detect_channel(int index) {
    ata_channel_t *ch = &g_channels[index];

    outb(ch->io_base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xA0 | (ch->slave << 4)));
    outb(ch->io_base + ATA_REG_SECCOUNT, 0);
    outb(ch->io_base + ATA_REG_LBA_LO, 0);
    outb(ch->io_base + ATA_REG_LBA_MID, 0);
    outb(ch->io_base + ATA_REG_LBA_HI, 0);
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ch->io_base + ATA_REG_STATUS);
    if (status == 0) { ch->present = 0; return 0; }

    if (ata_wait_not_busy(ch->io_base) != 0) { ch->present = 0; return 0; }

    if (inb(ch->io_base + ATA_REG_LBA_MID) != 0 || inb(ch->io_base + ATA_REG_LBA_HI) != 0) {
        ch->present = 0;
        return 0;
    }

    if (ata_wait_drq(ch->io_base) != 0) { ch->present = 0; return 0; }

    for (int i = 0; i < 256; i++) (void)inw(ch->io_base + ATA_REG_DATA);

    ch->present = 1;
    return 1;
}

int ata_detect(void) {
    return ata_detect_channel(0);
}

int ata_is_present(void) {
    return g_channels[0].present;
}

int ata_detect_all(void) {
    int found = 0;
    for (int i = 0; i < 4; i++) {
        if (ata_detect_channel(i)) found++;
    }
    return found;
}

int ata_drive_present(int drive_index) {
    if (drive_index < 0 || drive_index >= 4) return 0;
    return g_channels[drive_index].present;
}

static int ata_read_sector(ata_channel_t *ch, uint32_t lba, void *buf) {
    if (ata_wait_not_busy(ch->io_base) != 0) return -1;

    outb(ch->io_base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xE0 | (ch->slave << 4) | ((lba >> 24) & 0x0F)));
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_READ);

    if (ata_wait_drq(ch->io_base) != 0) return -1;

    uint16_t *p = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) p[i] = inw(ch->io_base + ATA_REG_DATA);
    return 0;
}

static int ata_write_sector(ata_channel_t *ch, uint32_t lba, const void *buf) {
    if (ata_wait_not_busy(ch->io_base) != 0) return -1;

    outb(ch->io_base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xE0 | (ch->slave << 4) | ((lba >> 24) & 0x0F)));
    outb(ch->io_base + ATA_REG_SECCOUNT, 1);
    outb(ch->io_base + ATA_REG_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ch->io_base + ATA_REG_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE);

    if (ata_wait_drq(ch->io_base) != 0) return -1;

    const uint16_t *p = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++) outw(ch->io_base + ATA_REG_DATA, p[i]);

    if (ata_wait_not_busy(ch->io_base) != 0) return -1;

    outb(ch->io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_not_busy(ch->io_base) != 0) return -1;
    return 0;
}

typedef struct {
    ata_channel_t *channel;
} ata_blkdev_ctx_t;

static ata_blkdev_ctx_t g_blkdev_ctx[4];

static int ata_blk_read(trpfs_blkdev_t *dev, uint64_t lba, void *buf) {
    ata_blkdev_ctx_t *ctx = (ata_blkdev_ctx_t *)dev->ctx;
    ata_channel_t *ch = ctx->channel;
    uint32_t base = (uint32_t)(lba * SECTORS_PER_BLOCK);
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; i++) {
        if (ata_read_sector(ch, base + i, out + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static int ata_blk_write(trpfs_blkdev_t *dev, uint64_t lba, const void *buf) {
    ata_blkdev_ctx_t *ctx = (ata_blkdev_ctx_t *)dev->ctx;
    ata_channel_t *ch = ctx->channel;
    uint32_t base = (uint32_t)(lba * SECTORS_PER_BLOCK);
    const uint8_t *in = (const uint8_t *)buf;
    for (uint32_t i = 0; i < SECTORS_PER_BLOCK; i++) {
        if (ata_write_sector(ch, base + i, in + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static trpfs_blkdev_t g_ata_devs[4];

trpfs_blkdev_t *ata_init_blkdev_drive(int drive_index, uint64_t total_bytes) {
    if (drive_index < 0 || drive_index >= 4) return NULL;

    if (!g_channels[drive_index].present && !ata_detect_channel(drive_index)) {
        serial_puts("[ATA] No disk detected on requested drive.\n");
        return NULL;
    }

    uint64_t blocks = total_bytes / TRPFS_BLOCK_SIZE;
    g_blkdev_ctx[drive_index].channel = &g_channels[drive_index];

    g_ata_devs[drive_index].read_block   = ata_blk_read;
    g_ata_devs[drive_index].write_block  = ata_blk_write;
    g_ata_devs[drive_index].total_blocks = blocks;
    g_ata_devs[drive_index].ctx          = &g_blkdev_ctx[drive_index];

    serial_puts("[ATA] Disk detected, persistent storage ready.\n");
    return &g_ata_devs[drive_index];
}

trpfs_blkdev_t *ata_init_blkdev(uint64_t total_bytes) {
    return ata_init_blkdev_drive(0, total_bytes);
}

