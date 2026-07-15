
#include "uhci.h"
#include "pci.h"
#include "mm.h"
#include "serial.h"
#include "string.h"
#include "pit.h"
#include "usb.h"

static inline uint8_t inb(uint16_t p)  { uint8_t v;  __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outb(uint16_t p, uint8_t v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }

#define UHCI_PCI_CLASS     0x0C
#define UHCI_PCI_SUBCLASS  0x03
#define UHCI_PCI_PROGIF    0x00

#define REG_USBCMD      0x00
#define REG_USBSTS      0x02
#define REG_USBINTR     0x04
#define REG_FRNUM       0x06
#define REG_FRBASEADD   0x08
#define REG_SOFMOD      0x0C
#define REG_PORTSC1     0x10
#define REG_PORTSC2     0x12

#define CMD_RUN         0x0001
#define CMD_HCRESET     0x0002
#define CMD_GRESET      0x0004
#define CMD_CONFIGURE   0x0040
#define CMD_MAXP_64     0x0080

#define STS_INT         0x0001
#define STS_ERROR_INT   0x0002
#define STS_HALTED      0x0020

#define PORTSC_CONNECTED     0x0001
#define PORTSC_CONN_CHANGE   0x0002
#define PORTSC_ENABLED       0x0004
#define PORTSC_ENABLE_CHANGE 0x0008
#define PORTSC_LOW_SPEED     0x0100
#define PORTSC_RESET         0x0200
#define PORTSC_WRITE_CLEAR_MASK (PORTSC_CONN_CHANGE | PORTSC_ENABLE_CHANGE)

#define PCI_LEGSUP_OFFSET 0xC0
#define PCI_LEGSUP_DISABLE_VALUE 0x8F00

#define LP_TERMINATE   0x1u
#define LP_QH          0x2u
#define LP_DEPTH_FIRST 0x4u

typedef struct {
    volatile uint32_t link;
    volatile uint32_t cs;
    volatile uint32_t token;
    volatile uint32_t buffer;

    uint32_t sw_used;
    uint32_t sw_reserved[3];
} __attribute__((packed, aligned(16))) uhci_td_t;

#define TD_CS_ACTLEN_MASK   0x7FFu
#define TD_CS_STATUS_SHIFT  16
#define TD_CS_STATUS_MASK   (0xFFu << TD_CS_STATUS_SHIFT)
#define TD_CS_ACTIVE        (1u << 23)
#define TD_CS_IOC           (1u << 24)
#define TD_CS_ISOCHRONOUS   (1u << 25)
#define TD_CS_LOW_SPEED     (1u << 26)
#define TD_CS_CERR_SHIFT    27
#define TD_CS_CERR_MASK     (0x3u << TD_CS_CERR_SHIFT)
#define TD_CS_SPD           (1u << 29)

#define TD_STATUS_BITSTUFF   (1u << (16+1))
#define TD_STATUS_CRC_TIMEOUT (1u << (16+0))
#define TD_STATUS_NAK        (1u << (16+3))
#define TD_STATUS_BABBLE     (1u << (16+4))
#define TD_STATUS_STALLED    (1u << (16+6))
#define TD_STATUS_ANY_ERROR  (TD_STATUS_BITSTUFF | TD_STATUS_CRC_TIMEOUT | \
                               TD_STATUS_NAK | TD_STATUS_BABBLE | TD_STATUS_STALLED)

#define TD_TOKEN_PID_SHIFT     0
#define TD_TOKEN_DEVADDR_SHIFT 8
#define TD_TOKEN_ENDPT_SHIFT   15
#define TD_TOKEN_TOGGLE_SHIFT  19
#define TD_TOKEN_MAXLEN_SHIFT  21

#define PID_IN   0x69u
#define PID_OUT  0xE1u
#define PID_SETUP 0x2Du

typedef struct {
    volatile uint32_t head;
    volatile uint32_t element;
    uint32_t sw_reserved[2];
} __attribute__((packed, aligned(16))) uhci_qh_t;

#define FRAME_LIST_ENTRIES 1024
#define TD_POOL_SIZE   64
#define QH_POOL_SIZE   16

