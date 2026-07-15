#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

#define ACPI_MAX_IOAPICS   8
#define ACPI_MAX_OVERRIDES 24

typedef struct {
    uint8_t  ioapic_id;
    uint32_t address;
    uint32_t gsi_base;
} acpi_ioapic_t;

typedef struct {
    uint8_t  bus_source;
    uint8_t  irq_source;
    uint32_t gsi;
    uint16_t flags;
} acpi_irq_override_t;

int acpi_available(void);

int acpi_has_legacy_pic(void);

uint64_t acpi_local_apic_addr(void);

int              acpi_ioapic_count(void);
const acpi_ioapic_t *acpi_ioapic(int index);

int                      acpi_override_count(void);
const acpi_irq_override_t *acpi_override(int index);

uint32_t acpi_irq_to_gsi(uint8_t irq);

void acpi_init(uint32_t rsdp_old_phys, uint32_t rsdp_new_phys);

#endif

