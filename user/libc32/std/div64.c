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
