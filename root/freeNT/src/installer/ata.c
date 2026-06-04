#include <stdint.h>

// Standard Primary IDE Controller I/O Ports
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7

// ATA Commands
#define ATA_CMD_WRITE_SECTORS    0x30
#define ATA_CMD_STATUS_BUSY      0x80
#define ATA_CMD_STATUS_DRQ       0x08

// Inline assembly wrappers for hardware I/O ports
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    // FIX: Fixed structural register assignment constraints for robust compilation output
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// Low-level function to write a single 512-byte sector to disk
void ata_write_sector(uint32_t lba, uint8_t *buffer) {
    // 1. Wait for drive to clear the BUSY status bit
    while (inb(ATA_PRIMARY_STATUS) & ATA_CMD_STATUS_BUSY);

    // 2. Select the primary master drive and pass highest bits of LBA
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));

    // 400ns Delay sequence
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);

    // 3. Send parameters: sector count (1) and LBA byte segments
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW,  (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(lba >> 16));

    // 4. Issue the write command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

    // Tiny status delay loop to allow hardware to assert the busy/drq registers
    for(volatile int g = 0; g < 1000; g++);

    // 5. Wait until drive is ready to receive data payload (DRQ bit set)
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_CMD_STATUS_DRQ));

    // 6. Stream 256 words (512 bytes) from system RAM directly into port
    for (int i = 0; i < 256; i++) {
        uint16_t word = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw(ATA_PRIMARY_DATA, word);
    }
}