#include "ps2.h"
#include "serial.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

#define I8042_DATA   0x60
#define I8042_STATUS 0x64
#define I8042_CMD    0x64

#define STATUS_OUT_FULL 0x01
#define STATUS_IN_FULL  0x02
#define STATUS_AUX      0x20

#define CMD_READ_CFG        0x20
#define CMD_WRITE_CFG        0x60
#define CMD_DISABLE_PORT1    0xAD
#define CMD_ENABLE_PORT1     0xAE
#define CMD_DISABLE_PORT2    0xA7
#define CMD_ENABLE_PORT2     0xA8
#define CMD_WRITE_TO_PORT2   0xD4
#define CMD_SELF_TEST        0xAA

#define CFG_PORT1_IRQ_EN   0x01
#define CFG_PORT2_IRQ_EN   0x02
#define CFG_TRANSLATION     0x40
#define CFG_PORT1_CLOCK_DIS 0x10
#define CFG_PORT2_CLOCK_DIS 0x20

static int wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (!(inb(I8042_STATUS) & STATUS_IN_FULL)) return 0;
        io_wait();
    }
    return -1;
}

static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (inb(I8042_STATUS) & STATUS_OUT_FULL) return 0;
        io_wait();
    }
    return -1;
}

static void write_cmd(uint8_t byte) {
    wait_input_empty();
    outb(I8042_CMD, byte);
}

static void write_data(uint8_t byte) {
    wait_input_empty();
    outb(I8042_DATA, byte);
}

static uint8_t read_cfg_byte(void) {
    write_cmd(CMD_READ_CFG);
    if (wait_output_full() < 0) return 0;
    return inb(I8042_DATA);
}

static void write_cfg_byte(uint8_t cfg) {
    write_cmd(CMD_WRITE_CFG);
    write_data(cfg);
}

void ps2_flush_output(void) {
    while (inb(I8042_STATUS) & STATUS_OUT_FULL) {
        (void)inb(I8042_DATA);
        io_wait();
    }
}

void ps2_controller_init(void) {

    write_cmd(CMD_DISABLE_PORT1);
    write_cmd(CMD_DISABLE_PORT2);

    ps2_flush_output();

    write_cmd(CMD_SELF_TEST);
    uint8_t self_test = 0;
    if (wait_output_full() == 0) self_test = inb(I8042_DATA);
    if (self_test == 0x55) {
        serial_puts("[PS2] Controller self-test OK.\n");
    } else {
        serial_puts("[PS2] WARNING: controller self-test did not return 0x55.\n");
    }

    uint8_t cfg = read_cfg_byte();
    cfg |= CFG_PORT1_IRQ_EN | CFG_PORT2_IRQ_EN;
    cfg &= (uint8_t)~(CFG_PORT1_CLOCK_DIS | CFG_PORT2_CLOCK_DIS | CFG_TRANSLATION);
    write_cfg_byte(cfg);

    write_cmd(CMD_ENABLE_PORT1);

    ps2_flush_output();

    serial_puts("[PS2] Controller init complete.\n");
}

void ps2_enable_port(int port) {
    write_cmd(port == PS2_PORT_MOUSE ? CMD_ENABLE_PORT2 : CMD_ENABLE_PORT1);
}

void ps2_disable_port(int port) {
    write_cmd(port == PS2_PORT_MOUSE ? CMD_DISABLE_PORT2 : CMD_DISABLE_PORT1);
}

void ps2_set_port_irq_enabled(int port, int enabled) {
    uint8_t cfg = read_cfg_byte();
    uint8_t bit = (port == PS2_PORT_MOUSE) ? CFG_PORT2_IRQ_EN : CFG_PORT1_IRQ_EN;
    if (enabled) cfg |= bit;
    else         cfg &= (uint8_t)~bit;
    write_cfg_byte(cfg);
}

void ps2_send_to_device(int port, uint8_t cmd) {
    if (port == PS2_PORT_MOUSE) {
        write_cmd(CMD_WRITE_TO_PORT2);
    }
    write_data(cmd);
}

uint8_t ps2_read_data(void) {
    if (wait_output_full() < 0) return 0;
    return inb(I8042_DATA);
}

int ps2_read_data_nb(uint8_t *out_byte, int *out_is_mouse) {
    uint8_t status = inb(I8042_STATUS);
    if (!(status & STATUS_OUT_FULL)) return 0;

    if (out_is_mouse) *out_is_mouse = (status & STATUS_AUX) ? 1 : 0;
    if (out_byte) *out_byte = inb(I8042_DATA);
    return 1;
}

int ps2_status_output_is_mouse(void) {
    return (inb(I8042_STATUS) & STATUS_AUX) ? 1 : 0;
}