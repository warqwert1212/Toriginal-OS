/* =============================================================================
 * rtc.c — CMOS Real-Time Clock reader
 *
 * Reads the current date/time from the CMOS RTC (port 0x70/0x71), the
 * same chip every PC has had since the original IBM AT. Used to drive
 * the top status bar's date/time display.
 * ========================================================================= */

#include "rtc.h"

static inline uint8_t cmos_read(uint8_t reg) {
    __asm__ volatile("outb %0, $0x70" :: "a"(reg));
    uint8_t v;
    __asm__ volatile("inb $0x71, %0" : "=a"(v));
    return v;
}

static int cmos_update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v / 16) * 10));
}

static int cmos_wait_not_updating(void) {
    /* Bounded — a real RTC never holds this flag for more than ~244us,
     * so any reasonable timeout is generous. Returns -1 on timeout so
     * the caller can fall back gracefully instead of hanging forever. */
    for (uint32_t i = 0; i < 100000u; i++) {
        if (!cmos_update_in_progress()) return 0;
    }
    return -1;
}

void rtc_read(rtc_time_t *out) {
    /* Wait for any in-progress update to finish, then read twice to
     * confirm we didn't catch a tick mid-update (standard CMOS dance).
     * Every wait/retry loop below is bounded — no path here can spin
     * forever regardless of CMOS timing quirks under virtualization. */
    uint8_t sec, min, hour, day, month, year;
    uint8_t sec2, min2, hour2, day2, month2, year2;

    int attempts = 0;
    do {
        cmos_wait_not_updating();
        sec   = cmos_read(0x00);
        min   = cmos_read(0x02);
        hour  = cmos_read(0x04);
        day   = cmos_read(0x07);
        month = cmos_read(0x08);
        year  = cmos_read(0x09);

        cmos_wait_not_updating();
        sec2   = cmos_read(0x00);
        min2   = cmos_read(0x02);
        hour2  = cmos_read(0x04);
        day2   = cmos_read(0x07);
        month2 = cmos_read(0x08);
        year2  = cmos_read(0x09);
    } while ((sec != sec2 || min != min2 || hour != hour2 ||
              day != day2 || month != month2 || year != year2) &&
             ++attempts < 10);

    uint8_t regB = cmos_read(0x0B);
    int is_bcd    = !(regB & 0x04);
    int is_12hr   = !(regB & 0x02);

    if (is_bcd) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin((uint8_t)(hour & 0x7F));
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    } else {
        hour = (uint8_t)(hour & 0x7F);
    }

    if (is_12hr && (cmos_read(0x04) & 0x80)) {
        hour = (uint8_t)((hour % 12) + 12);
    }

    out->second = sec;
    out->minute = min;
    out->hour   = hour;
    out->day    = day;
    out->month  = month;
    out->year   = (uint16_t)(2000 + year); /* CMOS only stores 2-digit year */
}
