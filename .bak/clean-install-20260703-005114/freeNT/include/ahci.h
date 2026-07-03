// ==============================================================================
// AHCI.H - Advanced Host Controller Interface (SATA Engine)
// ==============================================================================
#pragma once
#include <stdint.h>

#define SATA_SIG_ATAPI 0xEB140101
#define SATA_SIG_ATA   0x00000101

struct ahci_port {
    uint32_t clb;    // Command list base address, 1K byte aligned
    uint32_t clbu;   // Command list base address upper 32 bits
    uint32_t fb;     // FIS base address, 256-byte aligned
    uint32_t fbu;    // FIS base address upper 32 bits
    uint32_t is;     // Interrupt status
    uint32_t ie;     // Interrupt enable
    uint32_t cmd;    // Command and status
    uint32_t rsv0;   // Reserved
    uint32_t tfd;    // Task file data
    uint32_t sig;    // Signature
    uint32_t ssts;   // SATA status (SCR0:SStatus)
    uint32_t sctl;   // SATA control (SCR1:SControl)
    uint32_t serr;   // SATA error (SCR2:SError)
    uint32_t sact;   // SATA active (SCR3:SActive)
    uint32_t ci;     // Command issue
} __attribute__((packed));

struct ahci_hba_mem {
    uint32_t cap;       // Host capabilities
    uint32_t ghc;       // Global host control
    uint32_t is;        // Interrupt status register
    uint32_t pi;        // Ports implemented
    uint32_t vs;        // Version
    uint32_t ccc_ctl;   // Command completion coalescing control
    uint32_t ccc_pts;   // Command completion coalescing ports
    uint32_t em_loc;    // Enclosure management location
    uint32_t em_ctl;    // Enclosure management control
    uint32_t cap2;      // Host capabilities extended
    uint32_t bohc;      // BIOS/OS handoff control and status
    uint8_t  rsv[116];  // Reserved
    uint8_t  vendor[96];// Vendor specific registers
    struct ahci_port ports[32]; // Ports allocation arrays
} __attribute__((packed));

void ahci_init(uint64_t hba_base_phys);
