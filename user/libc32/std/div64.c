#include <stdint.h>

static uint64_t udivmod64(uint64_t num, uint64_t den, uint64_t *rem_out) {
    uint64_t quot = 0;
    uint64_t rem = 0;
    int bit;

    if (den == 0u) {
        if (rem_out != 0) {
            *rem_out = num;
        }
        return UINT64_MAX;
    }
    for (bit = 63; bit >= 0; bit--) {
        rem = (rem << 1) | ((num >> bit) & 1u);
        if (rem >= den) {
            rem -= den;
            quot |= 1ull << bit;
        }
    }
    if (rem_out != 0) {
        *rem_out = rem;
    }
    return quot;
}

uint64_t __udivdi3(uint64_t num, uint64_t den) {
    return udivmod64(num, den, 0);
}

uint64_t __umoddi3(uint64_t num, uint64_t den) {
    uint64_t rem;

    (void)udivmod64(num, den, &rem);
    return rem;
}

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_out) {
    return udivmod64(num, den, rem_out);
}

int64_t __divdi3(int64_t num, int64_t den) {
    int negative = 0;
    uint64_t unum;
    uint64_t uden;
    uint64_t quot;

    if (num < 0) {
        negative = !negative;
        unum = (uint64_t)(-(num + 1)) + 1u;
    } else {
        unum = (uint64_t)num;
    }
    if (den < 0) {
        negative = !negative;
        uden = (uint64_t)(-(den + 1)) + 1u;
    } else {
        uden = (uint64_t)den;
    }
    quot = udivmod64(unum, uden, 0);
    return negative ? -(int64_t)quot : (int64_t)quot;
}

int64_t __moddi3(int64_t num, int64_t den) {
    int negative = 0;
    uint64_t unum;
    uint64_t uden;
    uint64_t rem;

    if (num < 0) {
        negative = 1;
        unum = (uint64_t)(-(num + 1)) + 1u;
    } else {
        unum = (uint64_t)num;
    }
    if (den < 0) {
        uden = (uint64_t)(-(den + 1)) + 1u;
    } else {
        uden = (uint64_t)den;
    }
    (void)udivmod64(unum, uden, &rem);
    return negative ? -(int64_t)rem : (int64_t)rem;
}
