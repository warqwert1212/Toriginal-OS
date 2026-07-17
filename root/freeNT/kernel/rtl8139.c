#include "rtl8139.h"
#include "pci.h"
#include "net.h"
#include "port.h"
#include "interrupts.h"
#include "mm.h"
#include "string.h"
#include "serial.h"

/* rtl8139.c look a google serch suggested this name ok. */

#define REG_MAC0     0x00
#define REG_TSD0     0x10
#define REG_TSAD0    0x20
#define REG_RBSTART  0x30
#define REG_CMD      0x37
#define REG_CAPR     0x38
#define REG_IMR      0x3C
#define REG_ISR      0x3E
#define REG_RCR      0x44
#define REG_CONFIG1  0x52

#define CMD_RST  0x10
#define CMD_RE   0x08
#define CMD_TE   0x04

#define ISR_ROK   0x0001
#define ISR_TER   0x0008
#define ISR_RXOVW 0x0010

#define RX_BUF_SIZE (8192 + 16 + 1500)
#define TX_BUF_SIZE ETH_FRAME_MAX
#define TX_RING_COUNT 4

typedef struct {
    uint16_t   io_base;
    uint8_t   *rx_buffer;
    uint32_t   rx_offset;
    uint8_t   *tx_buffer[TX_RING_COUNT];
    int        tx_next;
    net_device_t netdev;
} rtl8139_dev_t;

static rtl8139_dev_t g_rtl;

static int rtl_send(net_device_t *dev, const uint8_t *frame, uint16_t len)
{
    rtl8139_dev_t *r = (rtl8139_dev_t *)dev->driver_data;
    if (len > TX_BUF_SIZE) return -1;

    int slot = r->tx_next;
    r->tx_next = (r->tx_next + 1) % TX_RING_COUNT;
    memcpy(r->tx_buffer[slot], frame, len);

    outl((uint16_t)(r->io_base + REG_TSAD0 + slot * 4), (uint32_t)(uintptr_t)r->tx_buffer[slot]);
    outl((uint16_t)(r->io_base + REG_TSD0  + slot * 4), (uint32_t)len);

    for (int i = 0; i < 100000; i++) {
        uint32_t status = inl((uint16_t)(r->io_base + REG_TSD0 + slot * 4));
        if (status & (1 << 15)) return 0;
        if (status & (1 << 30)) return -1;
    }
    return -1;
}

static void rtl_reset_rx(rtl8139_dev_t *r);

static void rtl_receive_all(rtl8139_dev_t *r)
{
    while (!(inb((uint16_t)(r->io_base + REG_CMD)) & 0x01)) {
        uint8_t *hdr = r->rx_buffer + r->rx_offset;
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t plen    = (uint16_t)(hdr[2] | (hdr[3] << 8));

        /* FIX: plen must be a plausible Ethernet frame length (frame +
         * 4-byte CRC), not merely "fits somewhere in the ring allocation".
         * The old bound (RX_BUF_SIZE - 4, ~9700 bytes) let a corrupted or
         * malicious header claim a length far beyond any real frame,
         * causing net_receive() below to read well past the actual
         * rx_buffer allocation (rx_buffer is only RX_BUF_SIZE bytes; the
         * extra 1500+16 padding only covers ring wrap-around, not an
         * oversized single packet). */
        if (!(status & 0x01) || plen < 4 || plen > (ETH_FRAME_MAX + 4)) {
            serial_puts("[RTL8139] bad rx status — resetting receiver\n");
            rtl_reset_rx(r);
            return;
        }

        uint16_t frame_len = (uint16_t)(plen - 4);
        net_receive(hdr + 4, frame_len);

        r->rx_offset = (uint32_t)((r->rx_offset + plen + 4 + 3) & ~3u);
        if (r->rx_offset >= 8192) r->rx_offset -= 8192;
        outw((uint16_t)(r->io_base + REG_CAPR), (uint16_t)(r->rx_offset - 16));
    }
}

/* FIX: recover from an RX buffer overflow instead of just logging it.
 * The overflow condition leaves the NIC's internal read pointer out of
 * sync with our rx_offset; toggling RE resets the NIC side, and zeroing
 * rx_offset + CAPR resyncs our side to match. Without this the driver
 * would keep interpreting stale/misaligned ring data after the first
 * overflow, likely wedging receive permanently. */
