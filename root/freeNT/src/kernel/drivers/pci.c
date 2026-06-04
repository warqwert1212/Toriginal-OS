// ==============================================================================
// PCI.C - Peripheral Component Interconnect Bus Scanner
// ==============================================================================
#include <stdint.h>

// Forward declare your printing function so we can output results
extern void print_vga(const char* str);
extern void print_serial(const char* str);

// Basic I/O port wrappers (you can move these to a common header later)
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void print_hex_32(uint32_t val) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    buffer[0] = '0'; buffer[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buffer[2 + i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    buffer[10] = '\0';
    print_vga(buffer);
    print_vga(" ");
}

// Read a 32-bit word from the PCI configuration space
uint32_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    
    // Create configuration address
    address = (uint32_t)((lbus << 16) | (lslot << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    
    // Write out the address and read in the data
    outl(0xCF8, address);
    return inl(0xCFC);
}

// Identify the manufacturer of the plugged-in device
void identify_vendor(uint16_t vendor_id) {
    switch(vendor_id) {
        case 0x8086: print_vga("[Intel Corp]\n"); break;
        case 0x10DE: print_vga("[NVIDIA Corp]\n"); break;
        case 0x1002: print_vga("[Advanced Micro Devices (AMD)]\n"); break;
        case 0x1234: print_vga("[QEMU Virtual VGA]\n"); break; // Standard QEMU default
        case 0x80EE: print_vga("[VirtualBox Graphics Adapter]\n"); break;
        case 0x15AD: print_vga("[VMware SVGA II Adapter]\n"); break;
        default:     print_vga("[Unknown/Other Hardware]\n"); break;
    }
}

// Brute force scan the first PCI bus to see what is plugged in
void pci_scan_bus() {
    print_vga("\n--- Scanning Motherboard PCI Bus ---\n");
    
    // We only scan Bus 0 for now (standard for basic hardware)
    for (uint16_t slot = 0; slot < 32; slot++) {
        // Offset 0 contains the Vendor ID and Device ID
        uint32_t device_vendor = pci_read_word(0, slot, 0, 0);
        
        // If the Vendor ID is 0xFFFF, there is no device in this slot
        uint16_t vendor = (uint16_t)(device_vendor & 0xFFFF);
        if (vendor != 0xFFFF) {
            uint16_t device = (uint16_t)(device_vendor >> 16);
            
            print_vga("Found Device in Slot ");
            print_hex_32(slot);
            print_vga("- Vendor: ");
            identify_vendor(vendor);
        }
    }
    print_vga("--- PCI Scan Complete ---\n\n");
}