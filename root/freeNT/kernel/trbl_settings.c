#include "trbl_settings.h"
#include "ata.h"
#include "string.h"
#include "serial.h"

/* =============================================================================
 * TRBL_SETTINGS.C — read/write the on-disk TRBL settings sector.
 *
 * This is the kernel-side half of trbl_settings.h. stage2.s (real mode,
 * pre-kernel) reads the same sector with its own tiny hand-rolled
 * routine at boot to pick the VBE mode; this file is what lets the
 * *running OS* change that sector afterward - from the shell's
 * `settings` command, from a future first-boot wizard, wherever -
 * so the change actually takes effect on the next real boot instead
 * of requiring a full bootloader reinstall.
 * ========================================================================= */

static size_t offsetof_checksum(void);

/* Sum every byte of the sector except the trailing checksum field
 * itself, four bytes at a time as little-endian uint32_t words -
 * matches the loop stage2.s runs in real mode over the same 508
 * bytes. Deliberately not a "real" hash (CRC32/etc): stage2.s has to
 * be able to compute the same thing in a handful of 16-bit
 * instructions with no lookup table, and this is the simplest
 * function both sides can implement identically and verify against
 * each other. It exists to catch torn/interrupted writes (power loss
 * mid-sector-write, a half-flashed USB stick), not to defend against
 * deliberate tampering. */
static uint32_t trbl_settings_checksum(const trbl_settings_t *s) {
    const uint8_t *bytes = (const uint8_t *)s;
    uint32_t sum = 0;
    size_t payload_len = offsetof_checksum();
    for (size_t i = 0; i < payload_len; i += 4) {
        uint32_t word;
        memcpy(&word, bytes + i, 4);
        sum += word;
    }
    return sum;
}

/* offsetof(trbl_settings_t, checksum) via a real compile-time constant
 * instead of <stddef.h>'s offsetof (kept out of the freestanding
 * include path this kernel builds with elsewhere) - checksum is the
 * last field, and struct is packed, so this is exactly "size of
 * everything before it". */
static size_t offsetof_checksum(void) {
    return sizeof(trbl_settings_t) - sizeof(((trbl_settings_t *)0)->checksum);
}

static void trbl_settings_defaults(trbl_settings_t *s) {
    memset(s, 0, sizeof(*s));
    s->magic       = TRBL_SETTINGS_MAGIC;
    s->version     = TRBL_SETTINGS_VERSION;
    s->struct_size = sizeof(trbl_settings_t);
    s->width       = 1024;
    s->height      = 768;
    s->bpp         = 32;
    s->net_mode    = TRBL_NET_OFF;
    s->boot_flags  = 0;
    s->mem_mode    = TRBL_MEM_MODE_MAX;
    s->ram_limit_kb = 0;
}

/* Validates magic/version/struct_size/checksum. Returns 1 if `s` is a
 * sector this code trusts, 0 otherwise (caller falls back to the
 * backup sector, then to defaults). */
static int trbl_settings_valid(const trbl_settings_t *s) {
    if (s->magic != TRBL_SETTINGS_MAGIC) return 0;
    if (s->version != TRBL_SETTINGS_VERSION) return 0;
    if (s->struct_size != sizeof(trbl_settings_t)) return 0;
    return trbl_settings_checksum(s) == s->checksum;
}

/* Reads the current TRBL settings. Tries the primary sector, falls
 * back to the backup, falls back to compiled-in defaults (never
 * fails outright - a missing/corrupt settings sector on a fresh disk
 * is expected, not an error). Returns 1 if the returned settings came
 * from disk (primary or backup), 0 if defaults were used. */
int trbl_settings_read(trbl_settings_t *out) {
    trbl_settings_t buf;

    if (ata_raw_read_sector(TRBL_SETTINGS_LBA, &buf) == 0 && trbl_settings_valid(&buf)) {
        *out = buf;
        return 1;
    }
    serial_puts("[TRBL] primary settings sector invalid/unreadable, trying backup\n");

    if (ata_raw_read_sector(TRBL_SETTINGS_BACKUP_LBA, &buf) == 0 && trbl_settings_valid(&buf)) {
        *out = buf;
        /* Primary was bad but backup is good - repair the primary now
         * so the next boot doesn't have to fall through again. */
        buf.checksum = trbl_settings_checksum(&buf);
        ata_raw_write_sector(TRBL_SETTINGS_LBA, &buf);
        return 1;
    }

    serial_puts("[TRBL] no valid settings on disk, using defaults\n");
    trbl_settings_defaults(out);
    return 0;
}

/* Writes settings to both the primary and backup sectors (primary
 * first, then backup - if power is lost between the two, the next
 * boot's read falls back to whichever one is still valid rather than
 * trusting a half-written primary). Fixes up struct_size/checksum
 * itself so callers never have to remember to. Returns 0 on success,
 * -1 if either write failed. */
int trbl_settings_write(trbl_settings_t *s) {
    s->magic       = TRBL_SETTINGS_MAGIC;
    s->version     = TRBL_SETTINGS_VERSION;
    s->struct_size = sizeof(trbl_settings_t);
    s->checksum    = trbl_settings_checksum(s);

    if (ata_raw_write_sector(TRBL_SETTINGS_LBA, s) != 0) {
        serial_puts("[TRBL] failed writing primary settings sector\n");
        return -1;
    }
    if (ata_raw_write_sector(TRBL_SETTINGS_BACKUP_LBA, s) != 0) {
        serial_puts("[TRBL] failed writing backup settings sector (primary OK)\n");
        return -1;
    }
    return 0;
}

/* Convenience used by `settings resolution <WxH>` (shell.c) - read,
 * patch width/height, write back, leaving every other field (network
 * config, boot flags) untouched. */
int trbl_settings_set_resolution(uint16_t width, uint16_t height) {
    trbl_settings_t s;
    trbl_settings_read(&s);
    s.width  = width;
    s.height = height;
    s.bpp    = 32;
    return trbl_settings_write(&s);
}

/* Convenience used by a future `settings network` shell command.
 * static_ip/mask/gateway/dns are only consulted when mode ==
 * TRBL_NET_STATIC; pass NULL for any/all of them under DHCP or OFF. */
int trbl_settings_set_network(uint8_t mode,
                               const uint8_t static_ip[4],
                               const uint8_t static_mask[4],
                               const uint8_t static_gateway[4],
                               const uint8_t static_dns[4]) {
    trbl_settings_t s;
    trbl_settings_read(&s);
    s.net_mode = mode;
    if (static_ip)      memcpy(s.static_ip,      static_ip,      4);
    if (static_mask)    memcpy(s.static_mask,    static_mask,    4);
    if (static_gateway) memcpy(s.static_gateway, static_gateway, 4);
    if (static_dns)     memcpy(s.static_dns,     static_dns,     4);
    return trbl_settings_write(&s);
}

/* Convenience used by the shell's `mem` command. mode is one of
 * TRBL_MEM_MODE_*; custom_kb is only consulted (and only stored) for
 * TRBL_MEM_MODE_CUSTOM. */
int trbl_settings_set_memory(uint8_t mode, uint32_t custom_kb) {
    trbl_settings_t s;
    trbl_settings_read(&s);
    s.mem_mode = mode;
    if (mode == TRBL_MEM_MODE_CUSTOM) s.ram_limit_kb = custom_kb;
    return trbl_settings_write(&s);
}
