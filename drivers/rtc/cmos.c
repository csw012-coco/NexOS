#include "drivers/rtc/cmos.h"

#include "arch/x86/io.h"

enum {
    CMOS_INDEX_PORT = 0x70u,
    CMOS_DATA_PORT = 0x71u,
    CMOS_REG_SECONDS = 0x00u,
    CMOS_REG_MINUTES = 0x02u,
    CMOS_REG_HOURS = 0x04u,
    CMOS_REG_WEEKDAY = 0x06u,
    CMOS_REG_DAY = 0x07u,
    CMOS_REG_MONTH = 0x08u,
    CMOS_REG_YEAR = 0x09u,
    CMOS_REG_STATUS_A = 0x0au,
    CMOS_REG_STATUS_B = 0x0bu,
    CMOS_REG_CENTURY = 0x32u,
    CMOS_STATUS_A_UIP = 0x80u,
    CMOS_STATUS_B_BINARY = 0x04u,
    CMOS_STATUS_B_24H = 0x02u
};

static uint8_t cmos_read_register(uint8_t reg) {
    outb(CMOS_INDEX_PORT, (uint8_t)(0x80u | reg));
    return inb(CMOS_DATA_PORT);
}

static int cmos_is_updating(void) {
    return (cmos_read_register(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UIP) != 0u;
}

static uint8_t cmos_bcd_to_bin(uint8_t value) {
    return (uint8_t)(((value >> 4) * 10u) + (value & 0x0fu));
}

static int cmos_is_leap_year(uint16_t year) {
    if ((year % 400u) == 0u) {
        return 1;
    }
    if ((year % 100u) == 0u) {
        return 0;
    }
    return (year % 4u) == 0u;
}

static uint8_t cmos_days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

    if (month == 0u || month > 12u) {
        return 0u;
    }
    if (month == 2u && cmos_is_leap_year(year)) {
        return 29u;
    }
    return days[month - 1u];
}

static int cmos_validate_snapshot(const struct cmos_rtc_info *info) {
    if (info == 0) {
        return 0;
    }
    if (info->year < 1970u || info->year > 2099u) {
        return 0;
    }
    if (info->month < 1u || info->month > 12u) {
        return 0;
    }
    if (info->day < 1u || info->day > cmos_days_in_month(info->year, info->month)) {
        return 0;
    }
    if (info->hour > 23u || info->minute > 59u || info->second > 59u) {
        return 0;
    }
    if (info->weekday > 7u) {
        return 0;
    }
    return 1;
}

static uint32_t cmos_unix_time(const struct cmos_rtc_info *info) {
    uint32_t days = 0u;

    for (uint16_t year = 1970u; year < info->year; year++) {
        days += cmos_is_leap_year(year) ? 366u : 365u;
    }
    for (uint8_t month = 1u; month < info->month; month++) {
        days += cmos_days_in_month(info->year, month);
    }
    days += (uint32_t)(info->day - 1u);
    return days * 86400u +
           (uint32_t)info->hour * 3600u +
           (uint32_t)info->minute * 60u +
           (uint32_t)info->second;
}

static void cmos_fill_snapshot(struct cmos_rtc_info *out) {
    uint8_t status_b;
    uint8_t hour_reg;
    uint8_t pm_flag;
    uint8_t century;
    uint16_t year;

    out->status_a = cmos_read_register(CMOS_REG_STATUS_A);
    status_b = cmos_read_register(CMOS_REG_STATUS_B);
    out->status_b = status_b;
    out->binary_mode = (uint8_t)((status_b & CMOS_STATUS_B_BINARY) != 0u);
    out->hour_24 = (uint8_t)((status_b & CMOS_STATUS_B_24H) != 0u);

    out->second = cmos_read_register(CMOS_REG_SECONDS);
    out->minute = cmos_read_register(CMOS_REG_MINUTES);
    hour_reg = cmos_read_register(CMOS_REG_HOURS);
    out->weekday = cmos_read_register(CMOS_REG_WEEKDAY);
    out->day = cmos_read_register(CMOS_REG_DAY);
    out->month = cmos_read_register(CMOS_REG_MONTH);
    out->year = cmos_read_register(CMOS_REG_YEAR);
    century = cmos_read_register(CMOS_REG_CENTURY);
    out->raw_year = (uint8_t)out->year;

    if (!out->binary_mode) {
        out->second = cmos_bcd_to_bin(out->second);
        out->minute = cmos_bcd_to_bin(out->minute);
        out->weekday = cmos_bcd_to_bin(out->weekday);
        out->day = cmos_bcd_to_bin(out->day);
        out->month = cmos_bcd_to_bin(out->month);
        out->year = cmos_bcd_to_bin(out->year);
        century = cmos_bcd_to_bin(century);
    }
    out->century = century;

    pm_flag = (uint8_t)(hour_reg & 0x80u);
    hour_reg &= 0x7fu;
    if (!out->binary_mode) {
        hour_reg = cmos_bcd_to_bin(hour_reg);
    }
    if (!out->hour_24) {
        if (pm_flag != 0u && hour_reg < 12u) {
            hour_reg = (uint8_t)(hour_reg + 12u);
        } else if (pm_flag == 0u && hour_reg == 12u) {
            hour_reg = 0u;
        }
    }
    out->hour = hour_reg;

    year = (uint16_t)out->year;
    if (century != 0u && century != 0xffu) {
        year = (uint16_t)((uint16_t)century * 100u + year);
    } else if (year < 70u) {
        year = (uint16_t)(2000u + year);
    } else {
        year = (uint16_t)(1900u + year);
    }
    out->year = year;
    out->valid = (uint8_t)cmos_validate_snapshot(out);
    out->unix_time = out->valid ? cmos_unix_time(out) : 0u;
}

static int cmos_same_snapshot(const struct cmos_rtc_info *a, const struct cmos_rtc_info *b) {
    return a->second == b->second &&
           a->minute == b->minute &&
           a->hour == b->hour &&
           a->weekday == b->weekday &&
           a->day == b->day &&
           a->month == b->month &&
           a->year == b->year;
}

int cmos_rtc_query(struct cmos_rtc_info *out) {
    struct cmos_rtc_info first;
    struct cmos_rtc_info second;
    uint32_t spins;

    if (out == 0) {
        return 0;
    }

    out->present = 0u;
    out->updating = 0u;
    out->valid = 0u;
    out->binary_mode = 0u;
    out->hour_24 = 0u;
    out->status_a = 0u;
    out->status_b = 0u;
    out->century = 0u;
    out->raw_year = 0u;
    out->second = 0u;
    out->minute = 0u;
    out->hour = 0u;
    out->weekday = 0u;
    out->day = 0u;
    out->month = 0u;
    out->year = 0u;
    out->unix_time = 0u;

    spins = 100000u;
    while (spins > 0u && cmos_is_updating()) {
        spins--;
    }
    if (spins == 0u) {
        out->present = 1u;
        out->updating = 1u;
        return 0;
    }

    for (uint32_t attempt = 0; attempt < 4u; attempt++) {
        cmos_fill_snapshot(&first);
        cmos_fill_snapshot(&second);
        if (cmos_same_snapshot(&first, &second)) {
            *out = second;
            out->present = 1u;
            out->updating = 0u;
            return 1;
        }
    }
    *out = second;
    out->present = 1u;
    out->updating = 0u;
    return 1;
}