#define MAX_INTERRUPT_ENDPOINTS 8

typedef struct {
    int      in_use;
    uint8_t  device_addr;
    uint8_t  endpoint;
    uint8_t  max_packet;
    int      low_speed;
    uint8_t  toggle;
    uhci_qh_t *qh;
    uhci_td_t *td;
    uint8_t  shadow_buf[64];
    int      new_data;
    int      error;
} interrupt_endpoint_t;

static int      g_uhci_available = 0;
static uint16_t g_io_base = 0;
static int      g_num_ports = 0;

static uint32_t *g_frame_list = 0;
static uhci_td_t *g_td_pool = 0;
static uhci_qh_t *g_qh_pool = 0;
static int g_td_pool_used = 0;
static int g_qh_pool_used = 0;

static interrupt_endpoint_t g_int_eps[MAX_INTERRUPT_ENDPOINTS];

static uhci_qh_t *g_root_qh = 0;

static uhci_td_t *alloc_td(void) {
    if (g_td_pool_used >= TD_POOL_SIZE) return 0;
    uhci_td_t *td = &g_td_pool[g_td_pool_used++];
    memset((void*)td, 0, sizeof(*td));
    return td;
}

static uhci_qh_t *alloc_qh(void) {
    if (g_qh_pool_used >= QH_POOL_SIZE) return 0;
    uhci_qh_t *qh = &g_qh_pool[g_qh_pool_used++];
    memset((void*)qh, 0, sizeof(*qh));
    return qh;
}

static void disable_bios_legacy(uint8_t bus, uint8_t dev, uint8_t func) {

    uint32_t existing = pci_config_read32(bus, dev, func, PCI_LEGSUP_OFFSET & 0xFC);
    (void)existing;
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                ((uint32_t)func << 8) | (PCI_LEGSUP_OFFSET & 0xFC));
    outl(0xCFC, PCI_LEGSUP_DISABLE_VALUE);
}

static int reset_controller(void) {

    outw(g_io_base + REG_USBCMD, CMD_GRESET);
    pit_sleep(50);
    outw(g_io_base + REG_USBCMD, 0);
    pit_sleep(10);

    outw(g_io_base + REG_USBCMD, CMD_HCRESET);
    for (int i = 0; i < 1000; i++) {
        if (!(inw(g_io_base + REG_USBCMD) & CMD_HCRESET)) break;
        pit_sleep(1);
    }
    if (inw(g_io_base + REG_USBCMD) & CMD_HCRESET) {
        serial_puts("[UHCI] HCRESET did not clear - controller unresponsive.\n");
        return -1;
    }

    outw(g_io_base + REG_USBINTR, 0);
    outw(g_io_base + REG_FRNUM, 0);

    return 0;
}

static void reset_port(int port_index) {
    uint16_t reg = g_io_base + (port_index == 0 ? REG_PORTSC1 : REG_PORTSC2);

    uint16_t v = inw(reg);
    outw(reg, (uint16_t)(v | PORTSC_RESET));
    pit_sleep(50);

    v = inw(reg);
    outw(reg, (uint16_t)(v & ~PORTSC_RESET));
    pit_sleep(10);

    v = inw(reg);
    outw(reg, (uint16_t)((v & ~PORTSC_WRITE_CLEAR_MASK) | PORTSC_ENABLED |
                          PORTSC_CONN_CHANGE | PORTSC_ENABLE_CHANGE));

    for (int i = 0; i < 100; i++) {
        if (inw(reg) & PORTSC_ENABLED) break;
        pit_sleep(1);
    }
}

static int port_bring_up_device(int port_index, int *out_low_speed) {
    uint16_t reg = g_io_base + (port_index == 0 ? REG_PORTSC1 : REG_PORTSC2);
    uint16_t v = inw(reg);

    if (!(v & PORTSC_CONNECTED)) return 0;

    reset_port(port_index);

    v = inw(reg);
    if (!(v & PORTSC_ENABLED)) {
        serial_puts("[UHCI] Port did not enable after reset.\n");
        return 0;
    }

    *out_low_speed = (v & PORTSC_LOW_SPEED) ? 1 : 0;
    return 1;
}

