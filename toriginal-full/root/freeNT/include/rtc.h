#ifndef _RTC_H
#define _RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} rtc_time_t;

void rtc_read(rtc_time_t *out);

#endif
