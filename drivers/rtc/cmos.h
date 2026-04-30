#pragma once

#include <stdint.h>

struct cmos_rtc_info {
    uint8_t present;
    uint8_t updating;
    uint8_t valid;
    uint8_t binary_mode;
    uint8_t hour_24;
    uint8_t status_a;
    uint8_t status_b;
    uint8_t century;
    uint8_t raw_year;
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint32_t unix_time;
};

int cmos_rtc_query(struct cmos_rtc_info *out);