static void rtl_reset_rx(rtl8139_dev_t *r)
{
    uint8_t cmd = inb((uint16_t)(r->io_base + REG_CMD));
    outb((uint16_t)(r->io_base + REG_CMD), (uint8_t)(cmd & ~CMD_RE));
    outb((uint16_t)(r->io_base + REG_CMD), (uint8_t)(cmd | CMD_RE));
    outl((uint16_t)(r->io_base + REG_RCR), 0x0F | (1 << 7));
    r->rx_offset = 0;
    outw((uint16_t)(r->io_base + REG_CAPR), (uint16_t)(0u - 16));
}

static void rtl_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;
    uint16_t status = inw((uint16_t)(g_rtl.io_base + REG_ISR));
    outw((uint16_t)(g_rtl.io_base + REG_ISR), status);

    if (status & ISR_ROK)   rtl_receive_all(&g_rtl);
    if (status & ISR_RXOVW) {
        serial_puts("[RTL8139] rx buffer overflow - resetting receiver\n");
        rtl_reset_rx(&g_rtl);
    }
    if (status & ISR_TER)   serial_puts("[RTL8139] tx error\n");
}

int rtl8139_probe_and_init(void)
{
    pci_device_t devices[64];
    int count = pci_scan(devices, 64);

    pci_device_t dev;
    if (!pci_find(devices, count, RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev)) {
        serial_puts("[RTL8139] not found on PCI bus\n");
        return 0;
    }

    pci_enable_device(&dev);
    uint16_t io_base = (uint16_t)(dev.bar[0] & ~0x3u);
    g_rtl.io_base = io_base;

    outb((uint16_t)(io_base + REG_CONFIG1), 0x00);
    outb((uint16_t)(io_base + REG_CMD), CMD_RST);
    for (int i = 0; i < 1000000; i++)
        if (!(inb((uint16_t)(io_base + REG_CMD)) & CMD_RST)) break;

    for (int i = 0; i < ETH_ALEN; i++)
        g_rtl.netdev.mac[i] = inb((uint16_t)(io_base + REG_MAC0 + i));

    g_rtl.rx_buffer = (uint8_t *)kmalloc(RX_BUF_SIZE);
    g_rtl.rx_offset = 0;
    if (!g_rtl.rx_buffer) { serial_puts("[RTL8139] OOM allocating rx buffer\n"); return 0; }

    for (int i = 0; i < TX_RING_COUNT; i++) {
        g_rtl.tx_buffer[i] = (uint8_t *)kmalloc(TX_BUF_SIZE);
        if (!g_rtl.tx_buffer[i]) { serial_puts("[RTL8139] OOM allocating tx buffer\n"); return 0; }
    }
    g_rtl.tx_next = 0;

    outl((uint16_t)(io_base + REG_RBSTART), (uint32_t)(uintptr_t)g_rtl.rx_buffer);
    outw((uint16_t)(io_base + REG_IMR), ISR_ROK | ISR_RXOVW | ISR_TER | 0x0004 /* TOK */);
    outl((uint16_t)(io_base + REG_RCR), 0x0F | (1 << 7)); /* accept all + WRAP */
    outb((uint16_t)(io_base + REG_CMD), CMD_RE | CMD_TE);

    interrupts_register_handler((uint32_t)(0x20 + dev.interrupt_line), rtl_irq_handler);
    interrupts_unmask_irq(dev.interrupt_line);

    strncpy(g_rtl.netdev.name, "rtl8139_0", sizeof(g_rtl.netdev.name) - 1);
    g_rtl.netdev.send = rtl_send;
    g_rtl.netdev.driver_data = &g_rtl;
    g_rtl.netdev.ip = 0;
    g_rtl.netdev.gateway_ip = 0;
    g_rtl.netdev.netmask = 0;
    g_rtl.netdev.dns_ip = 0;

    net_register_device(&g_rtl.netdev);

    char macstr[18];
    mac_to_string(g_rtl.netdev.mac, macstr);
    serial_puts("[RTL8139] up, MAC="); serial_puts(macstr); serial_puts("\n");
    return 1;
}
