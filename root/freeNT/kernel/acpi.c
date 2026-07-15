
#include "acpi.h"
#include "serial.h"
#include "string.h"

typedef struct {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;

    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_addr;
    uint32_t flags;

} __attribute__((packed)) acpi_madt_t;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_hdr_t;

#define MADT_TYPE_LOCAL_APIC        0
#define MADT_TYPE_IOAPIC            1
#define MADT_TYPE_IRQ_OVERRIDE      2
#define MADT_TYPE_LOCAL_APIC_NMI    4
#define MADT_TYPE_LOCAL_APIC_OVERR  5

typedef struct {
    acpi_madt_entry_hdr_t hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed)) madt_ioapic_entry_t;

typedef struct {
    acpi_madt_entry_hdr_t hdr;
    uint8_t  bus_source;
    uint8_t  irq_source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed)) madt_irq_override_entry_t;

typedef struct {
    acpi_madt_entry_hdr_t hdr;
    uint16_t reserved;
    uint64_t local_apic_addr;
} __attribute__((packed)) madt_local_apic_override_entry_t;

static int      g_acpi_available   = 0;
static int      g_has_legacy_pic   = 1;
static uint64_t g_local_apic_addr  = 0;

static acpi_ioapic_t       g_ioapics[ACPI_MAX_IOAPICS];
static int                 g_ioapic_count = 0;

static acpi_irq_override_t g_overrides[ACPI_MAX_OVERRIDES];
static int                 g_override_count = 0;

static uint8_t sum_bytes(const void *p, uint32_t len) {
    const uint8_t *b = (const uint8_t *)p;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + b[i]);
    return sum;
}

static int rsdp_valid_v1(const acpi_rsdp_t *r) {
    if (strncmp(r->signature, "RSD PTR ", 8) != 0) return 0;

    return sum_bytes(r, 20) == 0;
}

static int rsdp_valid_v2(const acpi_rsdp_t *r) {
    if (!rsdp_valid_v1(r)) return 0;
    if (r->revision < 2) return 1;
    return sum_bytes(r, r->length) == 0;
}

static int sdt_header_valid(const acpi_sdt_header_t *h, const char *want_sig) {
    if (strncmp(h->signature, want_sig, 4) != 0) return 0;
    return sum_bytes(h, h->length) == 0;
}

