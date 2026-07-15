
#include "apic.h"
#include "acpi.h"
#include "mm.h"
#include "serial.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(msr));
}
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

#define MSR_APIC_BASE   0x1B
#define APIC_BASE_ENABLE (1u << 11)

#define LAPIC_REG_ID            0x020
#define LAPIC_REG_EOI           0x0B0
#define LAPIC_REG_SPURIOUS      0x0F0
#define LAPIC_SPURIOUS_ENABLE   (1u << 8)

#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN  0x10

#define IOAPIC_REG_VER        0x01
#define IOAPIC_REDTBL_BASE    0x10

static volatile uint32_t *g_lapic_vbase  = 0;
static volatile uint8_t  *g_ioapic_vbase = 0;

static int g_apic_available = 0;

static void mmio_map_identity(uint64_t phys_addr) {

    uint64_t page = phys_addr & ~0xFFFULL;
    mm_map_page((vaddr_t)page, (paddr_t)page,
                PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    g_lapic_vbase[reg / 4] = val;
}
static inline uint32_t lapic_read(uint32_t reg) {
    return g_lapic_vbase[reg / 4];
}

static inline void ioapic_write(uint8_t reg, uint32_t val) {
    *(volatile uint32_t *)(g_ioapic_vbase + IOAPIC_REGSEL) = reg;
    *(volatile uint32_t *)(g_ioapic_vbase + IOAPIC_IOWIN)  = val;
}
static inline uint32_t ioapic_read(uint8_t reg) {
    *(volatile uint32_t *)(g_ioapic_vbase + IOAPIC_REGSEL) = reg;
    return *(volatile uint32_t *)(g_ioapic_vbase + IOAPIC_IOWIN);
}

static void legacy_pic_mask_all(void) {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

int apic_available(void) { return g_apic_available; }

void apic_init(void) {
    g_apic_available = 0;

    if (!acpi_available()) {
        serial_puts("[APIC] ACPI/MADT not available - staying on legacy PIC.\n");
        return;
    }
    if (acpi_ioapic_count() < 1) {
        serial_puts("[APIC] MADT has no I/O APIC entry - staying on legacy PIC.\n");
        return;
    }

    uint64_t lapic_phys = acpi_local_apic_addr();
    if (!lapic_phys) {

        lapic_phys = 0xFEE00000ULL;
    }

    const acpi_ioapic_t *io0 = acpi_ioapic(0);
    uint64_t ioapic_phys = io0->address;

    mmio_map_identity(lapic_phys);
    mmio_map_identity(ioapic_phys);

    g_lapic_vbase  = (volatile uint32_t *)(uintptr_t)lapic_phys;
    g_ioapic_vbase = (volatile uint8_t  *)(uintptr_t)ioapic_phys;

    uint64_t base_msr = rdmsr(MSR_APIC_BASE);
    base_msr |= APIC_BASE_ENABLE;
    wrmsr(MSR_APIC_BASE, base_msr);

    uint32_t spurious = lapic_read(LAPIC_REG_SPURIOUS);
    spurious |= LAPIC_SPURIOUS_ENABLE;
    spurious  = (spurious & ~0xFFu) | 0xFF;
    lapic_write(LAPIC_REG_SPURIOUS, spurious);

    legacy_pic_mask_all();

    g_apic_available = 1;
    serial_puts("[APIC] Local APIC enabled, I/O APIC mapped, legacy PIC masked off.\n");
}

void apic_route_irq(uint8_t irq, uint8_t vector) {
    if (!g_apic_available) return;

    uint32_t gsi = acpi_irq_to_gsi(irq);

    const acpi_ioapic_t *io0 = acpi_ioapic(0);
    if (gsi < io0->gsi_base) return;
    uint32_t pin = gsi - io0->gsi_base;

    uint16_t ovr_flags = 0;
    for (int i = 0; i < acpi_override_count(); i++) {
        const acpi_irq_override_t *o = acpi_override(i);
        if (o->irq_source == irq) { ovr_flags = o->flags; break; }
    }
    int active_low     = ((ovr_flags & 0x3) == 0x3);
    int level_triggered = ((ovr_flags & 0xC) == 0xC);

    uint32_t low = (uint32_t)vector;
    if (active_low)      low |= (1u << 13);
    if (level_triggered) low |= (1u << 15);

    uint8_t redtbl_lo = (uint8_t)(IOAPIC_REDTBL_BASE + pin * 2);
    uint8_t redtbl_hi = (uint8_t)(redtbl_lo + 1);

    ioapic_write(redtbl_hi, 0);
    ioapic_write(redtbl_lo, low);
}

void apic_send_eoi(void) {
    if (!g_apic_available) return;
    lapic_write(LAPIC_REG_EOI, 0);
}

