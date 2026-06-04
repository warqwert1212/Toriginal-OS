#include <stdint.h>
#include <string.h>
#include "include/mbr.h"
#include "fs.h"
#include "io.h"

// Forward declaration of low-level ATA driver routine
void ata_write_sector(uint32_t lba, uint8_t *buffer);

// References to your embedded user mode shell binary data space
extern uint8_t _binary_toriginal_shell_bin_start[];
// Declaring this as an opaque extern character allows taking its address safely for size resolution
extern uint8_t _binary_toriginal_shell_bin_size; 

void execute_system_installer(void) {
    uint8_t sector_scratchpad[512];
    memset(sector_scratchpad, 0, 512);

    serial_puts("[Installer] Commencing physical disk partition mapping...\n");

    // 1. Initialize the Master Boot Record structure on Sector 0
    mbr_t *installation_mbr = (mbr_t *)sector_scratchpad;
    memset(installation_mbr->bootstrap_code, 0, 446);
    
    // Create primary active partition starting safely at LBA block 2048
    installation_mbr->partitions[0].boot_indicator = 0x80;
    installation_mbr->partitions[0].partition_type = 0x83; 
    installation_mbr->partitions[0].start_lba = 2048;
    installation_mbr->partitions[0].sector_count = 40960; // 20 Megabytes allocation
    installation_mbr->boot_signature = 0xAA55;
    
    // Commit the structured partition block to physical sector zero
    ata_write_sector(0, sector_scratchpad);

    // FIX: Correctly resolve the size integer from the linker symbol address mapping
    uint64_t total_payload_bytes = (uint64_t)&_binary_toriginal_shell_bin_size;
    uint32_t destination_lba = 2048; 
    uint64_t bytes_transferred = 0;

    // Direct sanity verification check
    if (total_payload_bytes == 0) {
        serial_puts("[Installer] Critical Error: Embedded shell binary payload reporting 0 bytes.\n");
        return;
    }

    while (bytes_transferred < total_payload_bytes) {
        memset(sector_scratchpad, 0, 512);
        uint64_t chunk_size = total_payload_bytes - bytes_transferred;
        if (chunk_size > 512) {
            chunk_size = 512;
        }

        memcpy(sector_scratchpad, _binary_toriginal_shell_bin_start + bytes_transferred, chunk_size);
        ata_write_sector(destination_lba, sector_scratchpad);
        
        destination_lba++;
        bytes_transferred += chunk_size;
    }

    serial_puts("[Installer] Disk blocks written successfully.\n");

    // 3. VFS Registration: Map directories into the kernel file ecosystem
    serial_puts("[Installer] Synchronizing file structures into VFS memory mounts...\n");
    
    fs_mkdir("/toriginal_os", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    fs_mkdir("/toriginal_os/boot", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);

    // Create confirmation verification token flag checked by shell.c
    fd_t fd = fs_open("/toriginal_os/installed.flag", O_CREAT | O_WRONLY | O_TRUNC, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd >= 0) {
        const char *flag_data = "installed\n";
        fs_write(fd, (void*)flag_data, 10);
        fs_close(fd);
        serial_puts("[Installer] /toriginal_os/installed.flag registered.\n");
        serial_puts("[Installer] Installation routine completed successfully.\n");
    } else {
        serial_puts("[Installer] Critical Error: Failed to register VFS verification flag.\n");
    }
}