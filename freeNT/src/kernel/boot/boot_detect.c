/* Boot Medium Detection for Real Hardware
   Detects boot device type (USB, CD/DVD, HDD, etc.) */

#include <stdint.h>
#include <string.h>
#include "boot_detect.h"
#include "io.h"

#define MULTIBOOT2_TAG_TYPE_BOOT_DEVICE 5
#define MULTIBOOT2_TAG_TYPE_EFI64 12

typedef struct {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint8_t boot_device;
    uint8_t slice;
    uint8_t part;
    uint8_t reserved;
} multiboot_tag_boot_device_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t pointer;
} multiboot_tag_efi64_t;

static const char *boot_device_names[] = {
    "Floppy",
    "Hard Drive (INT13h)",
    "CD-ROM",
    "Hard Drive (INT13h Extensions)",
    "Network",
    "USB",
    "BIOS Boot Partition",
    "Unknown"
};

static int is_uefi_boot = 0;
static const char *current_boot_device = "Unknown";

void detect_boot_medium(unsigned int multiboot_magic, void *multiboot_info) {
    multiboot_tag_t *tags = (multiboot_tag_t *)multiboot_info;

    if (multiboot_magic != 0x36d76289) {
        current_boot_device = "No valid boot signature (not Multiboot2)";
        serial_puts("[boot_detect] Not Multiboot2 - no tags available\n");
        return;
    }

    if (!tags) {
        serial_puts("[boot_detect] Multiboot info pointer is NULL\n");
        return;
    }

    multiboot_tag_t *tag = tags;
    while (tag->type != 0) {
        /* Print some info about each tag encountered for diagnostics */
        switch (tag->type) {
            case MULTIBOOT2_TAG_TYPE_BOOT_DEVICE:
                serial_puts("[boot_detect] Found BOOT_DEVICE tag\n");
                break;
            case MULTIBOOT2_TAG_TYPE_EFI64:
                serial_puts("[boot_detect] Found EFI64 tag\n");
                break;
            default:
                serial_puts("[boot_detect] Found tag type: ");
                /* No integer printing; print placeholder */
                serial_puts("(other)\n");
                break;
        }
        if (tag->type == MULTIBOOT2_TAG_TYPE_BOOT_DEVICE) {
            multiboot_tag_boot_device_t *bd = (multiboot_tag_boot_device_t *)tag;
            
            if (bd->boot_device >= 0 && bd->boot_device <= 6) {
                current_boot_device = boot_device_names[bd->boot_device];
                serial_puts("[boot_detect] Boot device index detected\n");
            } else {
                current_boot_device = boot_device_names[7];
            }
            break;
        }
        
        if (tag->type == MULTIBOOT2_TAG_TYPE_EFI64) {
            is_uefi_boot = 1;
        }
        
        tag = (multiboot_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }
}

const char *get_boot_device(void) {
    return current_boot_device ? current_boot_device : "Unknown";
}

int get_boot_is_uefi(void) {
    return is_uefi_boot;
}

const char *get_boot_mode(void) {
    return is_uefi_boot ? "UEFI" : "Legacy BIOS";
}