int uhci_control_transfer(uint8_t device_addr, uint8_t max_packet,
                           int low_speed,
                           const uint8_t setup[8],
                           void *data, uint16_t data_len) {
    if (!g_uhci_available) return -1;
    if (!setup) return -1;

    int is_in = (setup[0] & 0x80) != 0;

    g_td_pool_used = 0;
    for (int i = 0; i < MAX_INTERRUPT_ENDPOINTS; i++) {
        if (g_int_eps[i].in_use) {

        }
    }
    g_qh_pool_used = 0;

    uhci_qh_t *qh = alloc_qh();
    if (!qh) return -1;

    uhci_td_t *setup_td = alloc_td();
    if (!setup_td) return -1;

    setup_td->token = (uint32_t)PID_SETUP
                     | ((uint32_t)device_addr << TD_TOKEN_DEVADDR_SHIFT)
                     | ((uint32_t)0 << TD_TOKEN_ENDPT_SHIFT)
                     | ((uint32_t)0 << TD_TOKEN_TOGGLE_SHIFT)
                     | (((uint32_t)8 - 1) << TD_TOKEN_MAXLEN_SHIFT);
    setup_td->buffer = (uint32_t)(uintptr_t)setup;
    setup_td->cs = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT)
                 | (low_speed ? TD_CS_LOW_SPEED : 0);

    uhci_td_t *prev_td = setup_td;
    uint8_t toggle = 1;

    uint8_t *data_ptr = (uint8_t *)data;
    uint16_t remaining = data_len;

    while (remaining > 0) {
        uhci_td_t *td = alloc_td();
        if (!td) return -1;

        uint16_t chunk = remaining < max_packet ? remaining : max_packet;

        td->token = (uint32_t)(is_in ? PID_IN : PID_OUT)
                  | ((uint32_t)device_addr << TD_TOKEN_DEVADDR_SHIFT)
                  | ((uint32_t)0 << TD_TOKEN_ENDPT_SHIFT)
                  | ((uint32_t)toggle << TD_TOKEN_TOGGLE_SHIFT)
                  | (((uint32_t)chunk - 1) << TD_TOKEN_MAXLEN_SHIFT);
        td->buffer = (uint32_t)(uintptr_t)data_ptr;
        td->cs = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT)
               | (low_speed ? TD_CS_LOW_SPEED : 0);

        prev_td->link = (uint32_t)(uintptr_t)td | LP_DEPTH_FIRST;
        prev_td = td;

        toggle ^= 1;
        data_ptr += chunk;
        remaining -= chunk;
    }

    uhci_td_t *status_td = alloc_td();
    if (!status_td) return -1;

    int status_is_in = data_len ? !is_in : 1;
    status_td->token = (uint32_t)(status_is_in ? PID_IN : PID_OUT)
                      | ((uint32_t)device_addr << TD_TOKEN_DEVADDR_SHIFT)
                      | ((uint32_t)0 << TD_TOKEN_ENDPT_SHIFT)
                      | ((uint32_t)1 << TD_TOKEN_TOGGLE_SHIFT)
                      | (0x7FFu << TD_TOKEN_MAXLEN_SHIFT);
    status_td->buffer = 0;
    status_td->cs = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT) | TD_CS_IOC
                   | (low_speed ? TD_CS_LOW_SPEED : 0);
    status_td->link = LP_TERMINATE;

    prev_td->link = (uint32_t)(uintptr_t)status_td | LP_DEPTH_FIRST;

    qh->element = (uint32_t)(uintptr_t)setup_td;
    qh->head = LP_TERMINATE;

    uint32_t saved_root_element = g_root_qh->element;
    g_root_qh->element = (uint32_t)(uintptr_t)qh | LP_QH;

    int result = -1;
    for (int i = 0; i < 5000; i++) {
        uint32_t cs = status_td->cs;
        if (!(cs & TD_CS_ACTIVE)) {
            if (cs & (TD_CS_STATUS_MASK)) {

                result = -1;
            } else {
                result = 0;
            }
            break;
        }

        if (!(setup_td->cs & TD_CS_ACTIVE) && (setup_td->cs & TD_STATUS_ANY_ERROR)) {
            result = -1;
            break;
        }
        pit_sleep(1);
    }

    g_root_qh->element = saved_root_element;

    return result;
}

