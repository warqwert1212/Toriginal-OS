#include "ahci.h"
#include "vmm.h"
#include "heap.h"

void print_serial(const char* str);

void ahci_init(uint64_t hba_base_phys) {
    print_serial("[SATA] Mapping AHCI HBA Controller Interface Bars...\n");
    
    // Map controller registers into Kernel high-virtual structure definitions
    uint64_t hba_virt = 0xFFFF800050000000;
    vmm_map_page(hba_virt, hba_base_phys, PAGE_WRITABLE);

    struct ahci_hba_mem* hba = (struct ahci_hba_mem*)hba_virt;
    uint32_t pi = hba->pi; // Ports Implemented Bitmask

    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t ssts = hba->ports[i].ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;

            if (det == 3 && ipm == 1) { // Device present and active
                if (hba->ports[i].sig == SATA_SIG_ATA) {
                    print_serial("[SATA] Hard Disk Drive (SATA) Identified on Port Unit\n");
                    // System link hooks for active reading go here
                }
            }
        }
    }
}