static void parse_madt(const acpi_madt_t *madt) {
    g_local_apic_addr = madt->local_apic_addr;
    g_has_legacy_pic  = (madt->flags & 0x1) ? 1 : 0;

    const uint8_t *ptr = (const uint8_t *)madt + sizeof(acpi_madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (ptr + sizeof(acpi_madt_entry_hdr_t) <= end) {
        const acpi_madt_entry_hdr_t *eh = (const acpi_madt_entry_hdr_t *)ptr;
        if (eh->length < sizeof(acpi_madt_entry_hdr_t) || ptr + eh->length > end) break;

        switch (eh->type) {
            case MADT_TYPE_IOAPIC: {
                if (eh->length >= sizeof(madt_ioapic_entry_t) &&
                    g_ioapic_count < ACPI_MAX_IOAPICS) {
                    const madt_ioapic_entry_t *e = (const madt_ioapic_entry_t *)ptr;
                    g_ioapics[g_ioapic_count].ioapic_id = e->ioapic_id;
                    g_ioapics[g_ioapic_count].address   = e->address;
                    g_ioapics[g_ioapic_count].gsi_base  = e->gsi_base;
                    g_ioapic_count++;

                    serial_puts("[ACPI] I/O APIC found, id/addr/gsi_base logged\n");
                }
                break;
            }
            case MADT_TYPE_IRQ_OVERRIDE: {
                if (eh->length >= sizeof(madt_irq_override_entry_t) &&
                    g_override_count < ACPI_MAX_OVERRIDES) {
                    const madt_irq_override_entry_t *e = (const madt_irq_override_entry_t *)ptr;
                    g_overrides[g_override_count].bus_source = e->bus_source;
                    g_overrides[g_override_count].irq_source = e->irq_source;
                    g_overrides[g_override_count].gsi        = e->gsi;
                    g_overrides[g_override_count].flags      = e->flags;
                    g_override_count++;
                }
                break;
            }
            case MADT_TYPE_LOCAL_APIC_OVERR: {
                if (eh->length >= sizeof(madt_local_apic_override_entry_t)) {
                    const madt_local_apic_override_entry_t *e =
                        (const madt_local_apic_override_entry_t *)ptr;
                    g_local_apic_addr = e->local_apic_addr;
                }
                break;
            }
            default:
                break;
        }

        ptr += eh->length;
    }
}

static int find_and_parse_madt(const acpi_sdt_header_t *root, int use_xsdt) {
    uint32_t entry_bytes = use_xsdt ? 8u : 4u;
    uint32_t n = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / entry_bytes;
    const uint8_t *arr = (const uint8_t *)root + sizeof(acpi_sdt_header_t);

    for (uint32_t i = 0; i < n; i++) {
        uint64_t phys;
        if (use_xsdt) {
            uint64_t v; __builtin_memcpy(&v, arr + i * 8, 8); phys = v;
        } else {
            uint32_t v; __builtin_memcpy(&v, arr + i * 4, 4); phys = v;
        }
        if (!phys) continue;

        const acpi_sdt_header_t *h = (const acpi_sdt_header_t *)(uintptr_t)phys;
        if (sdt_header_valid(h, "APIC")) {
            parse_madt((const acpi_madt_t *)h);
            return 1;
        }
    }
    return 0;
}

void acpi_init(uint32_t rsdp_old_phys, uint32_t rsdp_new_phys) {
    g_acpi_available = 0;
    g_ioapic_count    = 0;
    g_override_count  = 0;
    g_local_apic_addr = 0;

    const acpi_rsdp_t *rsdp = 0;

    if (rsdp_new_phys) {
        const acpi_rsdp_t *cand = (const acpi_rsdp_t *)(uintptr_t)rsdp_new_phys;
        if (rsdp_valid_v2(cand)) rsdp = cand;
    }
    if (!rsdp && rsdp_old_phys) {
        const acpi_rsdp_t *cand = (const acpi_rsdp_t *)(uintptr_t)rsdp_old_phys;
        if (rsdp_valid_v1(cand)) rsdp = cand;
    }

    if (!rsdp) {
        serial_puts("[ACPI] No valid RSDP from multiboot - APIC unavailable, using legacy PIC.\n");
        return;
    }

    int found = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        const acpi_sdt_header_t *xsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
        if (sdt_header_valid(xsdt, "XSDT")) {
            found = find_and_parse_madt(xsdt, 1);
        }
    }
    if (!found && rsdp->rsdt_address) {
        const acpi_sdt_header_t *rsdt = (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_address;
        if (sdt_header_valid(rsdt, "RSDT")) {
            found = find_and_parse_madt(rsdt, 0);
        }
    }

    if (!found || g_ioapic_count == 0) {
        serial_puts("[ACPI] RSDP found but no valid MADT/I-O-APIC - using legacy PIC.\n");
        return;
    }

    g_acpi_available = 1;
    serial_puts("[ACPI] MADT parsed OK. Local APIC + I/O APIC available.\n");
}

int acpi_available(void)      { return g_acpi_available; }
int acpi_has_legacy_pic(void) { return g_has_legacy_pic; }
uint64_t acpi_local_apic_addr(void) { return g_local_apic_addr; }

int acpi_ioapic_count(void) { return g_ioapic_count; }
const acpi_ioapic_t *acpi_ioapic(int index) {
    if (index < 0 || index >= g_ioapic_count) return 0;
    return &g_ioapics[index];
}

int acpi_override_count(void) { return g_override_count; }
const acpi_irq_override_t *acpi_override(int index) {
    if (index < 0 || index >= g_override_count) return 0;
    return &g_overrides[index];
}

uint32_t acpi_irq_to_gsi(uint8_t irq) {
    for (int i = 0; i < g_override_count; i++) {
        if (g_overrides[i].irq_source == irq) return g_overrides[i].gsi;
    }
    return irq;
}