int uhci_setup_interrupt_endpoint(uint8_t device_addr, uint8_t endpoint,
                                   uint8_t max_packet, int low_speed,
                                   uint8_t interval_ms) {
    (void)interval_ms;

    if (!g_uhci_available) return -1;
    if (max_packet > sizeof(((interrupt_endpoint_t*)0)->shadow_buf)) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_INTERRUPT_ENDPOINTS; i++) {
        if (!g_int_eps[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return -1;

    if (g_qh_pool_used >= QH_POOL_SIZE || (QH_POOL_SIZE - 1 - slot) < 0) return -1;
    uhci_qh_t *qh = &g_qh_pool[QH_POOL_SIZE - 1 - slot];
    uhci_td_t *td = &g_td_pool[TD_POOL_SIZE - 1 - slot];
    memset((void*)qh, 0, sizeof(*qh));
    memset((void*)td, 0, sizeof(*td));

    interrupt_endpoint_t *ep = &g_int_eps[slot];
    ep->device_addr = device_addr;
    ep->endpoint    = endpoint;
    ep->max_packet  = max_packet;
    ep->low_speed   = low_speed;
    ep->toggle      = 0;
    ep->qh = qh;
    ep->td = td;
    ep->new_data = 0;
    ep->error = 0;
    ep->in_use = 1;

    td->token = (uint32_t)PID_IN
              | ((uint32_t)device_addr << TD_TOKEN_DEVADDR_SHIFT)
              | ((uint32_t)endpoint << TD_TOKEN_ENDPT_SHIFT)
              | ((uint32_t)ep->toggle << TD_TOKEN_TOGGLE_SHIFT)
              | (((uint32_t)max_packet - 1) << TD_TOKEN_MAXLEN_SHIFT);
    td->buffer = (uint32_t)(uintptr_t)ep->shadow_buf;
    td->cs = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT) | TD_CS_SPD
           | (low_speed ? TD_CS_LOW_SPEED : 0);
    td->link = LP_TERMINATE;

    qh->element = (uint32_t)(uintptr_t)td;

    qh->head = g_root_qh->head;
    g_root_qh->head = (uint32_t)(uintptr_t)qh | LP_QH;

    return slot;
}

void uhci_service_interrupt_endpoints(void) {
    for (int i = 0; i < MAX_INTERRUPT_ENDPOINTS; i++) {
        interrupt_endpoint_t *ep = &g_int_eps[i];
        if (!ep->in_use) continue;

        uint32_t cs = ep->td->cs;
        if (cs & TD_CS_ACTIVE) continue;

        if (cs & TD_STATUS_ANY_ERROR) {
            if (cs & TD_STATUS_STALLED) {
                ep->error = 1;
            }

        } else {
            uint32_t actlen = (cs & TD_CS_ACTLEN_MASK);
            if (actlen != TD_CS_ACTLEN_MASK && actlen > 0 && actlen <= ep->max_packet) {
                ep->new_data = (int)actlen;
            }
            ep->toggle ^= 1;
        }

        ep->td->token = (ep->td->token & ~((uint32_t)1 << TD_TOKEN_TOGGLE_SHIFT))
                       | ((uint32_t)ep->toggle << TD_TOKEN_TOGGLE_SHIFT);
        ep->td->cs = TD_CS_ACTIVE | (3u << TD_CS_CERR_SHIFT) | TD_CS_SPD
                   | (ep->low_speed ? TD_CS_LOW_SPEED : 0);
    }
}

int uhci_poll_interrupt_endpoint(int handle, uint8_t *buf, uint8_t buf_len) {
    if (handle < 0 || handle >= MAX_INTERRUPT_ENDPOINTS) return -1;
    interrupt_endpoint_t *ep = &g_int_eps[handle];
    if (!ep->in_use) return -1;
    if (ep->error) return -1;
    if (!ep->new_data) return 0;

    int n = ep->new_data;
    if (n > buf_len) n = buf_len;
    memcpy(buf, ep->shadow_buf, (size_t)n);
    ep->new_data = 0;
    return n;
}

int uhci_available(void) { return g_uhci_available; }

void uhci_init(void) {
    g_uhci_available = 0;

    pci_device_t devices[64];
    int count = pci_scan(devices, 64);

    pci_device_t *uhci_dev = 0;
    for (int i = 0; i < count; i++) {
        if (devices[i].class_code == UHCI_PCI_CLASS &&
            devices[i].subclass   == UHCI_PCI_SUBCLASS &&
            devices[i].prog_if    == UHCI_PCI_PROGIF) {
            uhci_dev = &devices[i];
            break;
        }
    }

    if (!uhci_dev) {
        serial_puts("[UHCI] No UHCI controller found on PCI bus.\n");
        return;
    }

    serial_puts("[UHCI] UHCI controller found on PCI bus.\n");

    uint16_t io_base = 0;
    for (int b = 0; b < 6; b++) {
        uint32_t bar = uhci_dev->bar[b];
        if (bar & 0x1) {
            io_base = (uint16_t)(bar & ~0x3u);
            break;
        }
    }
    if (!io_base) {
        serial_puts("[UHCI] Controller has no I/O-space BAR - cannot proceed.\n");
        return;
    }
    g_io_base = io_base;

    pci_enable_device(uhci_dev);
    disable_bios_legacy(uhci_dev->bus, uhci_dev->device, uhci_dev->function);

    if (reset_controller() != 0) return;

    paddr_t frame_list_phys = mm_alloc_pages(1);
    paddr_t td_pool_phys    = mm_alloc_pages((sizeof(uhci_td_t) * TD_POOL_SIZE + 4095) / 4096 + 1);
    paddr_t qh_pool_phys    = mm_alloc_pages((sizeof(uhci_qh_t) * QH_POOL_SIZE + 4095) / 4096 + 1);

    if (!frame_list_phys || !td_pool_phys || !qh_pool_phys) {
        serial_puts("[UHCI] Failed to allocate frame list / TD / QH pools.\n");
        return;
    }

    g_frame_list = (uint32_t *)(uintptr_t)frame_list_phys;
    g_td_pool    = (uhci_td_t *)(uintptr_t)td_pool_phys;
    g_qh_pool    = (uhci_qh_t *)(uintptr_t)qh_pool_phys;

    memset(g_td_pool, 0, sizeof(uhci_td_t) * TD_POOL_SIZE);
    memset(g_qh_pool, 0, sizeof(uhci_qh_t) * QH_POOL_SIZE);
    memset(g_int_eps, 0, sizeof(g_int_eps));

    g_root_qh = alloc_qh();
    g_root_qh->head    = LP_TERMINATE;
    g_root_qh->element = LP_TERMINATE;

    for (int i = 0; i < FRAME_LIST_ENTRIES; i++) {
        g_frame_list[i] = (uint32_t)(uintptr_t)g_root_qh | LP_QH;
    }

    outl(g_io_base + REG_FRBASEADD, (uint32_t)(uintptr_t)g_frame_list);
    outb(g_io_base + REG_SOFMOD, 64);

    outw(g_io_base + REG_USBCMD, CMD_RUN | CMD_CONFIGURE | CMD_MAXP_64);

    for (int i = 0; i < 100; i++) {
        if (!(inw(g_io_base + REG_USBSTS) & STS_HALTED)) break;
        pit_sleep(1);
    }
    if (inw(g_io_base + REG_USBSTS) & STS_HALTED) {
        serial_puts("[UHCI] Controller did not leave halted state after RUN.\n");
        return;
    }

    g_num_ports = 2;

    serial_puts("[UHCI] Controller running. Probing ports...\n");

    g_uhci_available = 1;

    for (int p = 0; p < g_num_ports; p++) {
        int low_speed = 0;
        if (port_bring_up_device(p, &low_speed)) {
            serial_puts("[UHCI] Device detected and port enabled.\n");
            usb_enumerate_port(low_speed);
        }
    }
}

